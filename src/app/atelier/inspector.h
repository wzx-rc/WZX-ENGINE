#pragma once
#include <array>
#include <string>
#include <vector>

namespace atelier::inspector {
struct Document {
    std::string name = "M_Copper_Brushed";
    int shader = 0, pattern = 1, environment = 0, textureChannel = 0;
    std::array<float,4> baseColor{184.f/255,74.f/255,33.f/255,1.f};
    std::array<float,4> emissionColor{1.f,102.f/255,26.f/255,1.f};
    float roughness=.27f, metallic=1.f, normalStrength=.3f, ao=1.f;
    float patternScale=8.f, clearcoat=.35f, coatRoughness=.16f, anisotropy=.42f, sheen=0.f;
    float emissionStrength=0.f, opacity=1.f, ior=1.5f;
    std::array<float,2> uvScale{1.f,1.f}, uvOffset{0.f,0.f};
    float uvRotation=0.f;
    bool tiling=true, twoSided=false, castShadows=true, receiveShadows=true;
    float exposure=0.f, environmentRotation=0.f, intensity=1.f;
    float keyIntensity=1.f, fillIntensity=.65f, rimIntensity=1.f;
    float yaw=28.f, pitch=12.f, distance=3.7f;
    bool grid=true, shadows=true, turntable=false;
    std::string texturePath;
    bool textureEnabled=false;
    float textureStrength=1.f;
};
struct ScalarSpec { const char* id; const char* label; float Document::*member; float minimum,maximum; int decimals; const char* unit; };
const std::vector<ScalarSpec>& ScalarSpecs();
void Clamp(Document&);
bool Equal(const Document&, const Document&);
std::string ToJson(const Document&);
bool FromJson(const std::string&, Document&, std::string& error);
bool ParseHex(const std::string&, std::array<float,4>&);
std::string Hex(const std::array<float,4>&);

struct ControlRect { std::string id; float x,y,width,height; bool enabled=true; };
struct InspectorState {
    Document value, defaults;
    int tab=0;
    char search[128]{};
    bool sections[10]{true,true,false,false,true,false,true,true,true,true};
    std::vector<Document> undo,redo;
    bool editing=false;
    Document editBefore;
    bool pickerOpen=false;
    int pickerTarget=0, pickerMode=0;
    std::array<float,4> pickerDraft{},pickerOriginal{};
    std::array<float,3> pickerHSV{};
    bool requestPicker=false;
    std::vector<ControlRect> controls;
    std::string status, storagePath;
    std::string textureSlotName = "Base Color";   // title of the Texture tab (host texture slot)
    void BeginEdit(const Document& before);
    bool EndEdit();
    bool Commit(const Document& before);
    bool Undo();
    bool Redo();
    bool Reset(const std::string& id);
    int ModifiedCount() const;
    bool Save(const std::string& path);
    bool Load(const std::string& path);
    void OpenPicker(int target=0);
    void ApplyPicker();
    void CancelPicker();
    const ControlRect* FindControl(const std::string&) const;
};
void ApplyTheme(float scale=1.f);
void DrawInspector(InspectorState&,float clientWidth,float clientHeight);
// Draws the panel inside the current ImGui window at a physical rectangle (host integration).
void DrawInspectorPanel(InspectorState&,float x,float y,float width,float height,float scale);
bool RunModelTests(const std::string& reportPath);
} // namespace atelier::inspector
