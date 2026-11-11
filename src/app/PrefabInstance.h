// Live prefab instances, shared by the editor and the players (no editor dependency).
//
// An instance is one scene object, its root (Object::prefab = the .prefab file relative to the project, mesh -1, part
// "PREFAB"), carrying the transform of the whole instance (position, quaternion, scale) and its animation (Object::clip,
// clipTime, clipSpeed, clipLoop, clipPlaying). The parts of the prefab are generated objects (Object::instanceRoot =
// the root's id, instancePart = the part id): created from the file, posed from the root and the clip every frame,
// never saved (the .wzx keeps the root only). Moving, turning, hiding or destroying the root acts on the whole
// instance; a part keeps its own material (a colour change of one instance does not touch the others).
//
// Each frame (editor and player): syncPrefabInstances (only when objects were added, removed or a file changed), then
// updatePrefabInstances (clips and poses). In Play, object indices are graph handles: nothing is erased then (orphan
// parts are hidden), new objects are only appended.
#pragma once
#include "Prefab.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app {

constexpr const char* kPrefabPart = "PREFAB";   // Object::part of an instance root

class PrefabLibrary {
public:
    std::function<std::wstring(const std::string&)> absPath;                   // project-relative path -> file
    std::function<bool(const std::wstring&, MeshData&, std::string*)> loadMesh;   // empty: OBJ files (loadObj)
    // The asset of a .prefab file (path as written in the scene), read once; nullptr when missing or invalid.
    const PrefabAsset* get(const std::string& path);
    // Reads again the files changed on disk since they were read: true when one did (the instances rebuild).
    bool refresh();
    uint32_t version() const { return m_version; }   // bumps at each (re)load: the instances compare it
    // The file of a prefab given by its name ("MechaWorker": searched under game/prefabs, file name or prefab name
    // without spaces) or by its path (kept as is). Empty when there is none.
    std::string resolve(const std::string& nameOrPath);
    std::vector<std::string> warnings;
    void clear() { m_entries.clear(); ++m_version; }

private:
    struct Entry {
        std::shared_ptr<PrefabAsset> asset;
        long long stamp = 0;   // last write time of the file
        uint32_t version = 0;
    };
    std::map<std::string, Entry> m_entries;
    std::map<std::string, std::string> m_resolved;
    uint32_t m_version = 1;
    bool load(const std::string& path, Entry& e);
public:
    uint32_t versionOf(const std::string& path) const;
};

// Root of the instance an object belongs to (its own index when it is not a part); -1 for -1.
int instanceRootOf(const Scene& sc, int obj);

// A new instance root at pos (yaw in degrees), playing "Idle" when the prefab has it. Its parts come with the next
// syncPrefabInstances. Returns the root index (-1: the prefab cannot be read).
int addPrefabInstance(Scene& sc, PrefabLibrary& lib, const std::string& path, float3 pos, float yawDeg,
                      const std::string& name = std::string());

// Makes the parts of every instance match its prefab: missing parts are created, the parts of a changed prefab
// rebuilt, duplicates and parts of a removed root erased (allowErase) or hidden (Play). True when objects changed.
bool syncPrefabInstances(Scene& sc, PrefabLibrary& lib, bool allowErase);

// Advances the clips by dt when `advance` (Play), then poses every part from its root: world = root * pose(part).
// finished: (root index, clip name) of the non-looping clips that reached their end this frame.
void updatePrefabInstances(Scene& sc, PrefabLibrary& lib, float dt, bool advance,
                           std::vector<std::pair<int, std::string>>* finished = nullptr);

// Clip control of an instance (root index): plays `clip` from its start (empty: the rest pose), or stops it.
bool playClip(Scene& sc, PrefabLibrary& lib, int root, const std::string& clip, bool loop, float speed);
void stopClip(Scene& sc, int root);

// Unpack: the parts become ordinary objects (children of the root, now a plain group), the instance link is gone.
bool unpackPrefabInstance(Scene& sc, int root);

// The files an instance needs besides its .prefab (meshes and textures of its parts), as written in the prefab.
std::vector<std::string> prefabFiles(const PrefabAsset& a);

} // namespace app
