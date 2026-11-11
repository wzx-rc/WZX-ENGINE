// AI control of the editor: the commands of the API added for the MCP bridge (materials, curves, render mode, undo,
// workspaces, prefab) and the Tools > AI Control (MCP) window. The API itself: GameHost::command (AppGame.cpp).
#include "App.h"
#include "AiControl.h"
#include "CurveEditor.h"
#include "GameHost.h"
#include "Theme.h"
#include <cmath>
#include <filesystem>
#include <sstream>

namespace app {

namespace fs = std::filesystem;

static std::string num(float v) {
    char b[32];
    snprintf(b, sizeof b, "%g", v);
    return b;
}
static std::string vec(const float3& v) { return "[" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + "]"; }
static std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }
static bool readVec(const Json& j, float3& out) {
    if (j.type != Json::Array || j.a.size() < 3) return false;
    out = float3((float)j[0].num(), (float)j[1].num(), (float)j[2].num());
    return true;
}
static float toSrgb(float c) {
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}
static float3 srgbOf(const float3& lin) { return float3(toSrgb(lin.x), toSrgb(lin.y), toSrgb(lin.z)); }
static float3 linearOf(const float3& c) {
    return float3(srgbToLinear(clampf(c.x, 0, 1)), srgbToLinear(clampf(c.y, 0, 1)), srgbToLinear(clampf(c.z, 0, 1)));
}

static const char* kRenderModes[] = {"lit", "metal", "progressive", "gi", "splitrc"};
static const char* kCurveTypes[] = {"none", "linear", "cv", "ep", "bezier"};
static const char* kCurveShapes[] = {"none", "line", "circle", "square", "arc", "helix", "spiral", "star"};
static const char* kWorkspaces[] = {"scene", "graph", "materials", "prefab"};

static int indexOf(const char* const* names, int count, const std::string& s) {
    for (int i = 0; i < count; ++i) if (s == names[i]) return i;
    return -1;
}

// Bezier points given without handles: smooth handles a third of the way to the neighbours.
static void autoHandles(CurveData& c) {
    const int n = c.count();
    c.hin.resize(n);
    c.hout.resize(n);
    c.broken.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        const float3 prev = i > 0 ? c.points[i - 1] : (c.closed ? c.points[n - 1] : c.points[i]);
        const float3 next = i + 1 < n ? c.points[i + 1] : (c.closed ? c.points[0] : c.points[i]);
        const float3 t = (next - prev) * (1.0f / 6.0f);
        c.hin[i] = c.points[i] - t;
        c.hout[i] = c.points[i] + t;
    }
}

// The "points" of a request into the curve (Bezier: optional "in" / "out" handles); false when missing.
static bool readCurvePoints(const Json& j, CurveData& c) {
    if (j["points"].type != Json::Array) return false;
    c.points.clear();
    for (const Json& p : j["points"].a) {
        float3 v;
        if (!readVec(p, v)) return false;
        c.points.push_back(v);
    }
    if (c.points.size() < 2) return false;
    if (c.type == CURVE_BEZIER) {
        autoHandles(c);
        const size_t n = c.points.size();
        if (j["in"].a.size() == n && j["out"].a.size() == n)
            for (size_t i = 0; i < n; ++i) { readVec(j["in"][i], c.hin[i]); readVec(j["out"][i], c.hout[i]); }
    } else {
        c.hin.clear();
        c.hout.clear();
        c.broken.clear();
    }
    return true;
}

std::string GameHost::extraCommand(const std::string& cmd, const Json& j, int& status, bool& handled) {
    App& a = m_app;
    Scene& s = a.m_scene;
    handled = true;
    auto fail = [&](const std::string& e) { status = 400; return "{\"ok\": false, \"error\": " + jstr(e) + "}"; };
    auto ok = [](const std::string& extra = "") { return "{\"ok\": true" + extra + "}"; };
    auto materialIndex = [&](const Json& t) -> int {
        if (t.type == Json::Number) return t.n >= 0 && t.n < s.materials.size() ? (int)t.n : -1;
        if (t.type == Json::String)
            for (size_t i = 0; i < s.materials.size(); ++i) if (s.materials[i].name == t.s) return (int)i;
        return -1;
    };
    auto pathArg = [&](const std::string& def) {
        fs::path p = fromUtf8(j["path"].str(def));
        if (p.is_relative()) p = fs::path(m_projectDir) / p;
        return p;
    };

    // --- object.add of a parametric primitive (the parts of the game library: object.add with "part")
    if (cmd == "object.add" && j["primitive"].type == Json::String) {
        std::string name = j["primitive"].s;
        int type = -1;
        for (int t = PRIM_CUBE; t <= PRIM_DISK; ++t) {
            std::string n = primitiveName(t);
            for (char& ch : n) ch = (char)tolower((unsigned char)ch);
            if (n == name) type = t;
        }
        if (type < 0) return fail("unknown primitive: " + name + " (cube, sphere, plane, cylinder, torus, disk)");
        if (m_playing) return fail("stop Play first");
        float3 pos(0, 0, 0), v;
        readVec(j["pos"], pos);
        a.pushUndo();
        int idx = app::addPrimitive(s, type, pos);
        if (idx < 0) return fail("primitive not added");
        Object& o = s.objects[idx];
        if (j["name"].type == Json::String) o.name = a.uniqueNodeName(j["name"].s);
        if (j["yaw"].type == Json::Number) {
            o.yaw = radians((float)j["yaw"].n);
            o.rot = qAxisAngle(float3(0, 1, 0), o.yaw);
            o.useRot = true;
        }
        if (readVec(j["scale"], v)) o.scale = v;
        if (readVec(j["color"], v)) {
            Material m = s.materials[o.material];   // its own material (the primitives may share the default one)
            m.name = o.name;
            m.baseColor = linearOf(v);
            m.colorId = -1;
            o.material = s.addMaterial(m);
        }
        a.outlinerIds();
        a.markDirty();
        return ok(", \"index\": " + std::to_string(idx) + ", \"name\": " + jstr(o.name));
    }
    // --- undo / redo, view, render mode, workspaces
    if (cmd == "undo" || cmd == "redo") {
        if (m_playing) return fail("stop Play first");
        const size_t before = cmd == "undo" ? a.m_undo.size() : a.m_redo.size();
        if (!before) return fail(std::string("nothing to ") + cmd);
        if (cmd == "undo") a.undo(); else a.redo();
        return ok(", \"objects\": " + std::to_string(s.objects.size()));
    }
    if (cmd == "view.frame") {
        if (j["target"].type != Json::Null) {
            int i = resolveObject(j["target"]);
            if (i < 0) return fail("object not found");
            a.m_selected = i;
            a.m_multi.clear();
        }
        a.frameSelection();
        return ok();
    }
    if (cmd == "render.get" || cmd == "render.set") {
        if (cmd == "render.set") {
            if (j["mode"].type == Json::String) {
                int m = indexOf(kRenderModes, 5, j["mode"].s);
                if (m < 0) return fail("unknown mode: " + j["mode"].s + " (lit, metal, progressive, gi, splitrc)");
                a.setRenderMode(m);
            }
            if (j["exposure"].type == Json::Number) a.m_rs.exposure = clampf((float)j["exposure"].n, 0.01f, 64.0f);
            a.markDirty();
        }
        const int m = a.renderMode();
        return ok(", \"mode\": " + jstr(m >= 0 && m < 5 ? kRenderModes[m] : "?") + ", \"exposure\": " + num(a.m_rs.exposure));
    }
    if (cmd == "ai.window") {   // Tools > AI Control (MCP), for captures
        m_showAi = j["open"].type != Json::Bool || j["open"].b;
        return ok();
    }
    if (cmd == "workspace.set") {
        int w = indexOf(kWorkspaces, 4, j["name"].str());
        if (w < 0) return fail("unknown workspace (scene, graph, materials, prefab)");
        if (w == 1) setGraphOpen(true);
        else a.m_workspace = w;
        return ok(", \"workspace\": " + jstr(kWorkspaces[w]));
    }
    // --- materials
    if (cmd == "material.list") {
        std::ostringstream js;
        js << "{\"ok\": true, \"materials\": [";
        for (size_t i = 0; i < s.materials.size(); ++i) {
            const Material& m = s.materials[i];
            js << (i ? ",\n  " : "\n  ") << "{\"index\": " << i << ", \"name\": " << jstr(m.name) << ", \"baseColor\": " << vec(srgbOf(m.baseColor))
               << ", \"roughness\": " << num(m.roughness) << ", \"metallic\": " << num(m.metallic) << ", \"specular\": " << num(m.specular)
               << ", \"transmission\": " << num(m.transmission) << ", \"ior\": " << num(m.ior) << ", \"emission\": " << vec(srgbOf(m.emission))
               << ", \"emissionStrength\": " << num(m.emissionStrength) << ", \"coat\": " << num(m.coat) << ", \"coatRoughness\": " << num(m.coatRoughness)
               << ", \"subsurface\": " << num(m.subsurface) << ", \"objects\": [";
            bool first = true;
            for (size_t k = 0; k < s.objects.size(); ++k)
                if (s.objects[k].material == (int)i && !s.objects[k].group && !s.objects[k].camera) { js << (first ? "" : ", ") << k; first = false; }
            js << "]}";
        }
        js << "\n]}";
        return js.str();
    }
    if (cmd == "material.set") {
        int i = materialIndex(j["target"]);
        if (i < 0) return fail("material not found (see material.list)");
        if (!m_playing) a.pushUndo();
        Material& m = s.materials[i];
        float3 v;
        auto f01 = [&](const char* k, float& dst, float lo = 0, float hi = 1) { if (j[k].type == Json::Number) dst = clampf((float)j[k].n, lo, hi); };
        if (j["name"].type == Json::String) m.name = j["name"].s;
        if (readVec(j["baseColor"], v)) { m.baseColor = linearOf(v); m.colorId = -1; }
        if (readVec(j["emission"], v)) m.emission = linearOf(v);
        f01("roughness", m.roughness);
        f01("metallic", m.metallic);
        f01("specular", m.specular);
        f01("transmission", m.transmission);
        f01("ior", m.ior, 1.0f, 3.0f);
        f01("emissionStrength", m.emissionStrength, 0.0f, 1000.0f);
        f01("coat", m.coat);
        f01("coatRoughness", m.coatRoughness);
        f01("subsurface", m.subsurface);
        a.markDirty();
        return ok(", \"index\": " + std::to_string(i));
    }
    // --- curves
    if (cmd == "curve.add") {
        if (!a.m_curves) return fail("no curve tools");
        if (m_playing) return fail("stop Play first");
        CurveData c;
        std::string name = j["name"].str();
        if (j["shape"].type == Json::String) {
            int shape = indexOf(kCurveShapes, CSHAPE_COUNT, j["shape"].s);
            if (shape <= 0) return fail("unknown shape (line, circle, square, arc, helix, spiral, star)");
            c = makeCurveShape(shape);
            if (name.empty()) name = curveShapeName(shape);
        } else {
            c.type = indexOf(kCurveTypes, CURVE_TYPE_COUNT, j["type"].str("cv"));
            if (c.type <= 0) return fail("unknown type (linear, cv, ep, bezier)");
            c.closed = j["closed"].type == Json::Bool && j["closed"].b;
            if (!readCurvePoints(j, c)) return fail("points: at least 2 points [x, y, z]");
            if (name.empty()) name = "Curve";
        }
        float3 pos(0, 0, 0);
        readVec(j["pos"], pos);
        int idx = a.m_curves->addCurveObject(c, pos, name);
        return ok(", \"index\": " + std::to_string(idx) + ", \"name\": " + jstr(s.objects[idx].name));
    }
    if (cmd == "curve.get" || cmd == "curve.set") {
        int i = resolveObject(j["target"]);
        if (i < 0 || !s.objects[i].curve.valid()) return fail("curve object not found");
        CurveData& c = s.objects[i].curve;
        if (cmd == "curve.set") {
            if (m_playing) return fail("stop Play first");
            CurveData n = c;
            if (j["type"].type == Json::String) {
                n.type = indexOf(kCurveTypes, CURVE_TYPE_COUNT, j["type"].s);
                if (n.type <= 0) return fail("unknown type (linear, cv, ep, bezier)");
            }
            if (j["closed"].type == Json::Bool) n.closed = j["closed"].b;
            if (j["points"].type != Json::Null) {
                if (!readCurvePoints(j, n)) return fail("points: at least 2 points [x, y, z]");
            } else if (n.type == CURVE_BEZIER && n.hin.size() != n.points.size()) {
                autoHandles(n);
            }
            n.shape = CurveShapeParams();   // edited by hand: no longer a parametric shape
            a.pushUndo();
            c = n;
            a.markDirty();
        }
        std::ostringstream js;
        js << "{\"ok\": true, \"index\": " << i << ", \"name\": " << jstr(s.objects[i].name) << ", \"type\": " << jstr(kCurveTypes[std::clamp(c.type, 0, 4)])
           << ", \"closed\": " << (c.closed ? "true" : "false") << ", \"pos\": " << vec(s.objects[i].position) << ", \"length\": " << num(curveLength(c))
           << ", \"points\": [";
        for (size_t k = 0; k < c.points.size(); ++k) js << (k ? ", " : "") << vec(c.points[k]);
        js << "]";
        if (c.type == CURVE_BEZIER && c.hin.size() == c.points.size()) {
            js << ", \"in\": [";
            for (size_t k = 0; k < c.hin.size(); ++k) js << (k ? ", " : "") << vec(c.hin[k]);
            js << "], \"out\": [";
            for (size_t k = 0; k < c.hout.size(); ++k) js << (k ? ", " : "") << vec(c.hout[k]);
            js << "]";
        }
        js << "}";
        return js.str();
    }
    // --- prefab (the Prefab workspace)
    // --- live prefab instances of the scene (PrefabInstance.h), without the studio
    if (cmd == "prefab.instance") {   // {prefab: name or file, pos?, yaw?, name?} -> index of the instance
        if (m_playing) return fail("stop Play first");
        const std::string path = a.m_prefabs.resolve(j["prefab"].str());
        if (path.empty()) return fail("prefab not found: " + j["prefab"].str());
        const Json& p = j["pos"];
        float3 pos(0, 0, 0);
        if (p.type == Json::Array && p.a.size() >= 3) pos = float3((float)p[0].num(), (float)p[1].num(), (float)p[2].num());
        a.pushUndo();
        a.outlinerIds();
        const PrefabAsset* as = a.m_prefabs.get(path);
        const int ri = addPrefabInstance(a.m_scene, a.m_prefabs, path, pos, (float)j["yaw"].num(0),
                                         a.uniqueNodeName(j["name"].str(as ? as->name : "Prefab")));
        if (ri < 0) return fail("prefab not read: " + path);
        a.m_scene.objects[ri].collapsed = true;
        syncPrefabInstances(a.m_scene, a.m_prefabs, true);
        a.markDirty();
        return "{\"ok\": true, \"index\": " + std::to_string(ri) + ", \"prefab\": " + jstr(path) + "}";
    }
    if (cmd == "prefab.unpack") {   // {target}: the instance becomes a plain group
        if (m_playing) return fail("stop Play first");
        const int o = resolveObject(j["target"]);
        if (o < 0) return fail("no such object");
        a.select(o);
        a.prefabUnpackSelected();
        return "{\"ok\": true}";
    }
    if (cmd == "prefab.openInstance") {   // {target}: Ouvrir le prefab of an instance (or of one of its parts)
        const int o = resolveObject(j["target"]);
        if (a.prefabInstanceUnder(o) < 0) return fail("not a prefab instance");
        if (!a.m_pf.initialised) a.prefabInit();
        a.prefabOpenInstance(o);
        return std::string("{\"ok\": true, \"asked\": ") + (a.m_prefabAskOpen ? "true" : "false") + ", \"workspace\": " + std::to_string(a.m_workspace) + "}";
    }
    if (cmd == "ui.prefabMenu") {   // {target} or {close: true}: the right-click menu of an instance in the view (captures)
        if (j["close"].type == Json::Bool && j["close"].b) { a.m_vpCtxClose = true; return "{\"ok\": true}"; }
        const int o = resolveObject(j["target"]);
        const int r = a.prefabInstanceUnder(o);
        if (r < 0) return fail("not a prefab instance");
        float2 sp;
        if (!a.project(a.m_scene.objects[r].position + float3(0, 1.0f, 0), sp)) return fail("not in view");
        a.select(r);
        a.m_vpCtxObj = r;
        a.m_vpCtxAt = ImVec2(sp.x, sp.y);
        a.m_vpCtxRequest = true;
        return "{\"ok\": true}";
    }
    if (cmd == "prefab.animate") {   // {on}: the clips of the instances play in the editor viewport too
        a.m_prefabAnimate = j["on"].type == Json::Bool ? j["on"].b : !a.m_prefabAnimate;
        return std::string("{\"ok\": true, \"animate\": ") + (a.m_prefabAnimate ? "true" : "false") + "}";
    }
    if (cmd.rfind("prefab.", 0) == 0) {
        App::PrefabStudio& P = a.m_pf;
        if (!P.initialised) a.prefabInit();
        if (cmd == "prefab.new") a.prefabNew(j["demo"].type == Json::Bool && j["demo"].b);
        else if (cmd == "prefab.recolor") {   // {material, color}: the parts of the studio with that material recoloured (not saved)
            float3 c;
            if (!readVec(j["color"], c)) return fail("give color");
            int n = 0;
            for (PrefabPart& p : P.asset.parts)
                if (p.material.name == j["material"].str()) { p.material.baseColor = c; ++n; }
            if (!n) return fail("no part with the material " + j["material"].str());
            P.rebuild = true;
        }
        else if (cmd == "prefab.open") {
            if (!a.prefabOpen(pathArg("").wstring())) return fail("prefab not opened: " + j["path"].str());
        } else if (cmd == "prefab.save") {
            std::wstring keep = P.path;
            if (j["path"].type == Json::String) P.path = pathArg("").wstring();
            else if (P.path.empty()) {
                fs::path dir = fs::path(m_projectDir) / "game" / "prefabs";
                std::error_code ec;
                fs::create_directories(dir, ec);
                P.path = (dir / (fromUtf8(P.asset.name) + kPrefabExt)).wstring();
            }
            if (!a.prefabSave(false)) { P.path = keep; return fail("prefab not saved"); }
        } else if (cmd == "prefab.place") {
            if (P.asset.parts.empty()) return fail("the prefab has no part");
            if (m_playing) return fail("stop Play first");
            a.prefabAddToScene();
            a.m_workspace = 0;
        } else if (cmd != "prefab.info") {
            return fail("unknown command: " + cmd + " (prefab.info, prefab.new, prefab.open, prefab.save, prefab.place)");
        }
        std::ostringstream js;
        js << "{\"ok\": true, \"name\": " << jstr(P.asset.name) << ", \"path\": " << jstr(toUtf8(P.path)) << ", \"modified\": "
           << (a.prefabDirty() ? "true" : "false") << ", \"parts\": [";
        for (size_t k = 0; k < P.asset.parts.size(); ++k) js << (k ? ", " : "") << jstr(P.asset.parts[k].name);
        js << "], \"clips\": [";
        for (size_t k = 0; k < P.asset.clips.size(); ++k) js << (k ? ", " : "") << jstr(P.asset.clips[k].name);
        js << "]}";
        return js.str();
    }
    handled = false;
    return std::string();
}

// --------------------------------------------------------------------------- Tools > AI Control (MCP)

void GameHost::drawAiWindow() {
    const float d = m_app.m_dpi;
    ImGui::SetNextWindowSize(ImVec2(620 * d, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("AI Control (MCP)", &m_showAi, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    UiSettings& us = th::settings();
    bool on = us.aiControl;
    if (ImGui::Checkbox("Allow AI apps to drive this editor", &on)) {
        us.aiControl = on;
        if (on) startApi(); else stopApi();
        if (!m_app.m_themeTestOnly) th::saveSettings(m_app.m_exeDir + L"\\wzx_settings.ini");
    }
    ImGui::TextDisabled(m_server.running() ? "Listening on 127.0.0.1:%d (this PC only, with the token of this installation)" : "Off", m_server.port());
    ImGui::Separator();
    ImGui::TextWrapped("Connect your AI app (Claude Desktop, Claude Code, Cursor, Windsurf...): add WZX Engine as an MCP server. "
                       "The app starts \"Viewport3D.exe --mcp\", which talks to this editor (or opens it).");
    ImGui::Spacing();
    ImGui::TextUnformatted("Claude Desktop, Cursor, Windsurf: MCP configuration file (Claude Desktop: Settings > Developer > Edit Config)");
    std::string cfg = mcpConfigJson();
    ImGui::InputTextMultiline("##mcpConfig", &cfg[0], cfg.size() + 1, ImVec2(-1, 150 * d), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button(m_aiCopied == "config" ? "Copied" : "Copy configuration")) { ImGui::SetClipboardText(cfg.c_str()); m_aiCopied = "config"; }
    ImGui::Spacing();
    ImGui::TextUnformatted("Claude Code: run this command once");
    std::string cc = mcpClaudeCodeCommand();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##mcpClaude", &cc[0], cc.size() + 1, ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button(m_aiCopied == "claude" ? "Copied##cc" : "Copy command")) { ImGui::SetClipboardText(cc.c_str()); m_aiCopied = "claude"; }
    ImGui::Separator();
    ImGui::TextWrapped("Security: only programs of this PC that can read the token (%%LOCALAPPDATA%%\\WZX Engine\\api.json) are accepted; web pages are refused. "
                       "A new token disconnects every AI app until it restarts its MCP server.");
    if (ImGui::Button("New token")) {
        m_server.setToken(newApiToken());
        if (m_server.running()) writeApiPort(m_server.port());
        m_aiCopied = "token";
        toast(L"AI control: new token");
    }
    ImGui::End();
}

} // namespace app
