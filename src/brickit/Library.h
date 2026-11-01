// BrickIt - part library: the 50 base parts (assets/brickit/base.json + base.bin) and the 730 LDraw Technic parts
// (assets/brickit/technic: index.json + zlib packs g{n}.wasm, geometry loaded per pack on demand).
// Units: cm (1 LDU = 0.04 cm), LDraw parts turned 180 degrees around X, origin at the minimum corner.
#pragma once
#include "vp3d/MeshGen.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace brickit {

using vp3d::float3;

// Connector type of a voxel (snap): studs go into anti-studs, pins into round holes, axles into cross or round holes.
enum Conn : uint8_t { CONN_STUD, CONN_ANTISTUD, CONN_PIN, CONN_PINHOLE, CONN_AXLE, CONN_AXLEHOLE, CONN_COUNT };

struct Color {
    int id = 0;               // LDConfig code
    std::string name;
    float3 srgb;              // 0..1, sRGB encoded
    std::string group;        // standard, transparent, metal, ...
};

struct Part {
    std::string name;         // LDraw reference (file name without .dat)
    std::string label;        // English title
    std::string category;     // LDraw category (Brick, Plate, Technic ...)
    std::string sub;          // Technic sub-family (Poutres, Axes ...), empty for the base set
    bool base = false;        // one of the 50 base parts
    int pack = -1;            // Technic pack holding the geometry (-1: base part)
    int heightPlates = 3;
    float topY = 0.96f;
    float grab[2] = {0, 0};   // default grab column (x, z)
    std::vector<float3> voxels;
    std::vector<uint8_t> conn;            // Conn per voxel
    std::vector<std::pair<float, float>> columns;
    // geometry (empty until its pack is loaded)
    vp3d::MeshData mesh;
    bool loaded = false;
    float3 bmin, bmax;
};

class Library {
public:
    // dir: folder holding base.json, base.bin and technic/. Returns false with a message when the base set is missing.
    bool load(const std::wstring& dir, std::string* error = nullptr);
    // Loads the geometry of a Technic pack (all its parts). Returns false when the file is missing or broken.
    bool loadPack(int pack, std::string* error = nullptr);
    bool loadAll(std::string* error = nullptr);    // every pack (benchmarks, the part grid)
    // Part geometry, loading its pack when needed (nullptr when unknown or not loadable).
    Part* get(const std::string& name);
    int find(const std::string& name) const;

    std::vector<Part> parts;
    std::vector<Color> palette;
    int packCount = 0;

private:
    std::wstring m_dir;
    std::unordered_map<std::string, int> m_byName;
    std::vector<bool> m_packLoaded;
};

// sRGB to linear (one channel).
float srgbToLinear(float c);

} // namespace brickit
