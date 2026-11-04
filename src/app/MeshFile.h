// Mesh files for the editor: Wavefront OBJ (one UV set) and glTF 2.0 (.gltf / .glb, static meshes, every
// TEXCOORD_n set). Vertex::uv keeps the engine convention (v down, row 0 of the image on top); the UV sets
// (UvSet) use the usual UV-editor convention, v up: uv = (u, 1 - v).
#pragma once
#include "vp3d/Scene.h"
#include <string>
#include <vector>

namespace app {

using namespace vp3d;

struct UvSet {
    std::string name;            // glTF: mesh extras "uvSetNames" when present, else UV0, UV1...
    std::vector<float2> uv;      // one per vertex of the mesh
};

// glTF: the default scene with its node transforms (or every mesh), all triangle primitives merged into one mesh.
// Set 0 is also written in the vertices (Vertex::uv). Missing normals are computed (smooth).
bool loadGltf(const std::wstring& path, MeshData& out, std::vector<UvSet>* sets, std::string* error);

// By extension: .gltf / .glb, else OBJ. sets (optional) receives every UV set (OBJ: one, from the vertices).
bool loadMeshFile(const std::wstring& path, MeshData& out, std::vector<UvSet>* sets, std::string* error);
bool isMeshFile(const std::wstring& path);   // .obj, .gltf, .glb

} // namespace app
