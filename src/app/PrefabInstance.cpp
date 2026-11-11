// Live prefab instances (see PrefabInstance.h).
#include "PrefabInstance.h"
#include "Content.h"
#include "vp3d/AssetImport.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace app {

namespace fs = std::filesystem;

namespace {

long long fileStamp(const std::wstring& f) {
    std::error_code ec;
    auto t = fs::last_write_time(fs::path(f), ec);
    return ec ? 0 : (long long)t.time_since_epoch().count();
}

uint32_t maxObjectId(const Scene& sc) {
    uint32_t m = 0;
    for (const Object& o : sc.objects) m = std::max(m, o.id);
    for (const AreaLight& l : sc.lights) m = std::max(m, l.id);
    for (const Collection& c : sc.collections) m = std::max(m, c.id);
    return std::max(m, sc.sun.id);
}

float4 rootRotation(const Object& r) {
    return r.useRot ? r.rot : qAxisAngle(float3(0, 1, 0), r.yaw);
}

const PrefabClip* findClip(const PrefabAsset& a, const std::string& name) {
    if (name.empty()) return nullptr;
    for (const PrefabClip& c : a.clips)
        if (c.name == name) return &c;
    return nullptr;
}

// The asset version each root's parts were built from (root id -> version), per library
std::unordered_map<const PrefabLibrary*, std::unordered_map<uint32_t, uint32_t>>& builtVersions() {
    static std::unordered_map<const PrefabLibrary*, std::unordered_map<uint32_t, uint32_t>> m;
    return m;
}

} // namespace

// --------------------------------------------------------------------------- library

bool PrefabLibrary::load(const std::string& path, Entry& e) {
    const std::wstring file = absPath ? absPath(path) : fromUtf8(path);
    std::ifstream in(fs::path(file), std::ios::binary);
    e.stamp = fileStamp(file);
    e.version = ++m_version;
    e.asset.reset();
    if (!in) {
        warnings.push_back("Prefab not found: " + path);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    Json j;
    auto a = std::make_shared<PrefabAsset>();
    std::string err;
    if (!Json::parse(ss.str(), j) || !prefabFromJson(j, *a, &err)) {
        warnings.push_back("Prefab not read: " + path + (err.empty() ? "" : " (" + err + ")"));
        return false;
    }
    e.asset = a;
    return true;
}

const PrefabAsset* PrefabLibrary::get(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = m_entries.find(path);
    if (it == m_entries.end()) {
        Entry e;
        load(path, e);
        it = m_entries.emplace(path, e).first;
    }
    return it->second.asset.get();
}

uint32_t PrefabLibrary::versionOf(const std::string& path) const {
    auto it = m_entries.find(path);
    return it == m_entries.end() ? 0 : it->second.version;
}

bool PrefabLibrary::refresh() {
    bool any = false;
    for (auto& [path, e] : m_entries) {
        const std::wstring file = absPath ? absPath(path) : fromUtf8(path);
        if (fileStamp(file) != e.stamp) {
            load(path, e);
            any = true;
        }
    }
    return any;
}

// --------------------------------------------------------------------------- instances

int instanceRootOf(const Scene& sc, int obj) {
    if (obj < 0 || obj >= (int)sc.objects.size()) return obj;
    const uint32_t r = sc.objects[obj].instanceRoot;
    if (!r) return obj;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[i].id == r && sc.objects[i].isInstanceRoot()) return i;
    return obj;
}

int addPrefabInstance(Scene& sc, PrefabLibrary& lib, const std::string& path, float3 pos, float yawDeg, const std::string& name) {
    const PrefabAsset* a = lib.get(path);
    if (!a) return -1;
    Object r;
    r.name = name.empty() ? a->name : name;
    r.part = kPrefabPart;
    r.prefab = path;
    r.mesh = -1;
    r.position = pos;
    r.yaw = yawDeg * 3.14159265f / 180.0f;
    r.useRot = true;
    r.rot = qAxisAngle(float3(0, 1, 0), r.yaw);
    r.clip = findClip(*a, "Idle") ? "Idle" : "";
    r.id = maxObjectId(sc) + 1;
    return sc.addObject(r);
}

bool syncPrefabInstances(Scene& sc, PrefabLibrary& lib, bool allowErase) {
    auto& built = builtVersions()[&lib];
    uint32_t nextId = maxObjectId(sc);
    std::unordered_map<uint32_t, int> roots;   // root id -> index
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
        Object& o = sc.objects[i];
        if (!o.isInstanceRoot()) continue;
        if (!o.id) o.id = ++nextId;
        o.mesh = -1;
        o.part = kPrefabPart;
        roots[o.id] = i;
    }
    // the parts present, and those to remove (orphans, unknown parts, duplicates, parts of a changed prefab)
    std::map<std::pair<uint32_t, uint32_t>, int> present;
    std::vector<int> remove;
    std::unordered_map<uint32_t, bool> rebuild;
    for (auto& [id, ri] : roots) {
        const Object& r = sc.objects[ri];
        lib.get(r.prefab);
        auto b = built.find(id);
        rebuild[id] = b != built.end() && b->second != lib.versionOf(r.prefab);
    }
    for (int i = 0; i < (int)sc.objects.size(); ++i) {
        const Object& o = sc.objects[i];
        if (!o.isInstancePart()) continue;
        auto r = roots.find(o.instanceRoot);
        const PrefabAsset* a = r == roots.end() ? nullptr : lib.get(sc.objects[r->second].prefab);
        const bool keep = a && !rebuild[o.instanceRoot] && a->findPart(o.instancePart) >= 0 &&
                          !present.count({o.instanceRoot, o.instancePart});
        if (keep) present[{o.instanceRoot, o.instancePart}] = i;
        else remove.push_back(i);
    }
    bool changed = !remove.empty();
    if (allowErase) {
        std::sort(remove.rbegin(), remove.rend());
        for (int i : remove) sc.objects.erase(sc.objects.begin() + i);
        if (!remove.empty()) {   // the root indices moved
            roots.clear();
            for (int i = 0; i < (int)sc.objects.size(); ++i)
                if (sc.objects[i].isInstanceRoot()) roots[sc.objects[i].id] = i;
        }
    } else {
        for (int i : remove) {   // Play: indices are graph handles, the dead parts only disappear
            Object& o = sc.objects[i];
            o.visible = false;
            o.selectable = false;
            o.instanceRoot = 0;
            o.instancePart = 0;
            o.part = "DEAD";
        }
    }
    // missing parts
    std::map<std::string, int> textures;
    for (auto& [id, ri] : roots) {
        const std::string prefabPath = sc.objects[ri].prefab;
        const PrefabAsset* a = lib.get(prefabPath);
        built[id] = lib.versionOf(prefabPath);
        if (!a) continue;
        for (const PrefabPart& p : a->parts) {
            if (p.isPivot() || present.count({id, p.id})) continue;
            Object o;
            o.name = p.name;
            o.useRot = true;
            o.instanceRoot = id;
            o.instancePart = p.id;
            o.parent = id;   // (editor: listed under the instance, outlined, duplicated and deleted with it)
            o.id = ++nextId;
            o.selectable = sc.objects[ri].selectable;
            Material m = p.material;
            m.emission = p.material.emission * p.material.emissionStrength;
            m.baseColorTex = -1;
            if (!p.meshPath.empty()) {
                const std::string key = std::string("file:") + p.meshPath;
                int mi = sc.findMesh(key);
                if (mi < 0) {
                    MeshData mesh;
                    std::string err;
                    const std::wstring file = lib.absPath ? lib.absPath(p.meshPath) : fromUtf8(p.meshPath);
                    const bool ok = lib.loadMesh ? lib.loadMesh(file, mesh, &err) : loadObj(file, mesh, nullptr, &err);
                    if (!ok) {
                        lib.warnings.push_back("Mesh of prefab " + prefabPath + " not loaded: " + p.meshPath);
                        continue;
                    }
                    mi = sc.addMesh(key, std::move(mesh));
                }
                o.mesh = mi;
                o.part = "MESH";
                if (p.recenter) {   // the pivot at the centre of the base of the mesh
                    const MeshData& md = sc.meshes[mi];
                    float3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
                    for (const Vertex& v : md.vertices) { mn = minv(mn, v.pos); mx = maxv(mx, v.pos); }
                    if (!md.vertices.empty()) o.pivot = float3((mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f);
                }
            } else {
                o.prim = p.prim;
                o.part = std::string("PRIM_") + primitiveName(p.prim.type);
                o.mesh = -1;
            }
            if (!p.texture.empty()) {
                auto t = textures.find(p.texture);
                if (t != textures.end()) m.baseColorTex = t->second;
                else {
                    Texture tex;
                    if (loadImage(lib.absPath ? lib.absPath(p.texture) : fromUtf8(p.texture), tex, nullptr, 2048)) {
                        tex.name = p.texture;
                        m.baseColorTex = textures[p.texture] = sc.addTexture(std::move(tex));
                    }
                }
            }
            o.material = sc.addMaterial(m);
            const int idx = sc.addObject(o);
            if (o.prim.type != PRIM_NONE) syncPrimitiveMesh(sc, idx);
            changed = true;
        }
    }
    if (changed) updatePrefabInstances(sc, lib, 0.0f, false);
    return changed;
}

void updatePrefabInstances(Scene& sc, PrefabLibrary& lib, float dt, bool advance, std::vector<std::pair<int, std::string>>* finished) {
    // parts of each root
    std::unordered_map<uint32_t, std::vector<int>> parts;
    bool any = false;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[i].isInstancePart()) parts[sc.objects[i].instanceRoot].push_back(i);
    std::vector<PartPose> pose;
    std::vector<PartWorld> world;
    for (int ri = 0; ri < (int)sc.objects.size(); ++ri) {
        Object& r = sc.objects[ri];
        if (!r.isInstanceRoot()) continue;
        const PrefabAsset* a = lib.get(r.prefab);
        if (!a) continue;
        any = true;
        const PrefabClip* clip = findClip(*a, r.clip);
        if (advance && clip && r.clipPlaying) {
            r.clipTime += dt * r.clipSpeed;
            if (r.clipLoop) {
                if (clip->length > 0) r.clipTime = std::fmod(r.clipTime, clip->length);
                if (r.clipTime < 0) r.clipTime += clip->length;
            } else if (r.clipTime >= clip->length) {
                r.clipTime = clip->length;
                r.clipPlaying = false;
                if (finished) finished->push_back({ri, r.clip});
            }
        }
        evaluatePose(*a, clip, clip ? r.clipTime : 0.0f, pose);
        worldTransforms(*a, pose, world);
        const float4 R = rootRotation(r);
        const float3 S = r.scale, P = r.position;
        auto it = parts.find(r.id);
        if (it == parts.end()) continue;
        for (int pi : it->second) {
            Object& o = sc.objects[pi];
            const int k = a->findPart(o.instancePart);
            if (k < 0 || k >= (int)world.size()) continue;
            const PartWorld& w = world[k];
            o.position = P + qRot(R, w.position * S);
            o.rot = qNormalize(qMul(R, w.rot));
            o.scale = w.scale * S;
            o.useRot = true;
            o.visible = r.visible && a->parts[k].visible;
            o.selectable = r.selectable;
            o.ghost = r.ghost;
            o.ghostAlpha = r.ghostAlpha;
        }
    }
    if (any && sc.objectsVersion) ++sc.objectsVersion;
}

bool playClip(Scene& sc, PrefabLibrary& lib, int root, const std::string& clip, bool loop, float speed) {
    if (root < 0 || root >= (int)sc.objects.size() || !sc.objects[root].isInstanceRoot()) return false;
    Object& r = sc.objects[root];
    const PrefabAsset* a = lib.get(r.prefab);
    if (!a || (!clip.empty() && !findClip(*a, clip))) return false;
    r.clip = clip;
    r.clipTime = speed < 0 && findClip(*a, clip) ? findClip(*a, clip)->length : 0.0f;
    r.clipLoop = loop;
    r.clipSpeed = speed;
    r.clipPlaying = !clip.empty();
    return true;
}

void stopClip(Scene& sc, int root) {
    if (root < 0 || root >= (int)sc.objects.size() || !sc.objects[root].isInstanceRoot()) return;
    sc.objects[root].clipPlaying = false;
}

bool unpackPrefabInstance(Scene& sc, int root) {
    if (root < 0 || root >= (int)sc.objects.size() || !sc.objects[root].isInstanceRoot()) return false;
    Object& r = sc.objects[root];
    const uint32_t id = r.id;
    r.prefab.clear();
    r.clip.clear();
    r.group = true;
    r.part = "GROUP";
    for (Object& o : sc.objects)
        if (o.instanceRoot == id) {
            o.instanceRoot = 0;
            o.instancePart = 0;
            o.parent = id;
        }
    return true;
}

std::vector<std::string> prefabFiles(const PrefabAsset& a) {
    std::vector<std::string> f;
    for (const PrefabPart& p : a.parts) {
        if (!p.meshPath.empty()) f.push_back(p.meshPath);
        if (!p.texture.empty()) f.push_back(p.texture);
    }
    std::sort(f.begin(), f.end());
    f.erase(std::unique(f.begin(), f.end()), f.end());
    return f;
}

} // namespace app

namespace app {

std::string PrefabLibrary::resolve(const std::string& nameOrPath) {
    if (nameOrPath.empty()) return std::string();
    if (nameOrPath.size() > 7 && nameOrPath.compare(nameOrPath.size() - 7, 7, ".prefab") == 0) return nameOrPath;
    auto it = m_resolved.find(nameOrPath);
    if (it != m_resolved.end()) return it->second;
    auto squash = [](std::string s) {
        std::string r;
        for (char c : s) if (c != ' ' && c != '_' && c != '-') r += (char)std::tolower((unsigned char)c);
        return r;
    };
    const std::string want = squash(nameOrPath);
    std::string found;
    const fs::path dir = absPath ? fs::path(absPath("game/prefabs")) : fs::path("game/prefabs");
    std::error_code ec;
    for (auto e = fs::recursive_directory_iterator(dir, ec); !ec && e != fs::recursive_directory_iterator(); e.increment(ec)) {
        if (!e->is_regular_file() || e->path().extension() != ".prefab") continue;
        if (squash(e->path().stem().u8string()) != want) continue;
        found = "game/prefabs/" + fs::relative(e->path(), dir, ec).generic_u8string();
        break;
    }
    m_resolved[nameOrPath] = found;
    return found;
}

} // namespace app
