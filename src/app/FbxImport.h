// FBX import (binary FBX 7.x, the format Maya, Blender and most tools write): meshes, skeleton (LimbNode models)
// and skin clusters. All the meshes are merged into one, baked in the FBX world at their bind pose (the skin clusters'
// Transform, else the model's global transform). Joints keep the scene pose (Lcl transforms, PreRotation included);
// the clusters give the bind matrices (TransformLink). Arrays may be zlib-compressed (inflate included).
// UVs follow MeshFile.h: Vertex::uv v down (engine), UvSet v up.
#pragma once
#include "MeshFile.h"
#include <string>
#include <vector>

namespace app {

struct FbxJoint {
    std::string name;
    int parent = -1;           // index in FbxScene::joints (parents come first)
    float3 translation;        // local to the parent joint (a root: the FBX world)
    float4 rotation{0, 0, 0, 1};
    float3 scale{1, 1, 1};
};

struct FbxCluster {
    int joint = -1;            // FbxScene::joints index
    std::vector<uint32_t> vertices;   // vertices of FbxScene::mesh
    std::vector<double> weights;
    Affine bindJoint;          // world transform of the joint at bind time (TransformLink)
    Affine bindMesh;           // world transform of the mesh at bind time (Transform), for information
};

// An animation stack, sampled per frame: the local transform of every joint (as FbxJoint) at each frame.
struct FbxClip {
    std::string name;
    float fps = 30.0f;
    int frames = 0;
    std::vector<std::vector<float3>> translation, scale;   // [joint][frame]
    std::vector<std::vector<float4>> rotation;
};

struct FbxScene {
    MeshData mesh;
    std::vector<UvSet> uvSets;
    std::vector<FbxJoint> joints;
    std::vector<FbxCluster> clusters;   // one per joint with weights (all the meshes merged)
    std::vector<FbxClip> clips;
    int meshes = 0, version = 0;
};

bool loadFbx(const std::wstring& path, FbxScene& out, std::string* error);

// raw zlib stream (2-byte header, deflate blocks, adler-32 ignored) -> bytes
bool zlibInflate(const uint8_t* src, size_t size, std::vector<uint8_t>& out, size_t expected);

} // namespace app
