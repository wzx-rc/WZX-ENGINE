// vp3d - asset import: Wavefront OBJ meshes (+ MTL diffuse map) and images (WIC: jpg, png, bmp...).
#pragma once
#include "Scene.h"
#include <string>

namespace vp3d {

struct ObjInfo {
    std::string diffuseMap;      // map_Kd of the first material, as written in the .mtl (may be missing on disk)
    size_t positions = 0, uvs = 0, normals = 0, faces = 0;
    bool hadNormals = false;     // false: normals computed (smoothing angle)
};

// Loads every face of the OBJ into one mesh (polygons are fanned, negative indices supported).
// Without normals in the file, they are computed with a smoothing angle (faces further apart stay hard).
bool loadObj(const std::wstring& path, MeshData& out, ObjInfo* info = nullptr, std::string* error = nullptr,
             float smoothAngleDeg = 60.0f);

// Decodes an image file to RGBA8 (the caller must have initialised COM). maxSize > 0 downsizes larger images.
bool loadImage(const std::wstring& path, Texture& out, std::string* error = nullptr, uint32_t maxSize = 0);

} // namespace vp3d
