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
    uint32_t part = 0;         // PrefabPart::id, or PrefabJoint::id when joint is set
    int channel = CH_POSITION;
    bool joint = false;        // a track of a joint of the skeleton (Rig module)
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
    PrefabTrack* jointTrack(uint32_t joint, int channel, bool create);
    const PrefabTrack* jointTrack(uint32_t joint, int channel) const;
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

// Rig module: a joint of the skeleton. Its rest transform (the bind pose) is local to its parent joint; root joints
// are in the space of the prefab. Clips animate joints like parts (PrefabTrack::joint).
struct PrefabJoint {
    uint32_t id = 0;           // stable id, unique among the joints
    uint32_t parent = 0;       // parent joint id (0 = root)
    std::string name = "joint";
    float3 position{0, 0, 0};  // rest (bind) local transform
    float3 rotation{0, 0, 0};  // degrees, Euler YXZ
    float3 scale{1, 1, 1};
    float radius = 0.06f;      // display size (scene units)
    bool collapsed = false;
};

// Rig module: skinning of a part by joints (linear blend skinning, done on the CPU). Weights are dense: one row per
// vertex of the part's mesh (as built: primitive or file), one column per influence. bindJoint / bindMesh are the
// world matrices (3 x 4, rows) of the influences and of the mesh at bind time: deformed = sum w * J(t) * bindJoint^-1 *
// bindMesh * v. A mesh whose vertex count changes (other primitive parameters) drops its weights (rebind).
struct PrefabSkin {
    uint32_t part = 0;
    std::vector<uint32_t> joints;      // influences (joint ids)
    std::vector<uint8_t> locked;       // per influence: weight locked for painting
    std::vector<Affine> bindJoint;     // per influence
    Affine bindMesh;
    uint32_t vertexCount = 0;
    std::vector<float> weights;        // vertexCount x joints.size()
    int maxInfluences = 4;
    bool dualQuaternion = false;       // dual quaternion skinning (keeps the volume at twisted joints) instead of linear
    float& w(size_t v, size_t j) { return weights[v * joints.size() + j]; }
    float w(size_t v, size_t j) const { return weights[v * joints.size() + j]; }
    int influence(uint32_t joint) const;   // column of a joint, -1 when it is not an influence
};

struct PrefabAsset {
    std::string name = "New Prefab";
    std::vector<PrefabPart> parts;
    std::vector<PrefabClip> clips;
    PrefabGameData game;
    uint32_t nextId = 1;
    // Rig module
    std::vector<PrefabJoint> joints;
    std::vector<PrefabSkin> skins;
    uint32_t nextJointId = 1;

    int findJoint(uint32_t id) const;
    uint32_t addJoint(const PrefabJoint& j);
    void removeJoint(uint32_t id);           // with its children, their tracks and their influences (weights renormalised)
    std::vector<int> jointChildren(uint32_t parent) const;
    bool jointAncestor(uint32_t ancestor, uint32_t id) const;
    PrefabSkin* skinOf(uint32_t part);
    const PrefabSkin* skinOf(uint32_t part) const;

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
// Joints: local pose at a time of a clip (rest + tracks), then world transforms (parents first).
void evaluateJoints(const PrefabAsset& a, const PrefabClip* clip, float time, std::vector<PartPose>& out);
void jointWorld(const PrefabAsset& a, const std::vector<PartPose>& pose, std::vector<PartWorld>& out);
Affine toAffine(const PartWorld& w);
Affine mul(const Affine& a, const Affine& b);
float4 eulerToQuat(const float3& deg);   // YXZ, as the inspector
float3 quatToEuler(const float4& q);

// File: readable JSON, fixed key order. pathOf / fromPath convert the stored relative paths.
std::string prefabToJson(const PrefabAsset& a);
bool prefabFromJson(const Json& j, PrefabAsset& a, std::string* error);

// The sample asset of a new studio: a watchtower made of primitives (base, posts, platform, roof, flag) with an
// "Idle" clip (flag waving, beacon turning) and a "Build" clip (parts rising into place).
PrefabAsset makeDemoPrefab();
// Rig module sample: a standing cylinder (height 2, 16 cuts) with three joints (root, mid, tip), smooth skin weights
// and a "Bend" clip.
PrefabAsset makeRigSample();

} // namespace app
