#include "material_model.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace atelier::material {
namespace {
constexpr std::size_t HistoryLimit = 128;
constexpr std::size_t MaterialLimit = 4096;
constexpr std::size_t StringLimit = 1024 * 1024;
constexpr std::size_t TagLimit = 64;

void Error(std::string* output, const std::string& value) { if (output) *output = value; }
float Safe(float value, float low, float high, float fallback = 0.0f) {
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}
bool Range(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
bool SameMaterial(const Material& a, const Material& b) {
    if (a.id != b.id || a.name != b.name || a.description != b.description || a.tags != b.tags ||
        a.shader != b.shader || a.baseColor != b.baseColor || a.roughness != b.roughness ||
        a.metallic != b.metallic || a.normalStrength != b.normalStrength || a.ao != b.ao ||
        a.emissionColor != b.emissionColor || a.emissionStrength != b.emissionStrength ||
        a.opacity != b.opacity || a.ior != b.ior || a.clearcoat != b.clearcoat ||
        a.clearcoatRoughness != b.clearcoatRoughness || a.anisotropy != b.anisotropy ||
        a.sheen != b.sheen || a.uvScale != b.uvScale || a.uvOffset != b.uvOffset ||
        a.uvRotation != b.uvRotation || a.tiling != b.tiling || a.pattern != b.pattern ||
        a.patternScale != b.patternScale || a.twoSided != b.twoSided ||
        a.castShadows != b.castShadows || a.receiveShadows != b.receiveShadows) return false;
    for (std::size_t i = 0; i < a.textureMaps.size(); ++i) {
        const auto& x = a.textureMaps[i]; const auto& y = b.textureMaps[i];
        if (x.path != y.path || x.enabled != y.enabled || x.strength != y.strength || x.channel != y.channel) return false;
    }
    return true;
}
bool SameMaterials(const std::vector<Material>& a, const std::vector<Material>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), SameMaterial);
}
template<class Enum> const char* EnumName(Enum value, const char* const* names, int count) {
    const int index = static_cast<int>(value);
    return index >= 0 && index < count ? names[index] : "Unknown";
}
std::string Json(const std::string& text) {
    std::ostringstream out;
    for (unsigned char c : text) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else out << c;
    }
    return out.str();
}

void WriteMaterial(std::ostream& out, const Material& m) {
    out << "MATERIAL " << m.id << ' ' << std::quoted(m.name) << ' ' << std::quoted(m.description) << ' ' << int(m.shader) << '\n';
    out << "TAGS " << m.tags.size();
    for (const auto& tag : m.tags) out << ' ' << std::quoted(tag);
    out << "\nSURFACE";
    for (float v : m.baseColor) out << ' ' << v;
    out << ' ' << m.roughness << ' ' << m.metallic << ' ' << m.normalStrength << ' ' << m.ao;
    for (float v : m.emissionColor) out << ' ' << v;
    out << ' ' << m.emissionStrength << ' ' << m.opacity << ' ' << m.ior << ' ' << m.clearcoat
        << ' ' << m.clearcoatRoughness << ' ' << m.anisotropy << ' ' << m.sheen << '\n';
    out << "UV " << m.uvScale[0] << ' ' << m.uvScale[1] << ' ' << m.uvOffset[0] << ' ' << m.uvOffset[1]
        << ' ' << m.uvRotation << ' ' << int(m.tiling) << ' ' << m.pattern << ' ' << m.patternScale << '\n';
    out << "FLAGS " << int(m.twoSided) << ' ' << int(m.castShadows) << ' ' << int(m.receiveShadows) << '\n';
    out << "TEXTURES " << m.textureMaps.size() << '\n';
    for (const auto& t : m.textureMaps) out << "MAP " << int(t.enabled) << ' ' << t.strength << ' ' << t.channel << ' ' << std::quoted(t.path) << '\n';
    out << "END_MATERIAL\n";
}

bool ReadBool(std::istream& in, bool& value) {
    int integer = -1;
    if (!(in >> integer) || (integer != 0 && integer != 1)) return false;
    value = integer != 0; return true;
}
bool Marker(std::istream& in, const char* expected) {
    std::string token; return static_cast<bool>(in >> token) && token == expected;
}
bool Quoted(std::istream& in, std::string& text) {
    in >> std::ws;
    if (in.peek() != '"' || !(in >> std::quoted(text))) return false;
    return text.size() <= StringLimit;
}
bool ReadMaterial(std::istream& in, Material& m, std::string& error) {
    long long rawId = 0;
    int shader = 0;
    if (!Marker(in, "MATERIAL") || !(in >> rawId) || rawId <= 0 || rawId >= std::numeric_limits<Id>::max() ||
        !Quoted(in, m.name) || !Quoted(in, m.description) || !(in >> shader) || shader < 0 || shader > 3) {
        error = "Invalid material identity, name or shader."; return false;
    }
    m.id = static_cast<Id>(rawId); m.shader = static_cast<Shader>(shader);
    std::size_t count = 0;
    if (!Marker(in, "TAGS") || !(in >> count) || count > TagLimit) { error = "Invalid material tags."; return false; }
    for (std::size_t i = 0; i < count; ++i) {
        std::string tag;
        if (!Quoted(in, tag)) { error = "Invalid material tag text."; return false; }
        m.tags.push_back(std::move(tag));
    }
    if (!Marker(in, "SURFACE")) { error = "Missing SURFACE properties."; return false; }
    for (float& v : m.baseColor) if (!(in >> v)) { error = "Invalid base color."; return false; }
    if (!(in >> m.roughness >> m.metallic >> m.normalStrength >> m.ao)) { error = "Invalid surface properties."; return false; }
    for (float& v : m.emissionColor) if (!(in >> v)) { error = "Invalid emission color."; return false; }
    if (!(in >> m.emissionStrength >> m.opacity >> m.ior >> m.clearcoat >> m.clearcoatRoughness >> m.anisotropy >> m.sheen)) {
        error = "Invalid layered surface properties."; return false;
    }
    if (!Marker(in, "UV") || !(in >> m.uvScale[0] >> m.uvScale[1] >> m.uvOffset[0] >> m.uvOffset[1] >> m.uvRotation) ||
        !ReadBool(in, m.tiling) || !(in >> m.pattern >> m.patternScale)) { error = "Invalid UV or procedural properties."; return false; }
    if (!Marker(in, "FLAGS") || !ReadBool(in, m.twoSided) || !ReadBool(in, m.castShadows) || !ReadBool(in, m.receiveShadows)) {
        error = "Invalid material flags."; return false;
    }
    if (!Marker(in, "TEXTURES") || !(in >> count) || count != m.textureMaps.size()) { error = "Invalid texture slot count."; return false; }
    for (auto& t : m.textureMaps) {
        if (!Marker(in, "MAP") || !ReadBool(in, t.enabled) || !(in >> t.strength >> t.channel) || !Quoted(in, t.path)) {
            error = "Invalid texture map properties."; return false;
        }
    }
    if (!Marker(in, "END_MATERIAL")) { error = "Missing END_MATERIAL marker."; return false; }
    return true;
}
} // namespace

const char* ShaderName(Shader value) {
    static const char* names[]{"Opaque", "Cutout", "Transparent", "Unlit"}; return EnumName(value, names, 4);
}
const char* ShapeName(Shape value) {
    static const char* names[]{"Sphere", "Cube", "Torus", "Plane", "Shader Ball"}; return EnumName(value, names, 5);
}
const char* EnvironmentName(Environment value) {
    static const char* names[]{"Studio", "Sunset", "Warehouse"}; return EnumName(value, names, 3);
}
const char* DebugViewName(DebugView value) {
    static const char* names[]{"Lit", "Base Color", "Roughness", "Metallic", "Normals", "Ambient Occlusion"}; return EnumName(value, names, 6);
}
const char* TextureSlotName(TextureSlot value) {
    static const char* names[]{"Base Color", "Roughness", "Metallic", "Normal", "Ambient Occlusion"}; return EnumName(value, names, 5);
}
const char* PatternName(int value) {
    static const char* names[]{"Smooth", "Brushed Metal", "Ancient Stone", "Wood Grain", "Checker", "Woven Fabric"};
    return value >= 0 && value < 6 ? names[value] : "Unknown";
}

void ClampMaterial(Material& m) {
    m.shader = static_cast<Shader>(std::clamp(int(m.shader), 0, 3));
    for (float& v : m.baseColor) v = Safe(v, 0.0f, 1.0f);
    m.roughness = Safe(m.roughness, 0.0f, 1.0f, 0.5f);
    m.metallic = Safe(m.metallic, 0.0f, 1.0f);
    m.normalStrength = Safe(m.normalStrength, 0.0f, 4.0f, 1.0f);
    m.ao = Safe(m.ao, 0.0f, 1.0f, 1.0f);
    for (float& v : m.emissionColor) v = Safe(v, 0.0f, 1.0f);
    m.emissionStrength = Safe(m.emissionStrength, 0.0f, 100.0f);
    m.opacity = Safe(m.opacity, 0.0f, 1.0f, 1.0f);
    m.ior = Safe(m.ior, 1.0f, 3.0f, 1.5f);
    m.clearcoat = Safe(m.clearcoat, 0.0f, 1.0f);
    m.clearcoatRoughness = Safe(m.clearcoatRoughness, 0.0f, 1.0f);
    m.anisotropy = Safe(m.anisotropy, 0.0f, 1.0f);
    m.sheen = Safe(m.sheen, 0.0f, 1.0f);
    for (float& v : m.uvScale) v = Safe(v, 0.01f, 100.0f, 1.0f);
    for (float& v : m.uvOffset) v = Safe(v, -100.0f, 100.0f);
    m.uvRotation = Safe(m.uvRotation, -360.0f, 360.0f);
    m.pattern = std::clamp(m.pattern, 0, 5);
    m.patternScale = Safe(m.patternScale, 0.1f, 100.0f, 8.0f);
    for (auto& t : m.textureMaps) { t.strength = Safe(t.strength, 0.0f, 4.0f, 1.0f); t.channel = std::clamp(t.channel, 0, 4); }
}
void ClampPreview(PreviewSettings& p) {
    p.shape = static_cast<Shape>(std::clamp(int(p.shape), 0, 4));
    p.environment = static_cast<Environment>(std::clamp(int(p.environment), 0, 2));
    p.debugView = static_cast<DebugView>(std::clamp(int(p.debugView), 0, 5));
    p.yaw = Safe(p.yaw, -100000.0f, 100000.0f, 28.0f);
    p.pitch = Safe(p.pitch, -85.0f, 85.0f, 12.0f);
    p.distance = Safe(p.distance, 1.5f, 12.0f, 3.7f);
    p.exposure = Safe(p.exposure, -5.0f, 5.0f);
    p.environmentRotation = Safe(p.environmentRotation, -360.0f, 360.0f);
    p.intensity = Safe(p.intensity, 0.0f, 4.0f, 1.0f);
    p.keyIntensity = Safe(p.keyIntensity, 0.0f, 4.0f, 1.0f);
    p.fillIntensity = Safe(p.fillIntensity, 0.0f, 4.0f, 0.65f);
    p.rimIntensity = Safe(p.rimIntensity, 0.0f, 4.0f, 1.0f);
}

MaterialLibrary::MaterialLibrary() { SeedPresets(); }
Material* MaterialLibrary::Find(Id id) {
    const auto i = std::find_if(materials_.begin(), materials_.end(), [=](const Material& m) { return m.id == id; });
    return i == materials_.end() ? nullptr : &*i;
}
const Material* MaterialLibrary::Find(Id id) const {
    const auto i = std::find_if(materials_.begin(), materials_.end(), [=](const Material& m) { return m.id == id; });
    return i == materials_.end() ? nullptr : &*i;
}
Material& MaterialLibrary::Current() {
    if (Material* m = Find(selected_)) return *m;
    throw std::logic_error("Material selection is invalid.");
}
const Material& MaterialLibrary::Current() const {
    if (const Material* m = Find(selected_)) return *m;
    throw std::logic_error("Material selection is invalid.");
}
bool MaterialLibrary::Select(Id id) {
    if (!Find(id)) return false;
    if (editing_) EndEdit();
    selected_ = id; return true;
}
MaterialLibrary::Snapshot MaterialLibrary::Capture() const { return {materials_, selected_, nextId_}; }
void MaterialLibrary::Restore(const Snapshot& snapshot) { materials_ = snapshot.materials; selected_ = snapshot.selected; nextId_ = snapshot.nextId; }
void MaterialLibrary::Record(const Snapshot& before) {
    if (editing_ || (SameMaterials(before.materials, materials_) && before.selected == selected_ && before.nextId == nextId_)) return;
    undo_.push_back(before); if (undo_.size() > HistoryLimit) undo_.erase(undo_.begin()); redo_.clear();
}
std::string MaterialLibrary::UniqueName(const std::string& wanted) const {
    const std::string base = wanted.empty() ? "M_NewMaterial" : wanted;
    auto exists = [&](const std::string& name) { return std::any_of(materials_.begin(), materials_.end(), [&](const Material& m) { return m.name == name; }); };
    if (!exists(base)) return base;
    for (std::size_t n = 2; n <= MaterialLimit + 1; ++n) {
        const std::string name = base + "_" + std::to_string(n);
        if (!exists(name)) return name;
    }
    return base + "_" + std::to_string(nextId_);
}
Id MaterialLibrary::Add(const std::string& name) {
    if (materials_.size() >= MaterialLimit || nextId_ >= std::numeric_limits<Id>::max()) return 0;
    const auto before = Capture();
    Material m; m.id = nextId_++; m.name = UniqueName(name); m.description = "Custom physically based material.";
    m.metallic = 0.0f; m.roughness = 0.45f; m.pattern = 0; m.anisotropy = 0.0f; m.clearcoat = 0.0f;
    m.baseColor = {0.54f, 0.58f, 0.62f, 1.0f}; m.tags = {"custom"};
    selected_ = m.id; materials_.push_back(std::move(m)); Record(before); return selected_;
}
Id MaterialLibrary::Duplicate(Id id) {
    if (!id) id = selected_;
    const Material* source = Find(id);
    if (!source || materials_.size() >= MaterialLimit || nextId_ >= std::numeric_limits<Id>::max()) return 0;
    const auto before = Capture(); Material m = *source;
    m.id = nextId_++; m.name = UniqueName(source->name + "_Copy"); selected_ = m.id;
    materials_.push_back(std::move(m)); Record(before); return selected_;
}
bool MaterialLibrary::Remove(Id id) {
    if (!id) id = selected_;
    auto found = std::find_if(materials_.begin(), materials_.end(), [=](const Material& m) { return m.id == id; });
    if (found == materials_.end() || materials_.size() <= 1) return false;
    const auto before = Capture(); const auto index = static_cast<std::size_t>(std::distance(materials_.begin(), found));
    materials_.erase(found);
    if (selected_ == id) selected_ = materials_[std::min(index, materials_.size() - 1)].id;
    Record(before); return true;
}
void MaterialLibrary::SeedPresets() {
    if (editing_) EndEdit();
    const bool initialized = !materials_.empty();
    const auto before = Capture();
    materials_.clear(); nextId_ = 1;
    auto preset = [&](const char* name, const char* description, std::initializer_list<const char*> tags) -> Material& {
        Material m; m.id = nextId_++; m.name = name; m.description = description;
        m.pattern = 0; m.anisotropy = 0.0f; m.clearcoat = 0.0f; m.normalStrength = 0.3f;
        for (const char* tag : tags) m.tags.push_back(tag);
        materials_.push_back(std::move(m)); return materials_.back();
    };
    auto& copper = preset("M_Copper_Brushed", "Warm copper with directional brushing and a polished protective coat.", {"metal", "copper", "brushed"});
    copper.baseColor = {0.72f, 0.29f, 0.13f, 1.0f}; copper.metallic = 1.0f; copper.roughness = 0.27f;
    copper.pattern = 1; copper.patternScale = 8.0f; copper.anisotropy = 0.42f; copper.clearcoat = 0.35f;
    auto& stone = preset("M_Stone_Ancient", "Weathered limestone for coastal ruins, cliffs and architectural pieces.", {"stone", "environment", "ruins"});
    stone.baseColor = {0.56f, 0.48f, 0.34f, 1.0f}; stone.metallic = 0.0f; stone.roughness = 0.86f;
    stone.pattern = 2; stone.patternScale = 6.0f; stone.normalStrength = 1.15f; stone.ao = 0.87f;
    auto& ceramic = preset("M_Ceramic_Cobalt", "Deep cobalt ceramic with a smooth glazed finish.", {"ceramic", "blue", "glazed"});
    ceramic.baseColor = {0.025f, 0.14f, 0.46f, 1.0f}; ceramic.metallic = 0.0f; ceramic.roughness = 0.18f;
    ceramic.clearcoat = 0.85f; ceramic.clearcoatRoughness = 0.08f;
    auto& gold = preset("M_Gold_Polished", "Polished gold for ornaments and decorative trim.", {"metal", "gold", "polished"});
    gold.baseColor = {0.93f, 0.63f, 0.18f, 1.0f}; gold.metallic = 1.0f; gold.roughness = 0.14f;
    auto& rubber = preset("M_Rubber_Matte", "Soft charcoal rubber with a broad diffuse response.", {"rubber", "dark", "matte"});
    rubber.baseColor = {0.055f, 0.064f, 0.073f, 1.0f}; rubber.metallic = 0.0f; rubber.roughness = 0.92f;
    auto& fabric = preset("M_Fabric_Sage", "Sage textile with a fine woven pattern and grazing-angle sheen.", {"fabric", "green", "woven"});
    fabric.baseColor = {0.23f, 0.35f, 0.27f, 1.0f}; fabric.metallic = 0.0f; fabric.roughness = 0.81f;
    fabric.pattern = 5; fabric.patternScale = 12.0f; fabric.sheen = 0.65f; fabric.normalStrength = 0.75f;
    auto& glow = preset("M_Emission_Amber", "Amber emissive material for signals, crystals and light fixtures.", {"emissive", "amber", "fx"});
    glow.baseColor = {0.93f, 0.31f, 0.04f, 1.0f}; glow.metallic = 0.0f; glow.roughness = 0.35f;
    glow.emissionColor = {1.0f, 0.25f, 0.025f}; glow.emissionStrength = 3.0f;
    auto& glass = preset("M_Glass_Clear", "A transparent surface approximation with adjustable opacity and IOR.", {"glass", "transparent", "clear"});
    glass.shader = Shader::Transparent; glass.baseColor = {0.65f, 0.82f, 0.87f, 1.0f}; glass.metallic = 0.0f;
    glass.roughness = 0.06f; glass.opacity = 0.27f; glass.ior = 1.52f; glass.clearcoat = 0.75f;
    auto& wood = preset("M_Wood_Walnut", "Warm walnut with procedural directional grain.", {"wood", "brown", "organic"});
    wood.baseColor = {0.31f, 0.13f, 0.055f, 1.0f}; wood.metallic = 0.0f; wood.roughness = 0.48f;
    wood.pattern = 3; wood.patternScale = 4.0f; wood.clearcoat = 0.18f;
    auto& checker = preset("M_UV_Checker", "A neutral checker pattern for validating UV scale, rotation and placement.", {"utility", "uv", "checker"});
    checker.baseColor = {0.68f, 0.72f, 0.76f, 1.0f}; checker.metallic = 0.0f; checker.roughness = 0.65f;
    checker.pattern = 4; checker.patternScale = 8.0f;
    selected_ = materials_.front().id;
    if (initialized) Record(before);
    else { ClearHistory(); saved_ = Capture(); }
}
void MaterialLibrary::BeginEdit() { if (!editing_) { editBefore_ = Capture(); editing_ = true; } }
bool MaterialLibrary::EndEdit() {
    if (!editing_) return false;
    for (auto& m : materials_) ClampMaterial(m);
    const bool changed = !SameMaterials(editBefore_.materials, materials_) || editBefore_.selected != selected_ || editBefore_.nextId != nextId_;
    editing_ = false; Record(editBefore_); editBefore_ = {}; return changed;
}
void MaterialLibrary::CancelEdit() { if (editing_) { Restore(editBefore_); editing_ = false; editBefore_ = {}; } }
bool MaterialLibrary::Undo() {
    if (editing_) EndEdit();
    if (undo_.empty()) return false;
    redo_.push_back(Capture()); Restore(undo_.back()); undo_.pop_back(); return true;
}
bool MaterialLibrary::Redo() {
    if (editing_) EndEdit();
    if (redo_.empty()) return false;
    undo_.push_back(Capture()); Restore(redo_.back()); redo_.pop_back(); return true;
}
void MaterialLibrary::ClearHistory() { undo_.clear(); redo_.clear(); editing_ = false; editBefore_ = {}; }
bool MaterialLibrary::IsDirty() const { return !SameMaterials(saved_.materials, materials_); }

std::vector<Diagnostic> MaterialLibrary::Validate(bool inspectTextures) const {
    std::vector<Diagnostic> out;
    auto add = [&](Severity severity, Id id, const std::string& text) { out.push_back({severity, id, text}); };
    if (materials_.empty() || materials_.size() > MaterialLimit) add(Severity::Error, 0, "Material library must contain between 1 and 4096 materials.");
    if (!Find(selected_)) add(Severity::Error, selected_, "Selected material does not exist.");
    std::set<Id> ids; std::set<std::string> names; Id maximum = 0;
    for (const auto& m : materials_) {
        maximum = std::max(maximum, m.id);
        if (!m.id || m.id >= std::numeric_limits<Id>::max() || !ids.insert(m.id).second) add(Severity::Error, m.id, "Material ID is invalid or duplicated.");
        if (m.name.empty() || m.name.size() > StringLimit || m.description.size() > StringLimit || m.tags.size() > TagLimit)
            add(Severity::Error, m.id, "Material name, description or tag count is invalid.");
        if (!names.insert(m.name).second) add(Severity::Warning, m.id, "Material name is duplicated; IDs remain unique.");
        for (const auto& tag : m.tags) if (tag.size() > StringLimit) add(Severity::Error, m.id, "Material tag exceeds the text limit.");
        if (int(m.shader) < 0 || int(m.shader) > 3) add(Severity::Error, m.id, "Unknown shader type.");
        bool surface = std::all_of(m.baseColor.begin(), m.baseColor.end(), [](float v) { return Range(v, 0, 1); }) &&
            std::all_of(m.emissionColor.begin(), m.emissionColor.end(), [](float v) { return Range(v, 0, 1); }) &&
            Range(m.roughness, 0, 1) && Range(m.metallic, 0, 1) && Range(m.normalStrength, 0, 4) && Range(m.ao, 0, 1) &&
            Range(m.emissionStrength, 0, 100) && Range(m.opacity, 0, 1) && Range(m.ior, 1, 3) && Range(m.clearcoat, 0, 1) &&
            Range(m.clearcoatRoughness, 0, 1) && Range(m.anisotropy, 0, 1) && Range(m.sheen, 0, 1);
        if (!surface) add(Severity::Error, m.id, "Surface properties must be finite and within their declared ranges.");
        if (!std::all_of(m.uvScale.begin(), m.uvScale.end(), [](float v) { return Range(v, 0.01f, 100); }) ||
            !std::all_of(m.uvOffset.begin(), m.uvOffset.end(), [](float v) { return Range(v, -100, 100); }) ||
            !Range(m.uvRotation, -360, 360) || m.pattern < 0 || m.pattern > 5 || !Range(m.patternScale, 0.1f, 100))
            add(Severity::Error, m.id, "UV transform or procedural pattern is invalid.");
        for (std::size_t i = 0; i < m.textureMaps.size(); ++i) {
            const auto& map = m.textureMaps[i]; const std::string slot = TextureSlotName(static_cast<TextureSlot>(i));
            if (map.path.size() > StringLimit || !Range(map.strength, 0, 4) || map.channel < 0 || map.channel > 4)
                add(Severity::Error, m.id, slot + ": texture properties are invalid.");
            if (map.enabled && map.path.empty()) add(Severity::Warning, m.id, slot + ": enabled texture slot has no image; scalar fallback is used.");
            else if (inspectTextures && map.enabled) {
                std::error_code code;
                if (!std::filesystem::is_regular_file(std::filesystem::u8path(map.path), code))
                    add(Severity::Warning, m.id, slot + ": image file is unavailable; scalar fallback is used.");
            }
        }
        if (m.shader == Shader::Transparent) add(Severity::Info, m.id, "Transparency preview uses a surface approximation; engine refraction is configured through IOR.");
    }
    if (!nextId_ || nextId_ <= maximum) add(Severity::Error, 0, "Next material ID is invalid.");
    return out;
}

bool MaterialLibrary::Save(const std::string& path, std::string* error) {
    Error(error, "");
    for (const auto& d : Validate(false)) if (d.severity == Severity::Error) { Error(error, d.message); return false; }
    std::filesystem::path temporary;
    auto cleanup = [&] { if (!temporary.empty()) { std::error_code code; std::filesystem::remove(temporary, code); } };
    try {
        const auto destination = std::filesystem::u8path(path);
        temporary = destination; temporary += ".writing-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { Error(error, "Cannot open material library for writing."); return false; }
        out.imbue(std::locale::classic()); out << std::setprecision(std::numeric_limits<float>::max_digits10);
        out << "ATELIER_MATERIAL_LIBRARY 1\nMATERIALS " << materials_.size() << ' ' << selected_ << ' ' << nextId_ << '\n';
        for (const auto& m : materials_) WriteMaterial(out, m);
        out << "END_LIBRARY\n"; out.flush();
        if (!out) { out.close(); cleanup(); Error(error, "Material library write did not complete; previous file is preserved."); return false; }
        out.close();
        if (!out) { cleanup(); Error(error, "Cannot close the material file; previous file is preserved."); return false; }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto code = GetLastError(); cleanup();
            Error(error, "Cannot replace the material file (Windows error " + std::to_string(code) + "); previous file is preserved."); return false;
        }
#else
        std::filesystem::rename(temporary, destination);
#endif
        saved_ = Capture(); return true;
    } catch (const std::exception& exception) { cleanup(); Error(error, exception.what()); return false; }
}
bool MaterialLibrary::Load(const std::string& path, std::string* error) {
    Error(error, "");
    try {
        const auto source = std::filesystem::u8path(path);
        if (std::filesystem::file_size(source) > 32 * 1024 * 1024) { Error(error, "Material library exceeds the 32 MB limit."); return false; }
        std::ifstream in(source, std::ios::binary);
        if (!in) { Error(error, "Cannot open material library for reading."); return false; }
        in.imbue(std::locale::classic());
        int version = 0;
        if (!Marker(in, "ATELIER_MATERIAL_LIBRARY") || !(in >> version) || version != 1) {
            Error(error, "Unsupported material format; expected ATELIER_MATERIAL_LIBRARY version 1."); return false;
        }
        std::size_t count = 0; long long selected = 0, next = 0;
        if (!Marker(in, "MATERIALS") || !(in >> count >> selected >> next) || count == 0 || count > MaterialLimit ||
            selected <= 0 || selected >= std::numeric_limits<Id>::max() || next <= 0 || next > std::numeric_limits<Id>::max()) {
            Error(error, "Invalid material count or selection."); return false;
        }
        MaterialLibrary parsed; parsed.materials_.clear(); parsed.selected_ = static_cast<Id>(selected); parsed.nextId_ = static_cast<Id>(next);
        for (std::size_t i = 0; i < count; ++i) {
            Material m; std::string reason;
            if (!ReadMaterial(in, m, reason)) { Error(error, "Material " + std::to_string(i + 1) + ": " + reason); return false; }
            parsed.materials_.push_back(std::move(m));
        }
        if (!Marker(in, "END_LIBRARY")) { Error(error, "Missing END_LIBRARY marker."); return false; }
        std::string trailing;
        if (in >> trailing) { Error(error, "Unexpected trailing material data."); return false; }
        for (const auto& d : parsed.Validate(false)) if (d.severity == Severity::Error) { Error(error, d.message); return false; }
        if (editing_) EndEdit();
        const auto before = Capture(); Restore(parsed.Capture()); Record(before); saved_ = Capture(); return true;
    } catch (const std::exception& exception) { Error(error, exception.what()); return false; }
}

bool RunMaterialTests(const std::string& directory, const std::string& reportPath) {
    struct Test { std::string name; bool passed; std::string detail; };
    std::vector<Test> tests;
    auto check = [&](const std::string& name, bool passed, const std::string& detail = "") { tests.push_back({name, passed, detail}); };
    try {
        const auto folder = std::filesystem::u8path(directory); std::filesystem::create_directories(folder);
        const std::string fixture = (folder / "material-model-fixture.amat").u8string();
        const std::string bad = (folder / "material-model-invalid.amat").u8string();
        MaterialLibrary library;
        check("ten_distinct_physically_based_presets", library.Items().size() == 10 && library.Current().pattern == 1 && library.Current().metallic == 1.0f);
        const auto diagnostics = library.Validate();
        check("default_library_has_no_errors", std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; }));
        check("default_library_is_clean", !library.IsDirty() && !library.CanUndo());
        const Id copper = library.SelectedId(); const Id stone = library.Items()[1].id;
        check("selection_by_stable_id", library.Select(stone) && library.Current().name == "M_Stone_Ancient" && !library.IsDirty());
        check("invalid_selection_preserves_selection", !library.Select(90000) && library.SelectedId() == stone);
        library.Select(copper); const Material original = library.Current();
        library.BeginEdit(); library.Current().roughness = 0.71f; library.Current().baseColor[0] = 0.26f; library.Current().normalStrength = 1.2f; library.EndEdit();
        check("coalesced_material_edit", library.CanUndo() && library.IsDirty() && library.Current().roughness == 0.71f);
        check("undo_restores_all_properties", library.Undo() && SameMaterial(library.Current(), original) && !library.CanUndo());
        check("redo_restores_transaction", library.Redo() && library.Current().roughness == 0.71f && library.Current().normalStrength == 1.2f);
        library.BeginEdit(); library.Current().name = "Temporary"; library.Current().roughness = 0.03f; library.CancelEdit();
        check("cancel_restores_transaction", library.Current().name == original.name && library.Current().roughness == 0.71f);
        library.ClearHistory(); library.BeginEdit();
        check("no_op_edit_has_no_history", !library.EndEdit() && !library.CanUndo());
        const Id copy = library.Duplicate();
        check("duplicate_has_unique_id_and_name", copy && copy != copper && library.Current().name == "M_Copper_Brushed_Copy" && library.Current().roughness == 0.71f);
        check("undo_duplicate_restores_selection", library.Undo() && library.SelectedId() == copper && !library.Find(copy));
        library.Redo(); library.Remove(copy);
        check("delete_material_updates_selection", !library.Find(copy) && library.Find(library.SelectedId()) && library.Items().size() == 10);
        check("undo_delete_restores_selection", library.Undo() && library.SelectedId() == copy && library.Find(copy));
        const Id added = library.Add("M_Stone_Ancient");
        check("add_generates_unique_name", added != copy && library.Current().name == "M_Stone_Ancient_2" && library.Current().metallic == 0.0f);
        library.BeginEdit(); auto& mutableMaterial = library.Current();
        mutableMaterial.name = u8"M_Céramique_été \"bleu\""; mutableMaterial.description = "First line\nSecond line with \\ and \"quotes\"";
        mutableMaterial.tags = {u8"été", "tag with spaces"}; mutableMaterial.shader = Shader::Cutout;
        mutableMaterial.roughness = 0.51f; mutableMaterial.metallic = 0.21f; mutableMaterial.normalStrength = 1.1f;
        mutableMaterial.ao = 0.72f; mutableMaterial.emissionColor = {0.25f, 0.33f, 0.42f}; mutableMaterial.emissionStrength = 2.3f;
        mutableMaterial.opacity = 0.62f; mutableMaterial.ior = 1.73f; mutableMaterial.clearcoat = 0.4f;
        mutableMaterial.clearcoatRoughness = 0.35f; mutableMaterial.anisotropy = 0.8f; mutableMaterial.sheen = 0.4f;
        mutableMaterial.uvScale = {1.3f, 2.4f}; mutableMaterial.uvOffset = {-0.25f, 0.6f}; mutableMaterial.uvRotation = 32.0f;
        mutableMaterial.tiling = false; mutableMaterial.pattern = 5; mutableMaterial.patternScale = 19.0f;
        mutableMaterial.twoSided = true; mutableMaterial.castShadows = false; mutableMaterial.receiveShadows = false;
        for (std::size_t i = 0; i < mutableMaterial.textureMaps.size(); ++i) {
            mutableMaterial.textureMaps[i] = {u8"C:/échantillons/texture " + std::to_string(i) + ".png", true, 0.5f + float(i) * 0.2f, int(i)};
        }
        library.EndEdit(); std::string error;
        MaterialLibrary loaded;
        check("all_pbr_properties_and_utf8_roundtrip", library.Save(fixture, &error) && loaded.Load(fixture, &error) &&
            SameMaterials(library.Items(), loaded.Items()) && library.SelectedId() == loaded.SelectedId(), error);
        check("successful_save_load_marks_clean", !library.IsDirty() && !loaded.IsDirty());
        const auto missing = loaded.Validate();
        check("missing_texture_images_emit_warnings", std::count_if(missing.begin(), missing.end(), [](const Diagnostic& d) { return d.severity == Severity::Warning && d.message.find("unavailable") != std::string::npos; }) == 5);
        loaded.BeginEdit(); loaded.Current().roughness = -8; loaded.Current().metallic = 5;
        loaded.Current().normalStrength = std::numeric_limits<float>::quiet_NaN(); loaded.Current().uvScale[0] = 0;
        loaded.Current().textureMaps[0].channel = 14; loaded.Current().pattern = 40; loaded.EndEdit();
        check("edit_clamps_invalid_numeric_properties", loaded.Current().roughness == 0 && loaded.Current().metallic == 1 &&
            loaded.Current().normalStrength == 1 && loaded.Current().uvScale[0] == 0.01f && loaded.Current().textureMaps[0].channel == 4 && loaded.Current().pattern == 5);
        loaded.Undo(); const auto preserved = loaded.Items(); const Id preservedSelection = loaded.SelectedId();
        { std::ofstream out(std::filesystem::u8path(bad)); out << "ATELIER_MATERIAL_LIBRARY 99\n"; }
        check("unsupported_file_preserves_live_library", !loaded.Load(bad, &error) && !error.empty() && SameMaterials(preserved, loaded.Items()) && loaded.SelectedId() == preservedSelection);
        std::ifstream fixtureIn(std::filesystem::u8path(fixture), std::ios::binary);
        const std::string fixtureText((std::istreambuf_iterator<char>(fixtureIn)), {});
        auto rejected = [&](std::string text) {
            { std::ofstream out(std::filesystem::u8path(bad), std::ios::binary | std::ios::trunc); out << text; }
            return !loaded.Load(bad, &error) && !error.empty() && SameMaterials(preserved, loaded.Items());
        };
        check("truncated_file_rejected_atomically", rejected(fixtureText.substr(0, fixtureText.size() / 2)));
        check("trailing_data_rejected_atomically", rejected(fixtureText + "EXTRA_DATA\n"));
        std::string duplicateId = fixtureText;
        const auto secondId = duplicateId.find("MATERIAL 2 ");
        if (secondId != std::string::npos) duplicateId.replace(secondId, 11, "MATERIAL 1 ");
        check("duplicate_ids_rejected_atomically", secondId != std::string::npos && rejected(duplicateId));
        std::string badRange = fixtureText;
        const auto surface = badRange.find("SURFACE ");
        if (surface != std::string::npos) { const auto end = badRange.find(' ', surface + 8); badRange.replace(surface + 8, end - surface - 8, "5"); }
        check("out_of_range_file_rejected_atomically", surface != std::string::npos && rejected(badRange));
        std::string badBoolean = fixtureText;
        const auto flags = badBoolean.find("FLAGS ");
        if (flags != std::string::npos) badBoolean[flags + 6] = '9';
        check("invalid_boolean_file_rejected_atomically", flags != std::string::npos && rejected(badBoolean));
        loaded.Current().roughness = std::numeric_limits<float>::infinity();
        check("invalid_save_preserves_existing_file", !loaded.Save(fixture, &error) && !error.empty() && library.Load(fixture) && SameMaterials(preserved, library.Items()));
        loaded.Current().roughness = 0.51f;
        check("bad_destination_preserves_model", !loaded.Save((folder / "does-not-exist" / "blocked.amat").u8string(), &error) && !error.empty() && SameMaterials(preserved, loaded.Items()));
        MaterialLibrary single;
        while (single.Items().size() > 1) single.Remove(single.Items().back().id);
        check("cannot_delete_last_material", !single.Remove() && single.Items().size() == 1 && single.SelectedId() == single.Current().id);
        MaterialLibrary divergent;
        divergent.Add("M_Custom"); divergent.Undo(); divergent.Add("M_Changed");
        check("new_edit_invalidates_redo", !divergent.CanRedo());
        divergent.SeedPresets();
        check("restore_presets_is_undoable", divergent.Items().size() == 10 && divergent.Undo() && divergent.Current().name == "M_Changed");
        PreviewSettings preview; preview.distance = -100; preview.pitch = 900; preview.exposure = 15; preview.intensity = -1;
        preview.shape = static_cast<Shape>(99); preview.keyIntensity = std::numeric_limits<float>::quiet_NaN(); ClampPreview(preview);
        check("preview_camera_and_lighting_clamp", preview.distance == 1.5f && preview.pitch == 85 && preview.exposure == 5 && preview.intensity == 0 && preview.shape == Shape::ShaderBall && preview.keyIntensity == 1);
    } catch (const std::exception& exception) { check("unexpected_exception", false, exception.what()); }
    const bool passed = !tests.empty() && std::all_of(tests.begin(), tests.end(), [](const Test& t) { return t.passed; });
    try {
        const auto report = std::filesystem::u8path(reportPath);
        if (!report.parent_path().empty()) std::filesystem::create_directories(report.parent_path());
        std::ofstream out(report, std::ios::binary | std::ios::trunc); if (!out) return false;
        out << "{\n  \"suite\": \"Atelier material model\",\n  \"passed\": " << (passed ? "true" : "false") << ",\n  \"tests\": [\n";
        for (std::size_t i = 0; i < tests.size(); ++i) {
            out << "    {\"name\": \"" << Json(tests[i].name) << "\", \"passed\": " << (tests[i].passed ? "true" : "false")
                << ", \"detail\": \"" << Json(tests[i].detail) << "\"}" << (i + 1 < tests.size() ? "," : "") << '\n';
        }
        out << "  ]\n}\n"; out.flush(); return passed && static_cast<bool>(out);
    } catch (...) { return false; }
}

} // namespace atelier::material

#ifdef ATELIER_MATERIAL_MODEL_TEST_MAIN
int main(int argc, char** argv) {
    const std::string directory = argc > 1 ? argv[1] : "outputs";
    const std::string report = argc > 2 ? argv[2] : "outputs/material-model-test.json";
    return atelier::material::RunMaterialTests(directory, report) ? 0 : 1;
}
#endif
