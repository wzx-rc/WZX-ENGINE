// Prefab asset: parts (mesh + material + transform, in a hierarchy) and animation clips (keyframed transform
// tracks per part), saved as readable JSON (.prefab). Like the .wzx scene, geometry is never stored: a part is a
// parametric primitive or the path of a mesh file (OBJ), textures are paths, all relative to the project folder.
// "Prefab" is a working name: the UI reads kPrefabNoun, the file kPrefabExt / kPrefabFormat.
#pragma once
#include "Json.h"
#include "vp3d/Scene.h"
#include <string>
#include <vector>

namespace app {

using namespace vp3d;

constexpr const char* kPrefabNoun = "Prefab";          // UI name of the asset (working name)
constexpr const wchar_t* kPrefabExt = L".prefab";      // file extension
constexpr const char* kPrefabFormat = "wzx-prefab";    // "format" key of the file

// Animated channels of a part (local transform). Rotation is in degrees (Euler Y, X, Z order, as the inspector).
enum PrefabChannel { CH_POSITION = 0, CH_ROTATION, CH_SCALE, CH_COUNT };
enum PrefabInterp { INTERP_LINEAR = 0, INTERP_STEP, INTERP_SMOOTH };
const char* channelName(int channel);
const char* interpName(int interp);

struct PrefabKey {
    float time = 0;            // seconds
    float3 value;
    int interp = INTERP_SMOOTH;   // towards the next key
};

struct PrefabTrack {
    uint32_t part = 0;         // PrefabPart::id
    int channel = CH_POSITION;
    std::vector<PrefabKey> keys;   // sorted by time
    float3 sample(float t) const;
    int keyAt(float t, float eps) const;   // index of the key at time t (within eps), -1 when none
    void setKey(float t, const float3& v, int interp, float eps);
    void sortKeys();
};

struct PrefabClip {
    std::string name = "Idle";
    float length = 2.0f;       // seconds
    float fps = 30.0f;         // frame grid of the timeline (keys snap to it)
    bool loop = true;
    std::vector<PrefabTrack> tracks;
    PrefabTrack* track(uint32_t part, int channel, bool create);
    const PrefabTrack* track(uint32_t part, int channel) const;
};

struct PrefabPart {
    uint32_t id = 0;           // stable id, unique in the prefab
    uint32_t parent = 0;       // parent part id (0 = root); the transform is local to the parent
    std::string name = "Part";
    // geometry: a mesh file (meshPath) or a parametric primitive (prim.type); neither = an empty pivot (group)
    std::string meshPath;
    bool recenter = true;      // mesh file: pivot moved to the centre of its base
    PrimitiveParams prim;
    // material (vp3d material + its base colour image path)
    Material material;
    std::string texture;
    // local transform
    float3 position{0, 0, 0};
    float3 rotation{0, 0, 0};  // degrees, Euler YXZ
    float3 scale{1, 1, 1};
    bool visible = true;
    bool collapsed = false;    // Parts list: children folded
    bool isPivot() const { return meshPath.empty() && prim.type == PRIM_NONE; }
};

// RTS data of the asset: what the game needs to place and select it.
struct PrefabGameData {
    std::string category = "Building";   // Building, Unit, Prop, Resource
    int footprintX = 2, footprintZ = 2;  // cells
    float selectionRadius = 1.2f;        // scene units (1 unit = 1 cell)
    float height = 2.0f;                 // health bar / selection box height
    bool blocksPath = true;
};

struct PrefabAsset {
    std::string name = "New Prefab";
    std::vector<PrefabPart> parts;
    std::vector<PrefabClip> clips;
    PrefabGameData game;
    uint32_t nextId = 1;

    int findPart(uint32_t id) const;
    uint32_t addPart(const PrefabPart& p);   // assigns an id, returns it
    void removePart(uint32_t id);            // with its children and its tracks
    bool isAncestor(uint32_t ancestor, uint32_t id) const;
    std::vector<int> childrenOf(uint32_t parent) const;   // part indices, in list order
    int depthOf(uint32_t id) const;
};

// Pose of the parts at a time of a clip (local transforms; the parts without tracks keep their rest transform).
struct PartPose { float3 position, rotation, scale; };
void evaluatePose(const PrefabAsset& a, const PrefabClip* clip, float time, std::vector<PartPose>& out);
// World transforms of the parts (parents first): position, orientation quaternion, scale.
struct PartWorld { float3 position; float4 rot; float3 scale; };
void worldTransforms(const PrefabAsset& a, const std::vector<PartPose>& pose, std::vector<PartWorld>& out);
float4 eulerToQuat(const float3& deg);   // YXZ, as the inspector
float3 quatToEuler(const float4& q);

// File: readable JSON, fixed key order. pathOf / fromPath convert the stored relative paths.
std::string prefabToJson(const PrefabAsset& a);
bool prefabFromJson(const Json& j, PrefabAsset& a, std::string* error);

// The sample asset of a new studio: a watchtower made of primitives (base, posts, platform, roof, flag) with an
// "Idle" clip (flag waving, beacon turning) and a "Build" clip (parts rising into place).
PrefabAsset makeDemoPrefab();

} // namespace app
