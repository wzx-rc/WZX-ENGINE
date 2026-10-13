#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace atelier::material {

using Id = std::uint32_t;
enum class Shader { Opaque = 0, Cutout = 1, Transparent = 2, Unlit = 3 };
enum class Shape { Sphere = 0, Cube = 1, Torus = 2, Plane = 3, ShaderBall = 4 };
enum class Environment { Studio = 0, Sunset = 1, Warehouse = 2 };
enum class DebugView { Lit = 0, BaseColor = 1, Roughness = 2, Metallic = 3, Normals = 4, AO = 5 };
enum class TextureSlot { BaseColor = 0, Roughness = 1, Metallic = 2, Normal = 3, AO = 4 };

struct TextureMap {
    std::string path;
    bool enabled = false;
    float strength = 1.0f;
    int channel = 0; // 0: RGB, 1: R, 2: G, 3: B, 4: A.
};

struct Material {
    Id id = 0;
    std::string name = "M_NewMaterial";
    std::string description;
    std::vector<std::string> tags;
    Shader shader = Shader::Opaque;
    std::array<float, 4> baseColor{0.63f, 0.24f, 0.12f, 1.0f};
    float roughness = 0.26f;
    float metallic = 1.0f;
    float normalStrength = 0.35f;
    float ao = 1.0f;
    std::array<float, 3> emissionColor{1.0f, 0.4f, 0.1f};
    float emissionStrength = 0.0f;
    float opacity = 1.0f;
    float ior = 1.5f;
    float clearcoat = 0.35f;
    float clearcoatRoughness = 0.16f;
    float anisotropy = 0.42f;
    float sheen = 0.0f;
    std::array<float, 2> uvScale{1.0f, 1.0f};
    std::array<float, 2> uvOffset{0.0f, 0.0f};
    float uvRotation = 0.0f;
    bool tiling = true;
    int pattern = 1; // 0 smooth, 1 brushed, 2 stone, 3 wood, 4 checker, 5 fabric.
    float patternScale = 8.0f;
    bool twoSided = false;
    bool castShadows = true;
    bool receiveShadows = true;
    std::array<TextureMap, 5> textureMaps;
};

struct PreviewSettings {
    Shape shape = Shape::ShaderBall;
    Environment environment = Environment::Studio;
    DebugView debugView = DebugView::Lit;
    float yaw = 28.0f;
    float pitch = 12.0f;
    float distance = 3.7f;
    float exposure = 0.0f;
    float environmentRotation = 0.0f;
    float intensity = 1.0f;
    float keyIntensity = 1.0f;
    float fillIntensity = 0.65f;
    float rimIntensity = 1.0f;
    bool grid = true;
    bool shadows = true;
    bool turntable = false;
};

enum class Severity { Info, Warning, Error };
struct Diagnostic { Severity severity = Severity::Info; Id material = 0; std::string message; };

const char* ShaderName(Shader shader);
const char* ShapeName(Shape shape);
const char* EnvironmentName(Environment environment);
const char* DebugViewName(DebugView debugView);
const char* TextureSlotName(TextureSlot slot);
const char* PatternName(int pattern);
void ClampMaterial(Material& material);
void ClampPreview(PreviewSettings& preview);

class MaterialLibrary {
public:
    MaterialLibrary();
    const std::vector<Material>& Items() const { return materials_; }
    Material& Current();
    const Material& Current() const;
    Material* Find(Id id);
    const Material* Find(Id id) const;
    Id SelectedId() const { return selected_; }
    bool Select(Id id);
    Id Add(const std::string& name = "M_NewMaterial");
    Id Duplicate(Id id = 0);
    bool Remove(Id id = 0);
    void SeedPresets();

    // Coalesce direct edits into one undo entry; pointers expire on mutations.
    void BeginEdit();
    bool EndEdit();
    void CancelEdit();
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    void ClearHistory();
    bool IsDirty() const;
    bool Save(const std::string& path, std::string* error = nullptr);
    bool Load(const std::string& path, std::string* error = nullptr);
    std::vector<Diagnostic> Validate(bool inspectTextures = true) const;

private:
    struct Snapshot { std::vector<Material> materials; Id selected = 0; Id nextId = 1; };
    Snapshot Capture() const;
    void Restore(const Snapshot& snapshot);
    void Record(const Snapshot& before);
    std::string UniqueName(const std::string& wanted) const;
    std::vector<Material> materials_;
    Id selected_ = 0;
    Id nextId_ = 1;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool editing_ = false;
    Snapshot editBefore_;
    Snapshot saved_;
};

bool RunMaterialTests(const std::string& directory, const std::string& reportPath);

} // namespace atelier::material
