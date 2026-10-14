// Demo app content: colour palette, material presets, primitive library
// and the test scenes (Cornell Box and an empty studio).
#pragma once
#include "vp3d/Scene.h"
#include <string>
#include <vector>

namespace app {

using namespace vp3d;

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

enum class ColorGroup { Standard, Transparent, Metallic };

struct PaletteColor {
    const wchar_t* name;
    int id;
    int r, g, b;
    ColorGroup group;
    float3 linear() const { return rgb255(r, g, b); }
};
const std::vector<PaletteColor>& palette();
int findColor(int id);

enum Preset {
    PRESET_PLASTIC = 0,
    PRESET_TRANSPARENT,
    PRESET_CHROME,
    PRESET_BRUSHED,
    PRESET_RUBBER,
    PRESET_WAX,
    PRESET_LACQUER,
    PRESET_MATTE,
    PRESET_EMISSIVE,
    PRESET_PEARL,
    PRESET_COUNT
};
const std::vector<std::wstring>& presetNames();
// Applies a preset to a material while keeping its base colour.
void applyPreset(Material& m, int preset, float unitsPerCm);

enum PartKind { K_SHADERBALL, K_SPHERE, K_CUBE };

struct PartDef {
    const char* id;          // reference shown in the UI
    const wchar_t* name;
    PartKind kind;
    int defaultColor;        // palette colour id
};
const std::vector<PartDef>& parts();
const PartDef* findPart(const std::string& id);
MeshData makePartMesh(const PartDef& p);
// Returns the mesh index for a part, creating it on demand.
int partMesh(Scene& s, const PartDef& p);

// Creates an object of the given part with its own material.
int addPart(Scene& s, const std::string& partId, int colorId, int preset, float3 pos, float yawDeg);

// Parametric primitives (Create > Mesh): cube, sphere, plane, cylinder, 1 RTS cell large, neutral matte grey.
const char* primitiveName(int type);
int addPrimitive(Scene& s, int type, float3 pos);
// Points the object at a mesh built from its parameters: rewritten in place when no other object uses it, else a new
// one. Returns true when the mesh changed. Call after editing Object::prim, and after undo / redo.
bool syncPrimitiveMesh(Scene& s, int object);

void buildCornellBox(Scene& s);
// Ground grid + soft studio lights, no objects.
void buildStudioScene(Scene& s);
// Neutral floor and five reference spheres (clay, chrome, glass, emissive, candle wax) lit by an HDRI.
void buildLookDevScene(Scene& s);
// HDRI rotation of the LookDev scene: the sun of the image goes behind the camera, on its right, so that the spheres
// are lit from the front (0 when the image has no clear sun).
float lookDevHdriRotation(const EnvImage& img);

struct ViewPreset {
    std::wstring name;
    Camera cam;
};
std::vector<ViewPreset> defaultViews(const Scene& s, bool cornell);

} // namespace app
