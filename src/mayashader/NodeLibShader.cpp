// Node library of the Maya shader graph (replaces game/NodeLib.cpp in WzxShaderGraph.exe).
// Each node stands for an existing Maya / Arnold node; the Maya side (tools/maya/scripts/wzx_shader_bridge.py)
// maps the node ids and pin names below to node types and attributes. Shader closures use the Object pin type.
#include "game/Graph.h"

namespace game {

namespace {

struct B {
    NodeDef d;
    B(const char* id, const char* label, const char* cat, const char* help) {
        d.op = Op::ConstFloat;   // not run by the game runtime
        d.id = id;
        d.label = label;
        d.category = cat;
        d.help = help;
        d.pure = true;
    }
    B& in(const char* n, PinType t) { d.ins.push_back({n, t}); d.defaults.push_back(Value::None(t)); return *this; }
    B& inF(const char* n, float v) { d.ins.push_back({n, PinType::Float}); d.defaults.push_back(Value::Float(v)); return *this; }
    B& inB(const char* n, bool v) { d.ins.push_back({n, PinType::Bool}); d.defaults.push_back(Value::Bool(v)); return *this; }
    B& inC(const char* n, float3 v) { d.ins.push_back({n, PinType::Vec3}); d.defaults.push_back(Value::Vec3(v)); return *this; }
    B& inS(const char* n, const char* v) { d.ins.push_back({n, PinType::String}); d.defaults.push_back(Value::Str(v)); return *this; }
    B& out(const char* n, PinType t) { d.outs.push_back({n, t}); return *this; }
};

std::vector<NodeDef> build() {
    const PinType F = PinType::Float, C = PinType::Vec3, SH = PinType::Object;
    const char* OU = "Output";
    const char* SD = "Shaders";
    const char* TX = "Textures";
    const char* UT = "Utilities";
    const char* CO = "Constants";
    std::vector<B> v;
    v.push_back(B("out.material", "Material Output", OU, "Shading group (shadingEngine) of the material. Assign it from the toolbar.")
                    .in("Surface", SH));
    v.push_back(B("shader.standardSurface", "aiStandardSurface", SD, "Arnold Standard Surface.")
                    .inF("Base", 1.0f).inC("Base Color", float3(0.8f, 0.8f, 0.8f)).inF("Diffuse Roughness", 0.0f).inF("Metalness", 0.0f)
                    .inF("Specular", 1.0f).inC("Specular Color", float3(1, 1, 1)).inF("Specular Roughness", 0.2f).inF("Specular IOR", 1.5f)
                    .inF("Transmission", 0.0f).inC("Transmission Color", float3(1, 1, 1)).inF("Subsurface", 0.0f).inC("Subsurface Color", float3(1, 1, 1))
                    .inF("Coat", 0.0f).inF("Coat Roughness", 0.1f).inF("Emission", 0.0f).inC("Emission Color", float3(1, 1, 1))
                    .in("Normal", C).out("Shader", SH));
    v.push_back(B("shader.mix", "aiMixShader", SD, "Blends two shaders (Mix = 0: Shader 1, 1: Shader 2).")
                    .in("Shader 1", SH).in("Shader 2", SH).inF("Mix", 0.5f).out("Shader", SH));
    v.push_back(B("shader.flat", "aiFlat", SD, "Constant colour, no lighting.").inC("Color", float3(1, 1, 1)).out("Shader", SH));
    v.push_back(B("tex.ramp", "Ramp", TX, "Maya ramp texture (V direction): Color A at the bottom, Color B at the top.")
                    .inC("Color A", float3(0.05f, 0.1f, 0.4f)).inF("Position A", 0.0f).inC("Color B", float3(0.9f, 0.6f, 0.2f)).inF("Position B", 1.0f)
                    .inB("U Direction", false).inF("Repeat", 1.0f).out("Color", C).out("Alpha", F));
    v.push_back(B("tex.checker", "Checker", TX, "Maya checker texture.")
                    .inC("Color 1", float3(0.9f, 0.9f, 0.9f)).inC("Color 2", float3(0.1f, 0.1f, 0.1f)).inF("Contrast", 1.0f).inF("Repeat", 4.0f)
                    .out("Color", C).out("Alpha", F));
    v.push_back(B("tex.noise", "aiNoise", TX, "Arnold 3D fractal noise.")
                    .inC("Color 1", float3(0, 0, 0)).inC("Color 2", float3(1, 1, 1)).inF("Scale", 1.0f).inF("Octaves", 1.0f).inF("Distortion", 0.0f)
                    .inF("Amplitude", 1.0f).out("Color", C));
    v.push_back(B("tex.image", "aiImage", TX, "Image file texture.").inS("File", "").inF("Repeat", 1.0f).out("Color", C));
    v.push_back(B("util.colorCorrect", "aiColorCorrect", UT, "Gamma, hue, saturation and exposure of a colour.")
                    .inC("Input", float3(1, 1, 1)).inF("Gamma", 1.0f).inF("Hue Shift", 0.0f).inF("Saturation", 1.0f).inF("Exposure", 0.0f)
                    .out("Color", C));
    v.push_back(B("util.blend", "Blend Colors", UT, "Maya blendColors: Blender = 1 gives Color 1.")
                    .inC("Color 1", float3(1, 0, 0)).inC("Color 2", float3(0, 0, 1)).inF("Blender", 0.5f).out("Color", C));
    v.push_back(B("util.multiply", "Multiply", UT, "Maya multiplyDivide (multiply): A x B per channel.")
                    .inC("A", float3(1, 1, 1)).inC("B", float3(1, 1, 1)).out("Color", C));
    v.push_back(B("util.bump", "aiBump2d", UT, "Bump from a height value (connect a texture alpha or colour).")
                    .inF("Bump Map", 0.0f).inF("Bump Height", 1.0f).out("Normal", C));
    v.push_back(B("const.color", "Color Constant", CO, "Maya colorConstant node.").inC("Color", float3(0.8f, 0.3f, 0.2f)).out("Color", C));
    v.push_back(B("const.float", "Float Constant", CO, "Maya floatConstant node.").inF("Value", 0.5f).out("Value", F));
    std::vector<NodeDef> out;
    for (auto& b : v) out.push_back(b.d);
    return out;
}

} // namespace

const std::vector<NodeDef>& nodeDefs() {
    static const std::vector<NodeDef> defs = build();
    return defs;
}

} // namespace game
