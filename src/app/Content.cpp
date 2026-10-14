#include "Content.h"
#ifdef _WIN32
#include <windows.h>
#else
#include "vp3d/FilePath.h"
#endif
#include "vp3d/shaders/Shared.h"

namespace app {

#ifdef _WIN32
std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
#else
std::string toUtf8(const std::wstring& w) { return nativePath(w); }

std::wstring fromUtf8(const std::string& s) {
    std::wstring w;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | ((uint8_t)s[i] & 0x3F);
        w.push_back((wchar_t)cp);
    }
    return w;
}
#endif

// --------------------------------------------------------------------------- colours

const std::vector<PaletteColor>& palette() {
    static const std::vector<PaletteColor> p = {
        // Standards
        {L"White", 1, 244, 244, 242, ColorGroup::Standard},
        {L"Light Bluish Gray", 194, 163, 162, 164, ColorGroup::Standard},
        {L"Dark Bluish Gray", 199, 99, 95, 97, ColorGroup::Standard},
        {L"Dark Stone Gray", 199 + 1000, 72, 74, 78, ColorGroup::Standard},
        {L"Black", 26, 27, 30, 34, ColorGroup::Standard},
        {L"Bright Red", 21, 201, 26, 9, ColorGroup::Standard},
        {L"Dark Red", 154, 123, 30, 34, ColorGroup::Standard},
        {L"Coral", 353, 255, 105, 105, ColorGroup::Standard},
        {L"Bright Purple", 221, 205, 98, 152, ColorGroup::Standard},
        {L"Light Purple", 222, 228, 173, 200, ColorGroup::Standard},

        {L"Bright Orange", 106, 254, 138, 24, ColorGroup::Standard},
        {L"Flame Yellowish Orange", 191, 248, 172, 30, ColorGroup::Standard},
        {L"Bright Yellow", 24, 255, 205, 0, ColorGroup::Standard},
        {L"Cool Yellow", 226, 255, 236, 108, ColorGroup::Standard},
        {L"Brick Yellow", 5, 215, 197, 153, ColorGroup::Standard},
        {L"Light Nougat", 283, 246, 215, 179, ColorGroup::Standard},
        {L"Nougat", 18, 204, 142, 105, ColorGroup::Standard},
        {L"Medium Nougat", 312, 170, 125, 85, ColorGroup::Standard},
        {L"Reddish Brown", 192, 105, 64, 40, ColorGroup::Standard},
        {L"Dark Brown", 308, 53, 33, 0, ColorGroup::Standard},

        {L"Sand Yellow", 138, 149, 138, 115, ColorGroup::Standard},
        {L"Olive Green", 330, 155, 154, 90, ColorGroup::Standard},
        {L"Bright Yellowish Green", 119, 165, 202, 24, ColorGroup::Standard},
        {L"Spring Yellowish Green", 326, 226, 249, 154, ColorGroup::Standard},
        {L"Bright Green", 37, 0, 150, 60, ColorGroup::Standard},
        {L"Dark Green", 141, 0, 69, 26, ColorGroup::Standard},
        {L"Sand Green", 151, 120, 144, 129, ColorGroup::Standard},
        {L"Bright Bluish Green", 107, 0, 143, 155, ColorGroup::Standard},
        {L"Medium Azur", 322, 54, 174, 191, ColorGroup::Standard},
        {L"Aqua", 323, 211, 242, 234, ColorGroup::Standard},

        {L"Bright Blue", 23, 0, 85, 191, ColorGroup::Standard},
        {L"Medium Blue", 102, 90, 147, 219, ColorGroup::Standard},
        {L"Dark Azur", 321, 70, 155, 195, ColorGroup::Standard},
        {L"Light Royal Blue", 212, 159, 195, 233, ColorGroup::Standard},
        {L"Sand Blue", 135, 112, 129, 154, ColorGroup::Standard},
        {L"Earth Blue", 140, 25, 50, 90, ColorGroup::Standard},
        {L"Medium Lilac", 268, 72, 47, 141, ColorGroup::Standard},
        {L"Medium Lavender", 324, 160, 110, 185, ColorGroup::Standard},
        {L"Bright Reddish Violet", 124, 144, 31, 118, ColorGroup::Standard},
        {L"Lavender", 325, 205, 164, 222, ColorGroup::Standard},

        // Transparents
        {L"Transparent", 40, 236, 240, 240, ColorGroup::Transparent},
        {L"Tr. Brown", 111, 140, 132, 118, ColorGroup::Transparent},
        {L"Tr. Red", 41, 220, 30, 20, ColorGroup::Transparent},
        {L"Tr. Bright Orange", 182, 250, 140, 30, ColorGroup::Transparent},
        {L"Tr. Yellow", 44, 250, 225, 60, ColorGroup::Transparent},
        {L"Tr. Green", 48, 60, 190, 80, ColorGroup::Transparent},
        {L"Tr. Light Blue", 43, 140, 225, 240, ColorGroup::Transparent},
        {L"Tr. Blue", 42, 40, 110, 230, ColorGroup::Transparent},
        {L"Tr. Purple", 113, 150, 90, 210, ColorGroup::Transparent},
        {L"Tr. Pink", 230, 240, 120, 200, ColorGroup::Transparent},

        // Metallics
        {L"Silver Metallic", 315, 200, 202, 205, ColorGroup::Metallic},
        {L"Warm Gold", 297, 226, 186, 110, ColorGroup::Metallic},
        {L"Gold", 334, 225, 165, 50, ColorGroup::Metallic},
        {L"Titanium", 316, 120, 120, 125, ColorGroup::Metallic},
        {L"Copper", 139, 220, 140, 100, ColorGroup::Metallic},
        {L"Bronze", 335, 140, 90, 50, ColorGroup::Metallic},
    };
    return p;
}

int findColor(int id) {
    const auto& p = palette();
    for (size_t i = 0; i < p.size(); ++i)
        if (p[i].id == id) return (int)i;
    return 0;
}

// --------------------------------------------------------------------------- material presets

const std::vector<std::wstring>& presetNames() {
    static const std::vector<std::wstring> n = {
        L"Plastic (standard)", L"Clear Plastic", L"Chrome Metal", L"Brushed Metal",
        L"Rubber", L"Wax / Subsurface", L"Lacquered Plastic", L"Matte Paint", L"Emissive", L"Pearl Metal"};
    return n;
}

void applyPreset(Material& m, int preset, float u) {
    float3 base = m.baseColor;
    float bevel = m.bevelRadius;
    uint32_t flags = m.flags;
    std::string name = m.name;
    int colorId = m.colorId;
    m = Material();
    m.name = name;
    m.colorId = colorId;
    m.baseColor = base;
    m.bevelRadius = bevel;
    m.flags = flags;
    m.preset = preset;
    switch (preset) {
    case PRESET_PLASTIC:
        m.roughness = 0.24f; m.specular = 0.5f;
        m.subsurface = 0.25f; m.sssRadius = float3(0.12f, 0.06f, 0.04f) * u;
        break;
    case PRESET_TRANSPARENT: m.transmission = 1.0f; m.roughness = 0.04f; m.ior = 1.49f; break;
    case PRESET_CHROME: m.metallic = 1.0f; m.roughness = 0.08f; break;
    case PRESET_BRUSHED: m.metallic = 1.0f; m.roughness = 0.32f; break;
    case PRESET_RUBBER: m.roughness = 0.72f; m.specular = 0.3f; break;
    case PRESET_WAX:
        m.roughness = 0.35f; m.subsurface = 1.0f; m.sssRadius = float3(1.2f, 0.6f, 0.35f) * u;
        break;
    case PRESET_LACQUER: m.roughness = 0.3f; m.coat = 1.0f; m.coatRoughness = 0.03f; break;
    case PRESET_MATTE: m.roughness = 0.95f; m.specular = 0.25f; break;
    case PRESET_EMISSIVE: m.roughness = 0.5f; m.emission = base; m.emissionStrength = 6.0f; break;
    case PRESET_PEARL: m.metallic = 1.0f; m.roughness = 0.22f; m.coat = 0.6f; m.coatRoughness = 0.06f; break;
    }
}

// --------------------------------------------------------------------------- primitives

const std::vector<PartDef>& parts() {
    static const std::vector<PartDef> p = {
        {"BALL", L"Shader ball", K_SHADERBALL, 1},
        {"SPHERE", L"Sphere", K_SPHERE, 21},
        {"CUBE", L"Cube", K_CUBE, 1},
    };
    return p;
}

const PartDef* findPart(const std::string& id) {
    for (const PartDef& p : parts())
        if (id == p.id) return &p;
    return nullptr;
}

MeshData makePartMesh(const PartDef& p) {
    switch (p.kind) {
    case K_SHADERBALL: return makeShaderBall(3.0f);
    case K_SPHERE: return makeSphere(3.0f, 96, 48);
    case K_CUBE: return makeBox({-3, 0, -3}, {3, 6, 3});
    }
    return MeshData();
}

int partMesh(Scene& s, const PartDef& p) {
    int m = s.findMesh(p.id);
    if (m >= 0) return m;
    return s.addMesh(p.id, makePartMesh(p));
}

static int defaultPresetFor(int colorId) {
    const PaletteColor& c = palette()[findColor(colorId)];
    if (c.group == ColorGroup::Transparent) return PRESET_TRANSPARENT;
    if (c.group == ColorGroup::Metallic) return PRESET_PEARL;
    return PRESET_PLASTIC;
}

int addPart(Scene& s, const std::string& partId, int colorId, int preset, float3 pos, float yawDeg) {
    const PartDef* p = findPart(partId);
    if (!p) return -1;
    const PaletteColor& c = palette()[findColor(colorId)];
    Material m;
    m.name = std::string(partId);
    m.baseColor = c.linear();
    m.colorId = c.id;
    applyPreset(m, preset < 0 ? defaultPresetFor(colorId) : preset, 1.0f);
    Object o;
    o.part = partId;
    o.name = toUtf8(p->name);
    o.mesh = partMesh(s, *p);
    o.material = s.addMaterial(m);
    o.position = pos;
    o.yaw = radians(yawDeg);
    return s.addObject(o);
}

// --------------------------------------------------------------------------- scenes

void buildCornellBox(Scene& s) {
    s = Scene();
    s.name = "Cornell Box";
    s.unitsPerMeter = 100.0f;
    s.gridScale = 5.0f;
    s.env.top = s.env.horizon = s.env.bottom = float3(0, 0, 0);
    s.env.intensity = 0.0f;

    auto mat = [&](const char* n, float3 c, float rough, int preset = PRESET_MATTE) {
        Material m;
        m.name = n;
        m.baseColor = c;
        applyPreset(m, preset, 1.0f);
        m.roughness = rough;
        return s.addMaterial(m);
    };
    int white = mat("White Wall", float3(0.74f, 0.73f, 0.70f), 0.9f);
    int red = mat("Red Wall", float3(0.72f, 0.045f, 0.03f), 0.9f);
    int green = mat("Green Wall", float3(0.10f, 0.52f, 0.025f), 0.9f);

    const float H = 55.5f, W = 27.75f, T = 1.0f;
    auto wall = [&](const char* name, float3 a, float3 b, int m) {
        Object o;
        o.name = name;
        o.part = "WALL";
        o.mesh = s.addMesh(name, makeBox(a, b));
        o.material = m;
        o.selectable = true;
        s.addObject(o);
    };
    wall("Floor", {-W - T, -T, -W - T}, {W + T, 0, W + 80}, white);
    wall("Ceiling", {-W - T, H, -W - T}, {W + T, H + T, W + 80}, white);
    wall("Back Wall", {-W - T, 0, -W - T}, {W + T, H, -W}, white);
    wall("Left Wall", {-W - T, 0, -W}, {-W, H, W + 80}, red);
    wall("Right Wall", {W, 0, -W}, {W + T, H, W + 80}, green);

    Material boxMat;
    boxMat.name = "White Block";
    boxMat.baseColor = float3(0.78f, 0.77f, 0.74f);
    applyPreset(boxMat, PRESET_MATTE, 1.0f);
    boxMat.roughness = 0.8f;
    boxMat.bevelRadius = 0.35f;
    int bm1 = s.addMaterial(boxMat);
    int bm2 = s.addMaterial(boxMat);
    {
        Object o;
        o.name = "Tall Block";
        o.part = "CUBE";
        o.mesh = s.addMesh("tallbox", makeBox({-8.5f, 0, -8.5f}, {8.5f, 30.0f, 8.5f}));
        o.material = bm1;
        o.position = {-9.0f, 0, -6.5f};
        o.yaw = radians(16.0f);
        s.addObject(o);
        o.name = "Short Block";
        o.mesh = s.addMesh("shortbox", makeBox({-8.5f, 0, -8.5f}, {8.5f, 15.0f, 8.5f}));
        o.material = bm2;
        o.position = {10.0f, 0, 6.5f};
        o.yaw = radians(-14.0f);
        s.addObject(o);
    }

    auto ball = [&](const char* name, float3 pos, float yawDeg, float3 color, int preset, float rough = -1) {
        int idx = addPart(s, "BALL", 1, preset, pos, yawDeg);
        Object& o = s.objects[idx];
        o.name = name;
        Material& m = s.materials[o.material];
        m.baseColor = color;
        m.colorId = -1;
        applyPreset(m, preset, 1.0f);
        if (rough >= 0) m.roughness = rough;
        return idx;
    };
    float3 sssWhite(0.86f, 0.83f, 0.80f);
    float3 limeGreen = rgb255(110, 205, 20);
    ball("Ball Wax", {-12.5f, 30.0f, -5.0f}, -30, sssWhite, PRESET_WAX);
    ball("Ball Gold", {-5.0f, 30.0f, -8.5f}, -30, rgb255(255, 190, 90), PRESET_CHROME, 0.18f);
    ball("Ball White Lacquer", {7.0f, 15.0f, 7.0f}, -20, float3(0.82f, 0.82f, 0.80f), PRESET_LACQUER, 0.35f);
    ball("Ball Green Lacquer", {14.0f, 15.0f, 5.0f}, -20, limeGreen, PRESET_LACQUER, 0.25f);
    ball("Ball Wax Floor", {-9.0f, 0.0f, 16.0f}, -10, sssWhite, PRESET_WAX);
    ball("Ball Green Metal", {0.0f, 0.0f, 17.0f}, -10, rgb255(90, 170, 30), PRESET_BRUSHED, 0.2f);

    AreaLight l;
    l.name = "Ceiling Light";
    l.position = {0, H - 0.05f, -2.0f};
    l.target = {0, 0, -2.0f};
    l.width = 14.0f;
    l.height = 11.0f;
    l.color = float3(1.0f, 0.93f, 0.82f);
    l.intensity = 48.0f;
    l.visibleToCamera = true;
    s.lights.push_back(l);

    Camera& c = s.camera;
    c.target = {0, H * 0.47f, 0};
    c.yaw = 0.0f;
    c.pitch = 0.0f;
    c.distance = 96.0f;
    c.fovY = radians(40.0f);
    c.dof = true;
    c.fStop = 5.6f;
    c.focusDistance = 78.0f;
    c.sensorScale = s.unitsPerMeter;
}

void buildStudioScene(Scene& s) {
    s = Scene();
    s.name = "Studio";
    s.unitsPerMeter = 100.0f;
    s.gridScale = 1.0f;
    s.env.top = float3(0.030f, 0.032f, 0.038f);
    s.env.horizon = float3(0.055f, 0.057f, 0.062f);
    s.env.bottom = float3(0.02f, 0.02f, 0.022f);
    s.env.intensity = 1.0f;
    s.env.ambient = float3(0.10f, 0.10f, 0.11f);

    // Ground with grid
    {
        Material g;
        g.name = "Grid Floor";
        g.baseColor = float3(0.022f, 0.023f, 0.026f);
        applyPreset(g, PRESET_MATTE, 1.0f);
        g.roughness = 0.6f;
        g.flags = MAT_FLAG_GRID;
        Object o;
        o.name = "Floor";
        o.part = "GROUND";
        o.mesh = s.addMesh("ground", makePlane(400.0f));
        o.material = s.addMaterial(g);
        o.selectable = false;
        s.addObject(o);
    }

    // Key: a directional sun (sharp shadows, as in a sunny day); rim and top fill are soft area lights (hidden from camera).
    s.sun.enabled = true;
    s.sun.direction = normalize(float3(-28.0f, 34.0f, 30.0f));   // towards the sun
    s.sun.color = float3(1.0f, 0.95f, 0.88f);
    s.sun.intensity = 4.0f;
    AreaLight rim;
    rim.name = "Rim Light";
    rim.position = {26, 22, -30};
    rim.target = {0, 3, 0};
    rim.width = 18;
    rim.height = 12;
    rim.color = float3(0.75f, 0.85f, 1.0f);
    rim.intensity = 18.0f;
    rim.visibleToCamera = false;
    s.lights.push_back(rim);
    AreaLight top;
    top.name = "Top Fill";
    top.position = {4, 45, 4};
    top.target = {0, 0, 0};
    top.width = 30;
    top.height = 30;
    top.intensity = 4.0f;
    top.visibleToCamera = false;
    s.lights.push_back(top);

    Camera& c = s.camera;
    c.target = {0.4f, 1.2f, 0.4f};
    c.yaw = radians(38.0f);
    c.pitch = radians(32.0f);
    c.distance = 26.0f;
    c.fovY = radians(32.0f);
    c.dof = false;
    c.focusDistance = 26.0f;
    c.sensorScale = s.unitsPerMeter;
}


// Look development: a neutral floor and five spheres with reference materials, lit by the HDRI (set by the app).
void buildLookDevScene(Scene& s) {
    s = Scene();
    s.name = "LookDev";
    s.unitsPerMeter = 100.0f;
    s.gridScale = 1.0f;
    s.env.top = float3(0.030f, 0.032f, 0.038f);
    s.env.horizon = float3(0.055f, 0.057f, 0.062f);
    s.env.bottom = float3(0.02f, 0.02f, 0.022f);
    s.env.intensity = 1.0f;
    s.env.ambient = float3(0.0f);

    {
        Material g;
        g.name = "LookDev Floor";
        g.baseColor = float3(0.18f, 0.18f, 0.18f);   // 18% grey
        applyPreset(g, PRESET_MATTE, 1.0f);
        g.roughness = 0.75f;
        Object o;
        o.name = "Floor";
        o.part = "GROUND";
        o.mesh = s.addMesh("ground", makePlane(400.0f));
        o.material = s.addMaterial(g);
        o.selectable = false;
        s.addObject(o);
    }
    auto sphere = [&](const char* name, float x, float3 color, int preset) -> Material& {
        int idx = addPart(s, "SPHERE", 1, preset, float3(x, 3.0f, 0.0f), 0.0f);
        Object& o = s.objects[idx];
        o.name = name;
        Material& m = s.materials[o.material];
        m.name = name;
        m.baseColor = color;
        m.colorId = -1;
        applyPreset(m, preset, 1.0f);
        return m;
    };
    {
        Material& m = sphere("Clay Ball Grey", -16.0f, float3(0.30f, 0.30f, 0.30f), PRESET_MATTE);
        m.roughness = 0.9f;
        m.specular = 0.3f;
    }
    {
        Material& m = sphere("Chrome Ball", -8.0f, float3(0.96f, 0.96f, 0.97f), PRESET_CHROME);
        m.roughness = 0.02f;   // mirror
    }
    {
        Material& m = sphere("Glass Ball", 0.0f, float3(0.98f, 0.99f, 0.99f), PRESET_TRANSPARENT);
        m.roughness = 0.0f;
        m.ior = 1.5f;
    }
    {
        Material& m = sphere("Emissive Ball", 8.0f, float3(1.0f, 0.78f, 0.48f), PRESET_EMISSIVE);
        m.emissionStrength = 8.0f;   // a real light: it lights the floor and its neighbours
    }
    {
        Material& m = sphere("Candle Wax Ball (SSS)", 16.0f, float3(0.93f, 0.80f, 0.56f), PRESET_WAX);
        m.subsurface = 1.0f;
        m.sssRadius = float3(2.0f, 1.0f, 0.45f);   // deep, warm translucency of candle wax
        m.roughness = 0.4f;
    }

    Camera& c = s.camera;
    c.target = {0.0f, 3.5f, 0.0f};
    c.yaw = radians(0.0f);
    c.pitch = radians(10.0f);
    c.distance = 58.0f;
    c.fovY = radians(32.0f);
    c.dof = false;
    c.focusDistance = 58.0f;
    c.sensorScale = s.unitsPerMeter;
}

float lookDevHdriRotation(const EnvImage& img) {
    float peak = 0;
    double mean = 0;
    uint32_t px = 0;
    for (uint32_t y = 0; y < img.height; ++y)
        for (uint32_t x = 0; x < img.width; ++x) {
            float3 c = img.texel(x, y);
            float l = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
            mean += l;
            if (l > peak) { peak = l; px = x; }
        }
    mean /= (double)img.width * img.height;
    if (peak <= 30.0f * (float)mean) return 0.0f;
    float az = ((px + 0.5f) / img.width - 0.5f) * 2.0f * kPi;   // azimuth of the sun in the image
    float r = radians(145.0f) - az;
    return std::atan2(std::sin(r), std::cos(r));
}

std::vector<ViewPreset> defaultViews(const Scene& s, bool cornell) {
    std::vector<ViewPreset> v;
    Camera base = s.camera;
    v.push_back({L"Main View", base});
    if (cornell) {
        Camera c = base;
        c.target = {-6, 33, -6}; c.distance = 40; c.pitch = radians(8); c.yaw = radians(10); c.fStop = 2.0f; c.focusDistance = 40;
        v.push_back({L"Shader balls", c});
        c = base; c.target = {0, 22, 0}; c.pitch = radians(22); c.distance = 80; c.focusDistance = 70;
        v.push_back({L"High Angle", c});
        c = base; c.target = {-4, 4, 16}; c.yaw = radians(-20); c.pitch = radians(12); c.distance = 26; c.fStop = 2.8f; c.focusDistance = 26;
        v.push_back({L"Floor (DOF)", c});
    } else if (s.name == "LookDev") {
        Camera c = base;
        c.target = {4, 3.2f, 0}; c.distance = 30; c.pitch = radians(6); c.focusDistance = 30;
        v.push_back({L"Glass & Emissive", c});
        c = base; c.target = {-12, 3.2f, 0}; c.distance = 26; c.pitch = radians(8); c.yaw = radians(-15); c.focusDistance = 26;
        v.push_back({L"Clay & Chrome", c});
        c = base; c.pitch = radians(35); c.distance = 64; c.focusDistance = 64;
        v.push_back({L"High Angle", c});
    } else {
        Camera c = base;
        c.yaw = radians(90); c.pitch = radians(8); c.distance = 26; c.focusDistance = 26;
        v.push_back({L"Side", c});
        c = base; c.yaw = radians(0.01f); c.pitch = radians(85); c.distance = 34; c.focusDistance = 34;
        v.push_back({L"Top", c});
    }
    return v;
}

} // namespace app
