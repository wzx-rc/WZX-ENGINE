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
    std::string family;       // LDraw family of the base set (Brique, Technic ...)
    std::string ldraw;        // LDraw file name without .dat (info.ldraw)
    float3 ldrawOrigin;       // LDraw origin in the part frame (info.ldrawOrigin, export .ldr)
    bool hasOrigin = false;
    // info of the part sheet in DEV mode (title, author, licence, release, history, keywords, sizes, mesh, connectors)
    std::string title, author, license, release, keywords;
    int history = 0, triangles = 0, vertices = 0;
    float sizeCm[3] = {0, 0, 0}, sizeStuds[2] = {0, 0};
    bool hasSizeCm = false, hasSizeStuds = false;
    std::vector<std::pair<std::string, int>> connectors;
    bool base = false;        // one of the 50 base parts
    int pack = -1;            // Technic pack holding the geometry (-1: base part)
    int heightPlates = 3;
    float topY = 0.96f;
    bool hasStuds = false;    // studs on top (above topY): left out of the body box (overlap safety, stacking)
    float grab[2] = {0, 0};   // default grab column (x, z)
    std::vector<float3> voxels;
    std::vector<uint8_t> conn;            // Conn per voxel
    std::vector<std::pair<float, float>> columns;
    // geometry (empty until its pack is loaded)
    vp3d::MeshData mesh;
    bool loaded = false;
    bool available() const { return loaded || pack >= 0; }   // geometry in memory or in a pack (loaded on demand)
    vp3d::MeshData ldrawMesh;  // DEV mode: the LDraw mesh while a replacement is shown
    bool hasLdrawMesh = false, devActive = false;
    float3 bmin, bmax;
};

class Library {
public:
    // dir: folder holding base.json, base.bin and technic/. Returns false with a message when the base set is missing.
    bool load(const std::wstring& dir, std::string* error = nullptr);
    // Loads the geometry of a Technic pack (all its parts). Returns false when the file is missing or broken.
    bool loadPack(int pack, std::string* error = nullptr);
    bool loadAll(std::string* error = nullptr);    // every Technic pack (benchmarks, the part grid; not the full library)
    void unloadPack(int pack);                      // frees the geometry of a pack (thumbnails of the full library)
    int technicPacks = 0;
    std::wstring fullDir;                           // folder of the full library (empty: not built)                           // packs [0, technicPacks): Technic; then the full LDraw library
    // Part geometry, loading its pack when needed (nullptr when unknown or not loadable).
    Part* get(const std::string& name);
    int find(const std::string& name) const;

    std::vector<Part> parts;
    std::vector<Color> palette;
    // colours that exist for real per part (assets/brickit/part_colors.txt, Rebrickable data, LDraw colour codes):
    // true when the palette colour exists for the part, or when the part is not in the table
    bool colorExists(int part, int paletteIndex) const;
    bool hasColorTable(int part) const;
    int packCount = 0;

private:
    std::wstring m_dir;
    std::unordered_map<std::string, int> m_byName;
    std::unordered_map<std::string, std::vector<int>> m_partColors;
    std::unordered_map<std::string, std::string> m_alias;   // LDraw alias / moved part -> target (full\\aliases.txt)   // LDraw part name -> LDraw colour codes (sorted)
    const std::vector<int>* colorsOf(int part) const;
    std::vector<bool> m_packLoaded;
    std::vector<std::wstring> m_packFile;   // file of each pack, relative to m_dir
};

// Folder of the full LDraw library: <assets>\\full, else %LOCALAPPDATA%\\BrickIt\\full (empty when neither exists).
std::wstring fullLibraryDir(const std::wstring& assets);

// sRGB to linear (one channel).
float srgbToLinear(float c);

} // namespace brickit
