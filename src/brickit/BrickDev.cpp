// BrickIt - DEV mode of the web builder (/* DEVMODE */ block): connectors coloured by type on every brick, the LDraw
// outline over a replaced part, OBJ export for Maya (zip: <id>.obj + LISEZMOI.txt, LDraw origin, cm, Y up, connector
// markers in ref_connecteurs_* groups), OBJ re-import (ref_ groups skipped, unit guess, centre check, live review) and
// the replacements kept next to the exe (the web keeps them in IndexedDB: "stocké sur cet appareil").
#include "BrickApp.h"
#include "app/Json.h"
#include "Deflate.h"
#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <cstdio>
#include <ctime>
#include <map>
#include <sstream>

namespace brickit {

static const char* kConnKey[CONN_COUNT] = {"stud", "antistud", "pin", "pinhole", "axle", "axlehole"};

static bool readAll(const std::wstring& path, std::string& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}
static bool writeAll(const std::wstring& path, const std::string& t) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    fwrite(t.data(), 1, t.size(), f);
    fclose(f);
    return true;
}
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
static std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '"' || c == '\\') o.push_back('\\'); o.push_back(c); }
    return o + "\"";
}
// (Math.round(v * 1e5) / 1e5).toString()
static std::string f5(float v) {
    double r = std::round((double)v * 1e5) / 1e5;
    if (r == 0) r = 0;
    char b[48];
    snprintf(b, sizeof(b), "%.5f", r);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string BrickApp::devId(int part) const {
    const Part& p = m_lib.parts[part];
    return p.ldraw.empty() ? p.name : p.ldraw;
}
int BrickApp::devPartById(const std::string& id) const {
    int i = m_lib.find(id);
    if (i >= 0) return i;
    for (int k = 0; k < (int)m_lib.parts.size(); ++k)
        if (devId(k) == id) return k;
    return -1;
}
std::wstring BrickApp::devDir() const { return m_exeDir + L"\\dev_overrides"; }

// ---------- OBJ export (devObjText)
std::string BrickApp::devObjText(int part, const MeshData& g, const float3& o, bool markers) const {
    const Part& p = m_lib.parts[part];
    std::string id = devId(part), gid;
    for (char c : id) gid.push_back((isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_');
    std::string title = p.title.empty() ? p.label : p.title, t2;
    for (char c : title) { if (std::isspace((unsigned char)c)) { if (t2.empty() || t2.back() != ' ') t2.push_back(' '); } else t2.push_back(c); }
    std::ostringstream L;
    L << "# BrickIt Builder LDraw · export mode développeur\n";
    L << "# brickit-part " << id << "\n";
    L << "# ldraw-title " << t2 << "\n";
    L << "# ldraw-release " << p.release << "\n";
    L << "# unites: cm (1 LDU = 0.04 cm) · Y vers le haut · origine = origine LDraw de la piece\n";
    L << "# Le groupe ref_connecteurs (" << (markers ? p.voxels.size() : 0) << " reperes) sert a l'alignement : il est ignore au reimport.\n";
    L << "g " << gid << "\n";
    for (const auto& v : g.vertices) L << "v " << f5(v.pos.x - o.x) << " " << f5(v.pos.y - o.y) << " " << f5(v.pos.z - o.z) << "\n";
    for (const auto& v : g.vertices) L << "vn " << f5(v.nrm.x) << " " << f5(v.nrm.y) << " " << f5(v.nrm.z) << "\n";
    for (size_t t = 0; t + 2 < g.indices.size(); t += 3) {
        uint32_t a = g.indices[t] + 1, b = g.indices[t + 1] + 1, c = g.indices[t + 2] + 1;
        L << "f " << a << "//" << a << " " << b << "//" << b << " " << c << "//" << c << "\n";
    }
    if (markers) {   // connector markers: small octahedra, one group per connector type
        size_t base = g.vertices.size();
        std::map<std::string, std::vector<float3>> byType;
        std::vector<std::string> order;
        for (size_t i = 0; i < p.voxels.size(); ++i) {
            std::string t = i < p.conn.size() && p.conn[i] < CONN_COUNT ? kConnKey[p.conn[i]] : "voxel";
            if (!byType.count(t)) order.push_back(t);
            byType[t].push_back(p.voxels[i]);
        }
        const float r = 0.06f;
        static const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
        for (const std::string& t : order) {
            L << "g ref_connecteurs_" << t << "\n";
            for (const float3& v : byType[t]) {
                float x = v.x - o.x, y = v.y - o.y, z = v.z - o.z;
                L << "v " << f5(x + r) << " " << f5(y) << " " << f5(z) << "\nv " << f5(x - r) << " " << f5(y) << " " << f5(z) << "\n";
                L << "v " << f5(x) << " " << f5(y + r) << " " << f5(z) << "\nv " << f5(x) << " " << f5(y - r) << " " << f5(z) << "\n";
                L << "v " << f5(x) << " " << f5(y) << " " << f5(z + r) << "\nv " << f5(x) << " " << f5(y) << " " << f5(z - r) << "\n";
                for (auto& fc : faces) L << "f " << base + fc[0] + 1 << " " << base + fc[1] + 1 << " " << base + fc[2] + 1 << "\n";
                base += 6;
            }
        }
    }
    return L.str();
}

// ---------- minimal ZIP (stored, no compression), as the web (devZip)
static uint32_t crc32(const std::string& d) {
    static uint32_t T[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) { uint32_t c = n; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1; T[n] = c; }
        init = true;
    }
    uint32_t c = 0xffffffffu;
    for (unsigned char u : d) c = T[(c ^ u) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, cen;
    auto u16 = [](std::string& s, uint32_t v) { s.push_back((char)(v & 255)); s.push_back((char)((v >> 8) & 255)); };
    auto u32 = [&](std::string& s, uint32_t v) { u16(s, v & 0xffff); u16(s, v >> 16); };
    for (const auto& [name, data] : files) {
        const uint32_t crc = crc32(data), off = (uint32_t)out.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0x0800); u16(out, 0); u16(out, 0); u16(out, 0);
        u32(out, crc); u32(out, (uint32_t)data.size()); u32(out, (uint32_t)data.size()); u16(out, (uint32_t)name.size()); u16(out, 0);
        out += name;
        out += data;
        u32(cen, 0x02014b50); u16(cen, 20); u16(cen, 20); u16(cen, 0x0800); u16(cen, 0); u16(cen, 0); u16(cen, 0);
        u32(cen, crc); u32(cen, (uint32_t)data.size()); u32(cen, (uint32_t)data.size()); u16(cen, (uint32_t)name.size());
        u16(cen, 0); u16(cen, 0); u16(cen, 0); u16(cen, 0); u32(cen, 0); u32(cen, off);
        cen += name;
    }
    const uint32_t cenOff = (uint32_t)out.size(), cenSize = (uint32_t)cen.size();
    out += cen;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (uint32_t)files.size()); u16(out, (uint32_t)files.size());
    u32(out, cenSize); u32(out, cenOff); u16(out, 0);
    return out;
}

std::string BrickApp::devReadme(int part) const {
    const Part& p = m_lib.parts[part];
    const std::string id = devId(part);
    return "BrickIt Builder LDraw · pièce " + id + " (" + p.label + ")\n\n"
           "Import dans Maya : Fichier > Importer, type OBJ. Unités de la scène Maya : centimètres. Y vers le haut.\n"
           "L'origine du fichier est l'origine LDraw de la pièce (1 LDU = 0,04 cm), donc ne déplace pas la pièce.\n\n"
           "Les groupes ref_connecteurs_* sont des repères des points de connexion (tenons, anti-tenons, trous…).\n"
           "Garde-les pour vérifier l'alignement : ils sont ignorés au réimport.\n\n"
           "Retour dans BrickIt : exporte en OBJ (normales activées) ou en FBX, en cm, puis dans BrickIt :\n"
           "mode DEV > fiche de la pièce (appui long dans la bibliothèque) > « Importer le maillage modifié ».\n"
           "Nom de fichier conseillé : " + id + ".obj (la référence est reconnue toute seule depuis le panneau DEV).\n\n"
           "Base LDraw : " + (p.release.empty() ? std::string("inconnue") : p.release) + " · géométrie LDraw (ldraw.org, CC BY).\n";
}

void BrickApp::devExport(int part) {
    Part* pp = m_lib.get(m_lib.parts[part].name);
    if (!pp) return;
    const std::string id = devId(part);
    const std::string obj = devObjText(part, pp->mesh, pp->ldrawOrigin, true);
    const std::string zip = zipStored({{id + ".obj", obj}, {"LISEZMOI.txt", devReadme(part)}});
    const bool replaced = m_devOverrides.count(id) && m_devUseOverrides;
    wchar_t file[MAX_PATH];
    wcsncpy_s(file, widen(id + "_maya.zip").c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Archive ZIP (*.zip)\0*.zip\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;   // (web: e.code === "declined")
    if (!writeAll(file, zip)) { toast("Enregistrement impossible : " + narrow(file)); return; }
    toast(id + "_maya.zip enregistré (" + (replaced ? "le remplacement actuel" : "la géométrie LDraw") + ")");
}

// ---------- OBJ import (devParseObj): faces of ref_ groups skipped, polygons fanned, flat normals when missing
bool BrickApp::devParseObj(const std::string& text, MeshData& g, std::map<std::string, std::string>& header) const {
    std::vector<float3> V, N;
    bool skip = false;
    g = MeshData();
    std::istringstream in(text);
    std::string line, s;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        s = line;
        while (!s.empty() && s.back() == '\\' && std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); s = s.substr(0, s.size() - 1) + " " + line; }
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        s = s.substr(a);
        if (s[0] == '#') {
            std::istringstream h(s.substr(1));
            std::string k, v;
            h >> k;
            if (k == "brickit-part" || k == "ldraw-release") { std::getline(h, v); size_t b = v.find_first_not_of(" \t"); header[k] = b == std::string::npos ? "" : v.substr(b); while (!header[k].empty() && std::isspace((unsigned char)header[k].back())) header[k].pop_back(); }
            continue;
        }
        std::istringstream ls(s);
        std::string k;
        ls >> k;
        if (k == "v") { float3 p; ls >> p.x >> p.y >> p.z; V.push_back(p); }
        else if (k == "vn") { float3 p; ls >> p.x >> p.y >> p.z; N.push_back(p); }
        else if (k == "g" || k == "o") {
            std::string rest, w;
            skip = false;
            while (ls >> w) {
                std::string lw = w;
                for (char& c : lw) c = (char)tolower((unsigned char)c);
                if (lw.rfind("ref_", 0) == 0 || lw.find(":ref_") != std::string::npos || lw.find("|ref_") != std::string::npos) skip = true;
            }
        } else if (k == "f" && !skip) {
            std::vector<std::pair<int, int>> c;
            std::string w;
            while (ls >> w) {
                int vi = atoi(w.c_str()), ni = -1;
                size_t s1 = w.find('/'), s2 = s1 == std::string::npos ? std::string::npos : w.find('/', s1 + 1);
                if (s2 != std::string::npos && s2 + 1 < w.size()) ni = atoi(w.c_str() + s2 + 1);
                vi = vi < 0 ? (int)V.size() + vi : vi - 1;
                if (ni != -1) ni = ni < 0 ? (int)N.size() + ni : ni - 1;
                c.push_back({vi, ni});
            }
            for (size_t i = 1; i + 1 < c.size(); ++i) {
                const std::pair<int, int> tri[3] = {c[0], c[i], c[i + 1]};
                bool ok = true;
                for (auto& t : tri) ok &= t.first >= 0 && t.first < (int)V.size();
                if (!ok) continue;
                float3 fn = normalize(cross(V[tri[1].first] - V[tri[0].first], V[tri[2].first] - V[tri[0].first]));
                for (auto& t : tri) {
                    Vertex vx;
                    vx.pos = V[t.first];
                    vx.nrm = (t.second >= 0 && t.second < (int)N.size()) ? N[t.second] : fn;
                    g.indices.push_back((uint32_t)g.vertices.size());
                    g.vertices.push_back(vx);
                }
            }
        }
    }
    return !g.indices.empty();
}

static void meshBounds(const MeshData& g, float3& mn, float3& mx) {
    mn = float3(1e30f);
    mx = float3(-1e30f);
    for (const auto& v : g.vertices) { mn = minv(mn, v.pos); mx = maxv(mx, v.pos); }
}

// In-file coordinates are LDraw-origin based: into BrickIt local space, scale guess and offset check (devFit).
void BrickApp::devFit(int part, MeshData& g, DevReview& R) {
    const Part& p = m_lib.parts[part];
    const MeshData& ref = p.hasLdrawMesh ? p.ldrawMesh : p.mesh;
    const float3 o = p.ldrawOrigin;
    for (auto& v : g.vertices) v.pos = v.pos + o;
    float3 rmn, rmx, imn, imx;
    meshBounds(ref, rmn, rmx);
    meshBounds(g, imn, imx);
    const float3 rs = rmx - rmn, is = imx - imn;
    const double ratio = std::cbrt((double)is.x * is.y * is.z / std::max(1e-9, (double)rs.x * rs.y * rs.z));
    struct Cand { double k; const char* unit; };
    static const Cand cands[] = {{1, ""}, {10, "mm"}, {0.1, "dm"}, {100, "m"}, {0.01, "×100"}, {2.54, "pouces"}, {1.0 / 25, "LDU"}, {1 / 2.54, "?"}};
    Cand best = cands[0];
    double bestErr = 1e30;
    for (const Cand& c : cands) { double e = std::fabs(std::log(ratio / c.k)); if (e < bestErr) { bestErr = e; best = c; } }
    R.scale = 1;
    R.note.clear();
    char b[160];
    if (best.k != 1 && bestErr < 0.12) {
        R.scale = (float)(1 / best.k);
        for (auto& v : g.vertices) v.pos = o + (v.pos - o) * R.scale;
        snprintf(b, sizeof(b), "Échelle corrigée ×%.3g (fichier en %s ?)", R.scale, best.unit);
        R.note = b;
    } else if (bestErr > 0.25) {
        snprintf(b, sizeof(b), "Taille très différente de LDraw (×%.2f) : vérifie les unités", ratio);
        R.note = b;
    }
    meshBounds(g, imn, imx);
    R.size = imx - imn;
    R.ref = rs;
    const float ax[3] = {R.size.x / rs.x, R.size.y / rs.y, R.size.z / rs.z};
    R.swapped = std::max(ax[0], std::max(ax[1], ax[2])) / std::min(ax[0], std::min(ax[1], ax[2])) > 1.6f;
    R.offset = (imn + imx) * 0.5f - (rmn + rmx) * 0.5f;
}

// ---------- geometry swap (render mesh only; the collision box and the connectors stay LDraw)
void BrickApp::devSetGeometry(int part, const MeshData& g) {
    Part& p = m_lib.parts[part];
    if (!p.hasLdrawMesh) { p.ldrawMesh = p.mesh; p.hasLdrawMesh = true; }
    p.mesh = g;
    const int mesh = part < (int)m_partMesh.size() ? m_partMesh[part] : -1;
    if (mesh >= 0) {
        m_scene.meshes[mesh] = g;
        if (mesh < (int)m_scene.lods.size()) m_scene.lods[mesh].lod1 = -1;   // the box level of detail stays
        ++m_scene.geometryVersion;
        ++m_scene.objectsVersion;
        m_renderer.resetAccumulation();
    }
}

void BrickApp::devApplyAll() {
    for (int i = 0; i < (int)m_lib.parts.size(); ++i) {
        Part& p = m_lib.parts[i];
        auto it = m_devOverrides.find(devId(i));
        const bool want = it != m_devOverrides.end() && m_devUseOverrides && m_lib.get(p.name);
        if (want) devSetGeometry(i, it->second.geometry);
        else if (p.hasLdrawMesh && p.devActive) devSetGeometry(i, p.ldrawMesh);
        p.devActive = want;
    }
}

// ---------- storage: <exe>\dev_overrides\<id>.obj (LDraw-origin cm, like the export) + <id>.json (meta)
void BrickApp::devLoadAll() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((devDir() + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string text, obj;
        app::Json j;
        const std::wstring base = devDir() + L"\\" + std::wstring(fd.cFileName).substr(0, wcslen(fd.cFileName) - 5);
        if (!readAll(base + L".json", text) || !app::Json::parse(text, j) || !readAll(base + L".obj", obj)) continue;
        DevOverride o;
        o.id = j["id"].str();
        o.label = j["label"].str();
        o.baseRelease = j["baseRelease"].str();
        o.importedAt = j["importedAt"].str();
        o.fileName = j["fileName"].str();
        o.triangles = (int)j["triangles"].num();
        o.ldrawTriangles = (int)j["ldrawTriangles"].num();
        std::map<std::string, std::string> hd;
        if (o.id.empty() || !devParseObj(obj, o.geometry, hd)) continue;
        const int part = devPartById(o.id);
        if (part >= 0) for (auto& v : o.geometry.vertices) v.pos = v.pos + m_lib.parts[part].ldrawOrigin;
        m_devOverrides[o.id] = std::move(o);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

bool BrickApp::devSave(const DevOverride& o, const std::string& obj) {
    CreateDirectoryW(devDir().c_str(), nullptr);
    char b[64];
    std::string meta = "{\"id\": " + jstr(o.id) + ", \"label\": " + jstr(o.label) + ", \"baseRelease\": " + jstr(o.baseRelease) +
                       ", \"importedAt\": " + jstr(o.importedAt) + ", \"fileName\": " + jstr(o.fileName);
    snprintf(b, sizeof(b), ", \"triangles\": %d, \"ldrawTriangles\": %d}\n", o.triangles, o.ldrawTriangles);
    meta += b;
    const std::wstring base = devDir() + L"\\" + widen(o.id);
    return writeAll(base + L".obj", obj) && writeAll(base + L".json", meta);
}

void BrickApp::devDelete(const std::string& id) {
    const std::wstring base = devDir() + L"\\" + widen(id);
    DeleteFileW((base + L".obj").c_str());
    DeleteFileW((base + L".json").c_str());
}

// ---------- import (devImportFile / devReview)
void BrickApp::devImport(int forced) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Maillage (*.obj;*.fbx;*.txt)\0*.obj;*.fbx;*.txt\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::wstring path = file;
    std::string fileName = narrow(path.substr(path.find_last_of(L"\\/") + 1));
    std::string lower = fileName;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".fbx") {   // (native: no FBX reader)
        toast("Lecture impossible : le FBX n'est pas lu par la version native, exporte en OBJ (normales activées)");
        return;
    }
    std::string text;
    if (!readAll(path, text)) { toast("Lecture impossible : " + fileName); return; }
    MeshData g;
    std::map<std::string, std::string> header;
    if (!devParseObj(text, g, header)) { toast("Aucun maillage trouvé dans " + fileName + " (hors ref_connecteurs)"); return; }
    int part = forced;
    if (part < 0) {
        std::string guess = header.count("brickit-part") ? header["brickit-part"] : "";
        if (guess.empty()) {   // /^([0-9a-z]+?)(?:[_. -]|$)/i
            size_t k = 0;
            while (k < fileName.size() && isalnum((unsigned char)fileName[k])) ++k;
            if (k > 0 && (k == fileName.size() || strchr("_. -", fileName[k]))) guess = fileName.substr(0, k);
        }
        part = guess.empty() ? -1 : devPartById(guess);
        if (part < 0) { toast("Référence inconnue : ouvre la fiche de la pièce (appui long) puis « Importer le maillage modifié »"); return; }
    }
    if (forced >= 0 && header.count("brickit-part") && header["brickit-part"] != devId(part)) toast("Attention : ce fichier vient de la pièce " + header["brickit-part"]);
    if (!m_lib.get(m_lib.parts[part].name)) return;
    DevReview R;
    devFit(part, g, R);
    R.open = true;
    R.part = part;
    R.fileName = fileName;
    R.prev = m_lib.parts[part].mesh;
    R.g = g;
    R.wasUse = m_devUseOverrides;
    devSetGeometry(part, g);   // live preview while the sheet is open
    m_devUseOverrides = true;
    bool any = false;
    for (const Brick& b : m_bricks) any |= b.part == part;
    if (!any) {
        ImVec2 c((m_stage.x + m_stage.z) * 0.5f, (m_stage.y + m_stage.w) * 0.5f);
        spawn(part, c.x, c.y);
    }
    m_devReview = std::move(R);
}

void BrickApp::devReviewCancel() {
    devSetGeometry(m_devReview.part, m_devReview.prev);
    m_devUseOverrides = m_devReview.wasUse;
    devApplyAll();
    m_devReview.open = false;
}

void BrickApp::devReviewRecenter() {
    DevReview& R = m_devReview;
    for (auto& v : R.g.vertices) v.pos = v.pos - R.offset;
    R.offset = float3(0);
    devSetGeometry(R.part, R.g);
    toast("Recalé sur le centre de la pièce LDraw");
}

void BrickApp::devReviewKeep() {
    DevReview& R = m_devReview;
    const Part& p = m_lib.parts[R.part];
    DevOverride o;
    o.id = devId(R.part);
    o.label = p.label;
    o.baseRelease = p.release;
    {
        time_t t = time(nullptr);
        tm g;
        gmtime_s(&g, &t);
        char b[32];
        strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S.000Z", &g);
        o.importedAt = b;
    }
    o.fileName = R.fileName;
    o.triangles = (int)(R.g.indices.size() / 3);
    o.ldrawTriangles = (int)((p.hasLdrawMesh ? p.ldrawMesh : R.prev).indices.size() / 3);
    o.geometry = R.g;
    // stored as OBJ in LDraw-origin cm, like the export (no connector markers)
    const std::string obj = devObjText(R.part, R.g, p.ldrawOrigin, false);
    if (!devSave(o, obj)) { toast("Enregistrement impossible : " + narrow(devDir())); return; }
    const std::string id = o.id;
    m_devOverrides[id] = std::move(o);
    devApplyAll();
    R.open = false;
    toast("Remplacement de " + id + " enregistré sur cet appareil");
}

void BrickApp::devRevert(int part) {
    const std::string id = devId(part);
    devDelete(id);
    m_devOverrides.erase(id);
    Part& p = m_lib.parts[part];
    if (p.hasLdrawMesh) devSetGeometry(part, p.ldrawMesh);
    devApplyAll();
}

void BrickApp::devSetOn(bool v) {
    m_dev = v;
    savePrefs();
    toast(v ? "Mode développeur : connecteurs LDraw affichés, export / import Maya dans la fiche des pièces" : "Mode développeur désactivé");
}

// ---------- overlays on every brick: connectors by type (8 px squares, over everything) and the LDraw outline
// (edges of more than 30 degrees) over a replaced part
static const ImU32 kDevTypeCol[CONN_COUNT] = {IM_COL32(0xf7, 0xc5, 0x1e, 242), IM_COL32(0xff, 0x8a, 0x3d, 242), IM_COL32(0x7f, 0xe3, 0xff, 242),
                                              IM_COL32(0x4a, 0xa3, 0xff, 242), IM_COL32(0x5f, 0xd0, 0x68, 242), IM_COL32(0xc7, 0x7d, 0xff, 242)};

const std::vector<std::pair<float3, float3>>& BrickApp::devEdges(int part) {
    auto it = m_devEdgeCache.find(part);
    if (it != m_devEdgeCache.end()) return it->second;
    std::vector<std::pair<float3, float3>>& out = m_devEdgeCache[part];
    const Part& p = m_lib.parts[part];
    const MeshData& g = p.hasLdrawMesh ? p.ldrawMesh : p.mesh;
    // vertices merged by position (1e-4 cm), edge -> face normals (THREE.EdgesGeometry, threshold 30 degrees)
    std::map<std::tuple<long, long, long>, uint32_t> ids;
    std::vector<float3> pos;
    auto idOf = [&](const float3& v) {
        auto k = std::make_tuple(std::lround(v.x * 1e4f), std::lround(v.y * 1e4f), std::lround(v.z * 1e4f));
        auto f = ids.find(k);
        if (f != ids.end()) return f->second;
        uint32_t i = (uint32_t)pos.size();
        ids[k] = i;
        pos.push_back(v);
        return i;
    };
    std::map<std::pair<uint32_t, uint32_t>, std::vector<float3>> edges;
    for (size_t t = 0; t + 2 < g.indices.size(); t += 3) {
        uint32_t a = idOf(g.vertices[g.indices[t]].pos), b = idOf(g.vertices[g.indices[t + 1]].pos), c = idOf(g.vertices[g.indices[t + 2]].pos);
        if (a == b || b == c || a == c) continue;
        float3 n = normalize(cross(pos[b] - pos[a], pos[c] - pos[a]));
        const uint32_t e[3][2] = {{a, b}, {b, c}, {c, a}};
        for (auto& ed : e) edges[{std::min(ed[0], ed[1]), std::max(ed[0], ed[1])}].push_back(n);
    }
    const float thr = std::cos(radians(30.0f));
    for (auto& [k, ns] : edges)
        if (ns.size() == 1 || dot(ns[0], ns[1]) <= thr) out.push_back({pos[k.first], pos[k.second]});
    return out;
}

void BrickApp::drawDevOverlay() {
    if (!m_dev) return;
    View v = view();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float h = 4 * m_ui;   // 8 px points, not attenuated
    int drawn = 0;
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        const Object& o = m_scene.objects[m_firstBrickObject + i];
        if (!o.visible) continue;
        Affine xf = o.transform();
        const int part = m_bricks[i].part;
        const Part& p = m_lib.parts[part];
        if (m_devShowEdges && p.hasLdrawMesh && m_devOverrides.count(devId(part)) && m_devUseOverrides) {
            for (const auto& e : devEdges(part)) {
                float ax, ay, bx, by;
                if (v.project(xf.point(e.first), ax, ay) && v.project(xf.point(e.second), bx, by)) dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), IM_COL32(0x7f, 0xe3, 0xff, 140), 1.0f);
            }
        }
        if (!m_devShowVox) continue;
        for (size_t k = 0; k < p.voxels.size() && drawn < 40000; ++k) {
            float sx, sy;
            if (!v.project(xf.point(p.voxels[k]), sx, sy)) continue;
            const uint8_t c = k < p.conn.size() ? p.conn[k] : 0;
            dl->AddRectFilled(ImVec2(sx - h, sy - h), ImVec2(sx + h, sy + h), c < CONN_COUNT ? kDevTypeCol[c] : IM_COL32(255, 255, 255, 242));
            ++drawn;
        }
    }
}

} // namespace brickit
