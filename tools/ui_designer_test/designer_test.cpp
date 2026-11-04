// UI Designer test without GPU or Windows (Linux / macOS, clang or gcc): drives wzui::Designer with simulated mouse
// and keyboard input, checks the edits, and rasterizes the ImGui draw data to PPM images (convert to PNG with any tool).
// Build from the repository root:
//   clang++ -std=c++17 -O1 -DIMGUI_DEFINE_MATH_OPERATORS -Isrc -Ithird_party/imgui tools/ui_designer_test/designer_test.cpp \
//       src/app/designer/*.cpp third_party/imgui/imgui*.cpp -o designer_test
//   ./designer_test out "assets/themes/Blue 01.theme"
#include "imgui.h"
#include "imgui_internal.h"
#include "app/designer/Designer.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <algorithm>

struct Tex { int w, h; std::vector<unsigned char> rgba; };
static std::map<ImTextureID, Tex> g_tex;
static float mx = -1, my = -1; static bool down = false, mdown = false;
static std::vector<std::pair<ImGuiKey,bool>> keys;
static const int W = 1600, H = 900;

static void handleTextures(ImDrawData* dd) {
    if (!dd->Textures) return;
    for (ImTextureData* t : *dd->Textures) {
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
            ImTextureID id = t->TexID ? t->TexID : (ImTextureID)(intptr_t)(g_tex.size() + 1);
            Tex& x = g_tex[id]; x.w = t->Width; x.h = t->Height; x.rgba.resize(x.w * x.h * 4);
            const unsigned char* p = (const unsigned char*)t->GetPixels();
            for (int i = 0; i < x.w * x.h; ++i) {
                if (t->Format == ImTextureFormat_RGBA32) for (int c = 0; c < 4; ++c) x.rgba[i*4+c] = p[i*4+c];
                else { x.rgba[i*4]=x.rgba[i*4+1]=x.rgba[i*4+2]=255; x.rgba[i*4+3]=p[i]; }
            }
            t->SetTexID(id); t->SetStatus(ImTextureStatus_OK);
        } else if (t->Status == ImTextureStatus_WantDestroy) { t->SetTexID(ImTextureID_Invalid); t->SetStatus(ImTextureStatus_Destroyed); }
    }
}

static void raster(ImDrawData* dd, const char* path) {
    std::vector<float> fb(W * H * 3, 0.07f);
    for (const ImDrawList* cl : dd->CmdLists) {
        const ImDrawVert* vb = cl->VtxBuffer.Data; const ImDrawIdx* ib = cl->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : cl->CmdBuffer) {
            if (cmd.UserCallback) continue;
            const Tex* tex = nullptr; auto it = g_tex.find(cmd.GetTexID()); if (it != g_tex.end()) tex = &it->second;
            int cx0 = std::max(0, (int)cmd.ClipRect.x), cy0 = std::max(0, (int)cmd.ClipRect.y), cx1 = std::min(W, (int)cmd.ClipRect.z), cy1 = std::min(H, (int)cmd.ClipRect.w);
            for (unsigned e = 0; e < cmd.ElemCount; e += 3) {
                const ImDrawVert* v[3] = {&vb[ib[cmd.IdxOffset+e]+cmd.VtxOffset], &vb[ib[cmd.IdxOffset+e+1]+cmd.VtxOffset], &vb[ib[cmd.IdxOffset+e+2]+cmd.VtxOffset]};
                float minx = std::min({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), maxx = std::max({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x});
                float miny = std::min({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), maxy = std::max({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y});
                int x0 = std::max(cx0, (int)std::floor(minx)), x1 = std::min(cx1, (int)std::ceil(maxx)), y0 = std::max(cy0, (int)std::floor(miny)), y1 = std::min(cy1, (int)std::ceil(maxy));
                float area = (v[1]->pos.x - v[0]->pos.x) * (v[2]->pos.y - v[0]->pos.y) - (v[2]->pos.x - v[0]->pos.x) * (v[1]->pos.y - v[0]->pos.y);
                if (std::fabs(area) < 1e-8f) continue;
                for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((v[1]->pos.x - px) * (v[2]->pos.y - py) - (v[2]->pos.x - px) * (v[1]->pos.y - py)) / area;
                    float w1 = ((v[2]->pos.x - px) * (v[0]->pos.y - py) - (v[0]->pos.x - px) * (v[2]->pos.y - py)) / area;
                    float w2 = 1 - w0 - w1;
                    if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                    float u = w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x, vv = w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y;
                    float col[4];
                    for (int c = 0; c < 4; ++c) col[c] = (w0 * ((v[0]->col >> (8*c)) & 255) + w1 * ((v[1]->col >> (8*c)) & 255) + w2 * ((v[2]->col >> (8*c)) & 255)) / 255.f;
                    if (tex) {
                        // bilinear
                        float fx = u * tex->w - 0.5f, fy = vv * tex->h - 0.5f; int ix = (int)std::floor(fx), iy = (int)std::floor(fy); float ax = fx - ix, ay = fy - iy;
                        float t4[4] = {0,0,0,0};
                        for (int k = 0; k < 4; ++k) { int sx = std::clamp(ix + (k&1), 0, tex->w-1), sy = std::clamp(iy + (k>>1), 0, tex->h-1); float wk = ((k&1)?ax:1-ax)*((k>>1)?ay:1-ay);
                            for (int c = 0; c < 4; ++c) t4[c] += wk * tex->rgba[(sy*tex->w+sx)*4+c] / 255.f; }
                        for (int c = 0; c < 4; ++c) col[c] *= t4[c];
                    }
                    float a = col[3]; float* d = &fb[(y * W + x) * 3];
                    for (int c = 0; c < 3; ++c) d[c] = d[c] * (1 - a) + col[c] * a;
                }
            }
        }
    }
    std::string ppm = std::string(path) + ".ppm";
    FILE* f = fopen(ppm.c_str(), "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
    for (float x : fb) fputc((int)std::clamp(x * 255.f + 0.5f, 0.f, 255.f), f);
    fclose(f);
}

static void loadTheme(const char* path) {
    ImGuiStyle& st = ImGui::GetStyle();
    std::ifstream f(path); std::string line;
    std::regex col(R"(colors\[ImGuiCol_(\w+)\]\s*=\s*ImVec4\(([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?,\s*([-\d.]+)f?\))");
    std::regex fl(R"(style\.(\w+)\s*=\s*([-\d.]+)f?;)");
    std::regex v2(R"(style\.(\w+)\s*=\s*ImVec2\(([-\d.]+)f?,\s*([-\d.]+)f?\))");
    while (std::getline(f, line)) {
        std::smatch m;
        if (std::regex_search(line, m, col)) {
            for (int i = 0; i < ImGuiCol_COUNT; ++i) if (m[1] == ImGui::GetStyleColorName(i)) st.Colors[i] = ImVec4(std::stof(m[2]), std::stof(m[3]), std::stof(m[4]), std::stof(m[5]));
        } else if (std::regex_search(line, m, v2)) {
            ImVec2 v(std::stof(m[2]), std::stof(m[3]));
            if (m[1] == "WindowPadding") st.WindowPadding = v; else if (m[1] == "FramePadding") st.FramePadding = v; else if (m[1] == "ItemSpacing") st.ItemSpacing = v; else if (m[1] == "ItemInnerSpacing") st.ItemInnerSpacing = v;
        } else if (std::regex_search(line, m, fl)) {
            float v = std::stof(m[2]);
            if (m[1] == "WindowRounding") st.WindowRounding = v; else if (m[1] == "ChildRounding") st.ChildRounding = v; else if (m[1] == "FrameRounding") st.FrameRounding = v;
            else if (m[1] == "PopupRounding") st.PopupRounding = v; else if (m[1] == "GrabRounding") st.GrabRounding = v; else if (m[1] == "WindowBorderSize") st.WindowBorderSize = v; else if (m[1] == "FrameBorderSize") st.FrameBorderSize = v;
        }
    }
}

static bool g_editorBg = false;
static void frame(const char* shot = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(W, H); io.DeltaTime = 1.f / 60;
    io.AddMousePosEvent(mx, my); io.AddMouseButtonEvent(0, down); io.AddMouseButtonEvent(2, mdown);
    for (auto& k : keys) io.AddKeyEvent(k.first, k.second); keys.clear();
    ImGui::NewFrame();
    if (g_editorBg) { // stand-in for the WZX editor behind the live windows
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), ImVec2(W,H), IM_COL32(16,19,22,255));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), ImVec2(W,36), IM_COL32(21,26,30,255));
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(14,10), IM_COL32(200,208,216,255), "WZX Engine  -  éditeur (vue Scene)   [fenêtres ci-dessous : document « exemple » affiché dans l'éditeur]");
    }
    wzui::designer().draw(1.0f);
    wzui::designer().drawLiveDocs(1.0f);
    ImGui::Render();
    handleTextures(ImGui::GetDrawData());
    if (shot) raster(ImGui::GetDrawData(), shot);
}
static void frames(int n) { for (int i = 0; i < n; ++i) frame(); }
static void moveTo(float x, float y, int steps = 8) { float sx = mx, sy = my; for (int i = 1; i <= steps; ++i) { mx = sx + (x - sx) * i / steps; my = sy + (y - sy) * i / steps; frame(); } }
static void click(float x, float y) { moveTo(x, y, 2); down = true; frame(); ImGuiContext& g=*GImGui; printf("click %.0f,%.0f hoveredWin=%s activeId=%u hoveredId=%u\n", x,y, g.HoveredWindow?g.HoveredWindow->Name:"-", g.ActiveId, g.HoveredId); down = false; frame(); }
static void drag(float x0, float y0, float x1, float y1) { moveTo(x0, y0, 2); down = true; frame(); { ImGuiContext& g=*GImGui; printf("drag press %.0f,%.0f hoveredWin=%s activeId=%u dragdrop=%d\n", x0,y0, g.HoveredWindow?g.HoveredWindow->Name:"-", g.ActiveId, (int)g.DragDropActive);} frame(); moveTo(x1, y1, 12); frame(); down = false; frame(); frame(); }
static const wzui::DrawCtx::Hit* hitOf(const std::string& id) {
    auto& d = wzui::designer().doc();
    std::vector<wzui::Node*> st; for (auto& w : d.windows) st.push_back(&w);
    int uid = 0; while (!st.empty()) { auto* n = st.back(); st.pop_back(); if (n->id == id) uid = n->uid; for (auto& c : n->children) st.push_back(&c); }
    for (auto& h : wzui::designer().lastCanvas().hits) if (h.uid == uid) return &h;
    return nullptr;
}
static std::string readFile(const std::string& p) { std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr; io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    if (!io.Fonts->AddFontFromFileTTF("/usr/share/fonts/opentype/inter/Inter-Regular.otf", 15.0f)) io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    loadTheme(argc > 2 ? argv[2] : "Blue 01.theme");
    std::system(("rm -rf " + out + "/ui").c_str());
    wzui::designer().setFolder(out + "/ui");
    wzui::designer().toggle();
    frames(4);

    // 1. click the slider on the canvas -> selected, properties shown
    auto* h = hitOf("intensite"); printf("intensite hit: %d\n", h != nullptr);
    click(h->rect.GetCenter().x, h->rect.GetCenter().y);
    printf("selected id: %s\n", wzui::designer().doc().find(wzui::designer().selected())->id.c_str());
    frame((out + "/1_selection").c_str());

    // 2. drag a Button from the palette into the toolbar Row (between Play and Stop)
    ImRect pb = wzui::designer().paletteRect(wzui::Kind::Button);
    auto* stop = hitOf("stop");
    float tx = stop->rect.Min.x + 4, ty = stop->rect.GetCenter().y;
    moveTo(pb.GetCenter().x, pb.GetCenter().y, 2); down = true; frame(); frame(); moveTo(tx, ty, 12); frame((out + "/2_glisser_palette").c_str());
    down = false; frame(); frame();
    auto& bar = wzui::designer().doc().windows[1].children[0];
    printf("toolbar row children:"); for (auto& c : bar.children) printf(" %s", c.id.c_str()); printf("\n");
    keys = {{ImGuiMod_Ctrl, true}, {ImGuiKey_Z, true}}; frame(); keys = {{ImGuiKey_Z, false}, {ImGuiMod_Ctrl, false}}; frame();
    printf("after undo: %zu children\n", wzui::designer().doc().windows[1].children[0].children.size());
    keys = {{ImGuiMod_Ctrl, true}, {ImGuiKey_Y, true}}; frame(); keys = {{ImGuiKey_Y, false}, {ImGuiMod_Ctrl, false}}; frame();
    printf("after redo: %zu children\n", wzui::designer().doc().windows[1].children[0].children.size());

    // 3. move the Inspector window by dragging its title bar, and move the 'reset' button out of its row into the window
    auto* win = hitOf("inspecteur");
    drag(win->rect.Min.x + 60, win->rect.Min.y + 8, win->rect.Min.x + 100, win->rect.Min.y + 48);
    printf("after drag selected=%s win rect %.0f,%.0f\n", wzui::designer().doc().find(wzui::designer().selected()) ? wzui::designer().doc().find(wzui::designer().selected())->id.c_str() : "-", win->rect.Min.x, win->rect.Min.y);
    printf("inspecteur pos: %.0f,%.0f\n", wzui::designer().doc().windows[0].x, wzui::designer().doc().windows[0].y);
    frames(30); // autosave
    std::string saved = readFile(out + "/ui/exemple.wzui.json");
    printf("saved has bouton between play/stop: %d, x moved: %d\n", (int)(saved.find("\"id\": \"button\"") != std::string::npos), (int)(saved.find("\"x\": 40") != std::string::npos));

    // 4. Claude edits the file on disk -> hot reload
    std::string edited = saved; size_t p = edited.find("\"text\": \"Appliquer\""); edited.replace(p, 19, "\"text\": \"Valider (Claude)\"");
    { std::ofstream f(out + "/ui/exemple.wzui.json"); f << edited; }
    std::system(("touch -d '+2 seconds' " + out + "/ui/exemple.wzui.json").c_str());
    for (int i = 0; i < 40; ++i) frame();
    auto* ap = hitOf("appliquer");
    click(ap->rect.GetCenter().x, ap->rect.GetCenter().y);
    printf("reloaded text: %s\n", wzui::designer().doc().find(wzui::designer().selected())->text.c_str());
    frame((out + "/3_recharge_claude").c_str());

    // 5. show the document live in the editor, designer closed
    wzui::designer().doc().showInEditor = true;
    wzui::designer().toggle(); g_editorBg = true; mx = 1500; my = 800;
    frames(3);
    frame((out + "/4_dans_editeur").c_str());
    std::system(("cp " + out + "/ui/exemple.wzui.json " + out + "/exemple.wzui.json").c_str());

    // 6. "Création Item": the default amber button, an ellipse drawn with the tool, a hover look, the Tester mode
    g_editorBg = false;
    wzui::designer().toggle();
    wzui::designer().setMode(1);
    frames(4);
    auto& ce = wzui::designer().items();
    printf("component: %s, %zu layers\n", ce.component().name.c_str(), ce.component().layers.size());
    ce.select(ce.component().layers[0].uid);
    frames(2);
    frame((out + "/5_creation_item").c_str());
    ce.setTool(wzui::Tool::Ellipse);
    ImVec2 a = ce.toScreen(ImVec2(12, 12)), b = ce.toScreen(ImVec2(28, 28));
    drag(a.x, a.y, b.x, b.y);
    auto& ly = ce.component().layers;
    printf("after draw: %zu layers, last=%s %.0f,%.0f %.0fx%.0f tool=%d undo=%zu\n", ly.size(), wzui::shapeName(ly.back().shape), ly.back().x, ly.back().y, ly.back().w, ly.back().h, (int)ce.tool(), ce.undoCount());
    ly.back().look[wzui::StNormal].fill.a = ImVec4(1, 0.95f, 0.85f, 1);
    ly.back().look[wzui::StNormal].shadow = true;
    ly.back().look[wzui::StNormal].shadowBlur = 3;
    // move the text to make room for the icon (drag on the canvas)
    ImVec2 t0 = ce.toScreen(ImVec2(70, 20));
    ce.select(0); frame();
    ImVec2 t1 = ce.toScreen(ImVec2(78, 20));
    drag(t0.x, t0.y, t1.x, t1.y);
    printf("text layer x after drag: %.0f (selected %d)\n", ly[1].x, ce.selected());
    ce.setState(wzui::StHover);
    ce.select(ly.back().uid);
    frames(3);
    frame((out + "/6_etat_survol").c_str());
    ce.setTest(true);
    ImVec2 c = ce.toScreen(ImVec2(100, 20));
    moveTo(c.x, c.y, 4);
    frames(2);
    frame((out + "/7_tester_survol").c_str());
    click(c.x, c.y);
    printf("test click: %s\n", ce.lastClick().c_str());
    ce.setTest(false);
    frames(40);   // autosave
    std::string comp = readFile(out + "/ui/components/bouton_ambre.wzcomp.json");
    printf("saved component has ellipse: %d\n", (int)(comp.find("\"ellipse\"") != std::string::npos));

    // 7. the component used in an interface, then live in the editor
    wzui::designer().setMode(0);
    frames(2);
    auto& doc = wzui::designer().doc();
    wzui::Node cn = doc.makeNode(wzui::Kind::Component);
    cn.id = "valider"; cn.text = "Valider"; cn.bind = "validate"; cn.x = 0; cn.y = 0; cn.w = 160; cn.h = 44;
    doc.windows[1].children.insert(doc.windows[1].children.begin(), cn);
    doc.windows[1].children[1].y = 56;
    doc.windows[1].h = 150;
    wzui::designer().saveNow();
    wzui::designer().select(doc.windows[1].children[0].uid);
    frames(3);
    frame((out + "/8_composant_dans_interface").c_str());
    int validated = 0;
    wzui::designer().bindings.actions["validate"] = [&] { ++validated; };
    wzui::designer().toggle(); g_editorBg = true;
    frames(3);
    // find the live window and hover / click the component
    ImGuiWindow* lw = nullptr;
    for (ImGuiWindow* w : GImGui->Windows) if (std::string(w->Name).find("barre") != std::string::npos) lw = w;
    if (lw) {
        ImVec2 pc(lw->ContentRegionRect.Min.x + 80, lw->ContentRegionRect.Min.y + 22);
        moveTo(pc.x, pc.y, 4);
        frame((out + "/9_editeur_survol").c_str());
        click(pc.x, pc.y);
    }
    printf("live window: %d, validate called: %d\n", lw != nullptr, validated);
    return 0;
}
