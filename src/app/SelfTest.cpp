// Editor self-test (--self-test report.txt): what the Basic Function branch added, checked in one run. Every check
// writes a PASS / FAIL line, then the editor quits (exit code 1 when a check failed). tools\selftest.bat runs it.
//   - logic: manipulators and Advanced Scale (ManipulatorsTest.inl), settings saved and read back;
//   - interface: scripted mouse and keys go through Dear ImGui like real input (they are applied after the OS ones):
//     shortcuts, buttons and menus found by their ImGui id or label (the item hooks of Dear ImGui, IMGUI_ENABLE_TEST_ENGINE
//     in build.bat), the view cube, click and rectangle selection, manipulator and Advanced Scale drags, marking menu;
//   - rendering: viewport images read back and measured: editor grid and its options, wireframe, selection glow and its
//     coverage, MSAA x4 / x8 / x16, path traced view with the editor overlay, the selection overlay of SelectionFx;
//   - robustness: anti-aliasing changes, window resize, GPU times, validation layer messages (VP3D_DEBUG).
#define IMGUI_ENABLE_TEST_ENGINE   // declarations of the item hooks in imgui_internal.h
#include "App.h"
#include "GameHost.h"
#include "imgui_internal.h"
#include <cstdarg>
#include <deque>
#include <filesystem>
#include <functional>
#include <unordered_map>

// --------------------------------------------------------------------------- Dear ImGui item hooks

namespace {

struct UiItem { ImRect bb; std::string label; int frame = -1; };
std::unordered_map<ImGuiID, UiItem> g_items;   // the widgets submitted, by id: rectangle and label (this frame)
int g_uiFrame = 0;

} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    UiItem& it = g_items[id];
    it.bb = bb;
    it.frame = g_uiFrame;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    auto it = g_items.find(id);
    if (it != g_items.end() && label) it->second.label = label;
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

namespace app {

namespace {

std::string fmt(const char* f, ...) {
    char t[512];
    va_list a;
    va_start(a, f);
    vsnprintf(t, sizeof t, f, a);
    va_end(a);
    return t;
}

// RGBA8 image read back from the GPU
struct Img {
    std::vector<uint8_t> px;
    uint32_t w = 0, h = 0;
    bool ok() const { return w > 0 && h > 0 && px.size() >= (size_t)w * h * 4; }
    const uint8_t* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    float lum(int x, int y) const { const uint8_t* p = at(x, y); return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }
};

struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // pixels, x1 / y1 excluded

// pixels whose channels differ by more than thr (in the rectangle)
int diffCount(const Img& a, const Img& b, int thr, const Rect* r = nullptr) {
    if (!a.ok() || !b.ok() || a.w != b.w || a.h != b.h) return -1;
    Rect all{0, 0, (int)a.w, (int)a.h};
    const Rect& q = r ? *r : all;
    int n = 0;
    for (int y = std::max(0, q.y0); y < std::min((int)a.h, q.y1); ++y)
        for (int x = std::max(0, q.x0); x < std::min((int)a.w, q.x1); ++x) {
            const uint8_t *p = a.at(x, y), *s = b.at(x, y);
            if (std::abs(p[0] - s[0]) > thr || std::abs(p[1] - s[1]) > thr || std::abs(p[2] - s[2]) > thr) ++n;
        }
    return n;
}

double meanAbsDiff(const Img& a, const Img& b, const std::vector<uint8_t>* skip = nullptr) {
    if (!a.ok() || !b.ok() || a.w != b.w || a.h != b.h) return 1e9;
    double s = 0;
    size_t n = 0;
    for (size_t i = 0; i < (size_t)a.w * a.h; ++i) {
        if (skip && (*skip)[i]) continue;
        for (int c = 0; c < 3; ++c) s += std::abs(a.px[i * 4 + c] - b.px[i * 4 + c]);
        n += 3;
    }
    return n ? s / n : 0;
}

int maxAbsDiff(const Img& a, const Img& b) {
    if (!a.ok() || !b.ok() || a.w != b.w || a.h != b.h) return 255;
    int m = 0;
    for (size_t i = 0; i < (size_t)a.w * a.h * 4; ++i) if ((i & 3) != 3) m = std::max(m, std::abs(a.px[i] - b.px[i]));
    return m;
}

// edges of an image: pixels with a strong step to their right or lower neighbour, grown by `grow` pixels
std::vector<uint8_t> edgeMask(const Img& im, float step, int grow) {
    std::vector<uint8_t> m((size_t)im.w * im.h, 0), g(m.size(), 0);
    for (uint32_t y = 0; y + 1 < im.h; ++y)
        for (uint32_t x = 0; x + 1 < im.w; ++x) {
            const float l = im.lum(x, y);
            if (std::fabs(im.lum(x + 1, y) - l) > step || std::fabs(im.lum(x, y + 1) - l) > step) m[(size_t)y * im.w + x] = 1;
        }
    for (int y = 0; y < (int)im.h; ++y)
        for (int x = 0; x < (int)im.w; ++x) {
            if (!m[(size_t)y * im.w + x]) continue;
            for (int dy = -grow; dy <= grow; ++dy)
                for (int dx = -grow; dx <= grow; ++dx) {
                    const int u = x + dx, v = y + dy;
                    if (u >= 0 && v >= 0 && u < (int)im.w && v < (int)im.h) g[(size_t)v * im.w + u] = 1;
                }
        }
    return g;
}

// sum of the squared luminance steps in the mask: a hard edge (one step of s) weighs s², the same edge spread over two
// pixels by the anti-aliasing only s² / 2
double edgeEnergy(const Img& im, const std::vector<uint8_t>& mask) {
    double e = 0;
    for (uint32_t y = 0; y + 1 < im.h; ++y)
        for (uint32_t x = 0; x + 1 < im.w; ++x) {
            if (!mask[(size_t)y * im.w + x]) continue;
            const double l = im.lum(x, y), dx = im.lum(x + 1, y) - l, dy = im.lum(x, y + 1) - l;
            e += dx * dx + dy * dy;
        }
    return e;
}

} // namespace

// --------------------------------------------------------------------------- the test driver

struct App::SelfTest {
    App& a;
    explicit SelfTest(App& app) : a(app) {}

    std::string report, group;
    int fails = 0, passes = 0;
    struct Step { std::string group; std::function<int()> run; };   // run: frames to wait before the next step, < 0 to run again
    std::vector<Step> steps;
    size_t next = 0;
    int wait = 0, polls = 0;
    // scripted input, applied just before ImGui::NewFrame (after the OS input: it wins)
    float mx = -1, my = -1;
    int mods = 0;   // 1 Shift, 2 Ctrl, 4 Alt
    std::deque<std::function<void(ImGuiIO&)>> events;   // one per frame
    // values kept between the steps
    Img img[8];
    std::vector<uint8_t> masks[3];
    Camera cam0;
    std::wstring graphFile;
    std::string graphBytes;
    std::map<std::string, double> v;
    bool flag = false;

    void add(const std::string& g, std::function<int()> f) { steps.push_back({g, std::move(f)}); }
    void check(const std::string& name, bool ok, const std::string& info = "") {
        report += std::string(ok ? "PASS " : "FAIL ") + "[" + group + "] " + name + (info.empty() ? "" : "  " + info) + "\n";
        if (ok) ++passes;
        else ++fails;
    }
    void note(const std::string& text) { report += "     [" + group + "] " + text + "\n"; }

    // ---- input
    void input() {
        ++g_uiFrame;
        ImGuiIO& io = ImGui::GetIO();
        if (!events.empty()) {
            auto e = std::move(events.front());
            events.pop_front();
            e(io);
        }
        io.AddKeyEvent(ImGuiMod_Shift, (mods & 1) != 0);
        io.AddKeyEvent(ImGuiMod_Ctrl, (mods & 2) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mods & 4) != 0);
        if (mx >= 0) {
            io.AddMousePosEvent(mx, my);
            // the backend reports the window under the real cursor (WindowFromPoint): the scripted one is over the editor
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
        }
    }
    int queued() { return (int)events.size() + 1; }
    int moveTo(float x, float y, int m = 0) {
        events.push_back([=](ImGuiIO&) { mx = x; my = y; mods = m; });
        events.push_back([](ImGuiIO&) {});
        return queued();
    }
    float lastClickX = -1e6f, lastClickY = -1e6f;
    int click(float x, float y, int button = 0, int m = 0) {
        // a second click on the same spot right after the first one is a double-click (pivot + focus in the view): a
        // little aside, past the double-click distance of Dear ImGui (6 pixels)
        if (std::hypot(x - lastClickX, y - lastClickY) < 7.0f) x += 7.0f;
        lastClickX = x;
        lastClickY = y;
        events.push_back([=](ImGuiIO&) { mx = x; my = y; mods = m; });
        events.push_back([=](ImGuiIO& io) { io.AddMouseButtonEvent(button, true); });
        events.push_back([=](ImGuiIO& io) { io.AddMouseButtonEvent(button, false); });
        events.push_back([=](ImGuiIO&) { mods = 0; });
        return queued();
    }
    int key(ImGuiKey k, int m = 0) {
        events.push_back([=](ImGuiIO&) { mods = m; });
        events.push_back([=](ImGuiIO& io) { io.AddKeyEvent(k, true); });
        events.push_back([=](ImGuiIO& io) { io.AddKeyEvent(k, false); });
        events.push_back([=](ImGuiIO&) { mods = 0; });
        return queued();
    }
    // drag with a button; midMods: modifiers from half way (Alt during an Advanced Scale drag); hold: a key held from
    // the press (V, or the tool key of a marking menu: then held before the press)
    int drag(float x0, float y0, float x1, float y1, int button = 0, int m = 0, int n = 8, int midMods = -1, ImGuiKey hold = ImGuiKey_None) {
        events.push_back([=](ImGuiIO& io) { mx = x0; my = y0; mods = m; if (hold != ImGuiKey_None) io.AddKeyEvent(hold, true); });
        events.push_back([=](ImGuiIO& io) { io.AddMouseButtonEvent(button, true); });
        for (int i = 1; i <= n; ++i)
            events.push_back([=](ImGuiIO&) {
                mx = x0 + (x1 - x0) * i / n;
                my = y0 + (y1 - y0) * i / n;
                if (midMods >= 0 && i == n / 2) mods = midMods;
                if (i == n / 2) flag = flag || a.m_mm.open;   // (marking menu: open during the gesture)
            });
        events.push_back([=](ImGuiIO& io) { io.AddMouseButtonEvent(button, false); });
        events.push_back([=](ImGuiIO& io) { mods = 0; if (hold != ImGuiKey_None) io.AddKeyEvent(hold, false); });
        return queued();
    }
    // a widget of this frame, by its ImGui label (the visible part or the whole label, "##id" for an id only)
    bool item(const std::string& label, ImRect& r) {
        const UiItem* best = nullptr;
        for (auto& kv : g_items) {
            const UiItem& it = kv.second;
            if (it.frame != g_uiFrame) continue;
            std::string vis = it.label;
            const size_t k = vis.find("##");
            if (k != std::string::npos && k > 0) vis = vis.substr(0, k);
            if (it.label != label && vis != label) continue;
            if (!best || it.bb.GetArea() < best->bb.GetArea()) best = &it;
        }
        if (!best) return false;
        r = best->bb;
        return true;
    }
    int clickItem(const std::string& label, int m = 0) {
        ImRect r;
        if (!item(label, r)) {
            check("widget on screen: " + label, false);
            return 2;
        }
        return click(r.GetCenter().x, r.GetCenter().y, 0, m);
    }

    // ---- scene helpers
    int obj(const char* name) const {
        for (size_t i = 0; i < a.m_scene.objects.size(); ++i) if (a.m_scene.objects[i].name == name) return (int)i;
        return -1;
    }
    float2 scr(const float3& p) const { float2 s(-1e6f, -1e6f); a.project(p, s); return s; }
    float2 centre(int o) const {
        const Object& ob = a.m_scene.objects[o];
        float3 mn, mx2;
        a.aabbOf(ob, ob.position, a.objRot(ob), mn, mx2);
        return scr((mn + mx2) * 0.5f);
    }
    // the object on screen (viewport pixels), grown by pad
    Rect objRect(int o, int pad) const {
        const Object& ob = a.m_scene.objects[o];
        float3 mn, mx2;
        a.aabbOf(ob, ob.position, a.objRot(ob), mn, mx2);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int k = 0; k < 8; ++k) {
            const float2 s = px(scr(float3((k & 1) ? mx2.x : mn.x, (k & 2) ? mx2.y : mn.y, (k & 4) ? mx2.z : mn.z)));
            x0 = std::min(x0, s.x); y0 = std::min(y0, s.y); x1 = std::max(x1, s.x); y1 = std::max(y1, s.y);
        }
        return Rect{(int)x0 - pad, (int)y0 - pad, (int)x1 + pad + 1, (int)y1 + pad + 1};
    }
    float2 px(float2 s) const {   // screen -> viewport pixel
        return float2((s.x - a.m_rcView.x) * a.m_vpPx.w / a.m_rcView.w, (s.y - a.m_rcView.y) * a.m_vpPx.h / a.m_rcView.h);
    }
    Img view() { Img i; a.m_renderer.readViewport(i.px, i.w, i.h); return i; }
    Img window() { Img i; a.m_renderer.readBackBuffer(i.px, i.w, i.h); return i; }
    void select(std::vector<int> v) {
        a.select(-1);
        if (v.empty()) return;
        a.select(v[0]);
        a.m_multi.assign(v.begin() + 1, v.end());
        a.markDirty();
    }
    std::vector<int> sel() const { return a.selectionAll(); }
    bool selIs(std::vector<int> want) const {
        std::vector<int> s = a.selectionAll();
        std::sort(s.begin(), s.end());
        std::sort(want.begin(), want.end());
        return s == want;
    }
    std::string selText() const {
        std::string t;
        for (int i : a.selectionAll()) t += (t.empty() ? "" : ", ") + a.m_scene.objects[i].name;
        return "selection: " + (t.empty() ? std::string("none") : t);
    }
    // a fresh Concept scene, saved state (no "Save changes?"), raster view, standard tools
    void fresh() {
        a.setAdvScale(false);
        a.m_advCorners = false;
        a.loadScene(SCENE_CONCEPT);
        a.m_savedSig = a.wzxSceneJson(false);
        a.m_docDirty = false;
        if (a.renderMode() != 0) a.setRenderMode(0);
        a.m_msaa = 4;
        a.m_rs.selectionStyle = 1;
        a.m_turntable = false;
        a.m_magnet = false;
        a.m_axisMode = 0;
        a.m_stepMove = 1.0f;
        a.m_stepRotate = 15.0f;
        a.m_stepScale = 0.1f;
        a.m_showGizmo = true;
        a.m_showGridOptions = false;
        a.m_showAdvSettings = false;
        a.m_undo.clear();
        th::settings().grid = GridOptions();
        th::settings().advScale = AdvScaleOptions();
    }
    // a point of the view on the empty background, away from the overlays
    bool emptyPoint(float2& p) const {
        for (float fy = 0.16f; fy < 0.5f; fy += 0.03f)
            for (float fx = 0.30f; fx < 0.70f; fx += 0.04f) {
                const float x = a.m_rcView.x + a.m_rcView.w * fx, y = a.m_rcView.y + a.m_rcView.h * fy;
                float3 o, d;
                a.cameraRay(x, y, o, d);
                if (pick(a.m_scene, o, d).object < 0) { p = float2(x, y); return true; }
            }
        return false;
    }
    // Advanced Scale: screen point of a handle of the box (sides sx, sy, sz in -1, 0, 1)
    bool advHandle(int sx, int sy, int sz, float2& p, int* index = nullptr) {
        AdvBox b;
        if (!a.advScaleBox(b)) return false;
        const int s[3] = {sx, sy, sz};
        float3 w = b.c;
        for (int k = 0; k < 3; ++k) w = w + b.ax[k] * (s[k] * b.h[k]);
        if (index) *index = (sx + 1) * 9 + (sy + 1) * 3 + (sz + 1);
        return a.project(w, p);
    }

    // an overlay (grid, wireframe, glow) seen in an image with noise (path tracing, probes): the change between `base`
    // and `with` on the pixels of masks[k] (where it shows in the raster view) must stand out of the change elsewhere
    void overlayCheck(const std::string& name, int k, const Img& base, const Img& with, const Rect& r) {
        const std::vector<uint8_t>& m = masks[k];
        double on = 0, off = 0;
        int nOn = 0, nOff = 0;
        if (base.ok() && with.ok() && base.w == with.w && base.h == with.h && m.size() == (size_t)base.w * base.h)
            for (int y = std::max(0, r.y0); y < std::min((int)base.h, r.y1); ++y)
                for (int x = std::max(0, r.x0); x < std::min((int)base.w, r.x1); ++x) {
                    const size_t i = (size_t)y * base.w + x;
                    double d = 0;
                    for (int c = 0; c < 3; ++c) d += std::abs(with.px[i * 4 + c] - base.px[i * 4 + c]);
                    if (m[i]) { on += d / 3; ++nOn; }
                    else { off += d / 3; ++nOff; }
                }
        on = nOn ? on / nOn : 0;
        off = nOff ? off / nOff : 0;
        check(name, nOn > 100 && on > off * 2 + 4, fmt("change %.1f on its %d pixels, %.1f elsewhere (noise)", on, nOn, off));
    }
    // two clicks in a row on the same spot (Dear ImGui: a double-click)
    int doubleClick(float x, float y) {
        events.push_back([=](ImGuiIO&) { mx = x; my = y; mods = 0; });
        for (int k = 0; k < 2; ++k) {
            events.push_back([](ImGuiIO& io) { io.AddMouseButtonEvent(0, true); });
            events.push_back([](ImGuiIO& io) { io.AddMouseButtonEvent(0, false); });
        }
        lastClickX = x;
        lastClickY = y;
        return queued();
    }

    void build();
    void frame();
    void finish();
};

void App::selfTestFrame() {
    if (m_opt.selfTest.empty()) return;
    if (!m_selfTest) {
        if (m_frameCounter < 6) return;   // the interface laid out first
        m_selfTest = std::make_shared<SelfTest>(*this);
        SelfTest& T = *m_selfTest;
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;   // every scripted event in its frame
        m_gui.beforeNewFrame = [this] { if (m_selfTest) m_selfTest->input(); };
        m_themeTestOnly = true;   // nothing saved into wzx_settings.ini
        T.report = "WZX Engine editor self-test\nGPU: " + m_renderer.adapterName() +
                   fmt("\nwindow %u x %u, viewport %d x %d, DPI scale %.2f\n", m_renderer.width(), m_renderer.height(), m_vpPx.w, m_vpPx.h, m_dpi);
        T.build();
        return;
    }
    m_selfTest->frame();
}

void App::SelfTest::frame() {
    if (wait > 0) { --wait; return; }
    while (next < steps.size()) {
        group = steps[next].group;
        const int w = steps[next].run();
        if (w < 0) {   // waiting for something (path tracing samples): the same step next frame
            if (++polls > 1200) {
                check("step finished in time", false, fmt("step %d", (int)next));
                polls = 0;
                ++next;
            }
            return;
        }
        polls = 0;
        ++next;
        if (w > 0) { wait = w - 1; return; }
    }
    finish();
}

void App::SelfTest::finish() {
    group = "validation";
    std::vector<std::string> msgs;
    int errors = 0, warnings = 0;
    if (a.m_renderer.takeDebugMessages(msgs, errors, warnings)) {
        check("DirectX 12 validation layer: no error", errors == 0, fmt("%d errors, %d warnings", errors, warnings));
        for (size_t i = 0; i < msgs.size() && i < 12; ++i) note(msgs[i]);
    } else note("DirectX 12 validation layer not running (VP3D_DEBUG not set, or the Windows optional feature \"Graphics Tools\" not installed)");
    report += fails ? fmt("FAILED %d of %d\n", fails, fails + passes) : fmt("ALL PASSED (%d checks)\n", passes);
    writeFileSafe(a.m_opt.selfTest, report);
    a.m_exitCode = fails ? 1 : 0;
    a.m_running = false;
    steps.clear();
    next = 0;
}

void App::SelfTest::build() {
    // ================================================================= logic
    add("logic", [this] {
        fresh();
        int f = 0;
        const std::string rep = a.gizmoLogicTests(f);
        size_t p = 0;
        while (p < rep.size()) {
            size_t e = rep.find('\n', p);
            if (e == std::string::npos) e = rep.size();
            const std::string line = rep.substr(p, e - p);
            if (line.rfind("PASS ", 0) == 0) check(line.substr(5), true);
            else if (line.rfind("FAIL ", 0) == 0) check(line.substr(5), false);
            else if (!line.empty()) note(line);
            p = e + 1;
        }
        fresh();
        return 4;
    });

    // ================================================================= settings saved and read back
    add("settings", [this] {
        UiSettings& s = th::settings();
        const UiSettings saved = s;
        GridOptions g;
        g.visible = false; g.sceneSpacing = false; g.every = 2.5f; g.subdivisions = 4; g.extent = 12.0f;
        g.lines = false; g.subdivLines = false; g.axes = false; g.thickAxes = false; g.arrows = false; g.numbers = true; g.fade = false;
        g.widthGrid = 2.25f; g.widthSub = 0.75f; g.widthAxis = 3.5f; g.opacity = 0.6f;
        g.colorGrid = ImVec4(0.1f, 0.2f, 0.3f, 0.4f); g.colorSub = ImVec4(0.5f, 0.6f, 0.7f, 0.8f);
        g.colorX = ImVec4(0.9f, 0.1f, 0.2f, 0.3f); g.colorY = ImVec4(0.2f, 0.9f, 0.1f, 0.5f); g.colorZ = ImVec4(0.1f, 0.3f, 0.9f, 0.7f);
        AdvScaleOptions o;
        o.colorEdge = ImVec4(0.9f, 0.8f, 0.7f, 1.0f); o.colorFace = ImVec4(0.1f, 0.2f, 0.3f, 1.0f);
        o.colorEdgeHandle = ImVec4(0.4f, 0.5f, 0.6f, 1.0f); o.colorCorner = ImVec4(0.7f, 0.6f, 0.5f, 1.0f);
        o.handleSize = 11.0f; o.interactDist = 17.0f; o.snapRadius = 60.0f; o.flatThreshold = 0.12f; o.menuOffset = 42.0f; o.showHints = false;
        s.grid = g;
        s.advScale = o;
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const std::wstring file = std::wstring(tmp) + L"wzx_selftest_settings.ini";
        th::saveSettings(file);
        s.grid = GridOptions();
        s.advScale = AdvScaleOptions();
        th::loadSettings(file);
        auto same4 = [](const ImVec4& p, const ImVec4& q) { return std::fabs(p.x - q.x) + std::fabs(p.y - q.y) + std::fabs(p.z - q.z) + std::fabs(p.w - q.w) < 0.004f; };
        const GridOptions& r = s.grid;
        const bool gridOk = r.visible == g.visible && r.sceneSpacing == g.sceneSpacing && r.every == g.every && r.subdivisions == g.subdivisions &&
                            r.extent == g.extent && r.lines == g.lines && r.subdivLines == g.subdivLines && r.axes == g.axes && r.thickAxes == g.thickAxes &&
                            r.arrows == g.arrows && r.numbers == g.numbers && r.fade == g.fade && r.widthGrid == g.widthGrid && r.widthSub == g.widthSub &&
                            r.widthAxis == g.widthAxis && r.opacity == g.opacity && same4(r.colorGrid, g.colorGrid) && same4(r.colorSub, g.colorSub) &&
                            same4(r.colorX, g.colorX) && same4(r.colorY, g.colorY) && same4(r.colorZ, g.colorZ);
        check("Grid Options saved and read back (wzx_settings.ini, grid.*)", gridOk);
        const AdvScaleOptions& q = s.advScale;
        const bool advOk = same4(q.colorEdge, o.colorEdge) && same4(q.colorFace, o.colorFace) && same4(q.colorEdgeHandle, o.colorEdgeHandle) &&
                           same4(q.colorCorner, o.colorCorner) && q.handleSize == o.handleSize && q.interactDist == o.interactDist &&
                           q.snapRadius == o.snapRadius && q.flatThreshold == o.flatThreshold && q.menuOffset == o.menuOffset && q.showHints == o.showHints;
        check("Advanced Scale settings saved and read back (advScale.*)", advOk);
        // values out of range in a hand-edited file are brought back into range
        {
            FILE* f = _wfopen(file.c_str(), L"w");
            if (f) {
                fputs("grid.widthGrid=100\ngrid.subdivisions=0\ngrid.opacity=-3\ngrid.every=0\nadvScale.handleSize=1\nadvScale.flatThreshold=9\nadvScale.snapRadius=100000\n", f);
                fclose(f);
            }
            th::loadSettings(file);
            check("settings out of range are clamped", s.grid.widthGrid == 8.0f && s.grid.subdivisions == 1 && s.grid.opacity == 0.0f && s.grid.every > 0.0f &&
                                                         s.advScale.handleSize == 3.0f && s.advScale.flatThreshold == 0.5f && s.advScale.snapRadius == 200.0f,
                  fmt("width %.2f, subdivisions %d, opacity %.2f, handle %.1f, flat %.2f", s.grid.widthGrid, s.grid.subdivisions, s.grid.opacity,
                      s.advScale.handleSize, s.advScale.flatThreshold));
        }
        DeleteFileW(file.c_str());
        s = saved;
        return 1;
    });

    // ================================================================= keys
    add("keys", [this] { fresh(); return key(ImGuiKey_Q); });
    add("keys", [this] { check("Q: Select tool", a.m_tool == TOOL_SELECT); return key(ImGuiKey_W); });
    add("keys", [this] { check("W: Move tool", a.m_tool == TOOL_MOVE); return key(ImGuiKey_E); });
    add("keys", [this] { check("E: Rotate tool", a.m_tool == TOOL_ROTATE); return key(ImGuiKey_R); });
    add("keys", [this] { check("R: Scale tool", a.m_tool == TOOL_SCALE); return key(ImGuiKey_T, 4); });
    add("keys", [this] {
        check("Alt+T: Advanced Scale on", a.m_advScale);
        check("Alt+T: the turntable (T) not started", !a.m_turntable);
        check("Advanced Scale on: no standard manipulator", !a.gizmoShown());
        return key(ImGuiKey_T, 4);
    });
    add("keys", [this] { check("Alt+T again: Advanced Scale off", !a.m_advScale); return key(ImGuiKey_T, 4); });
    add("keys", [this] { return key(ImGuiKey_W); });
    add("keys", [this] { check("W leaves Advanced Scale for Move", !a.m_advScale && a.m_tool == TOOL_MOVE); return key(ImGuiKey_T, 4); });
    add("keys", [this] { return key(ImGuiKey_E); });
    add("keys", [this] { check("E leaves Advanced Scale for Rotate", !a.m_advScale && a.m_tool == TOOL_ROTATE); return key(ImGuiKey_Escape); });
    add("keys", [this] {
        check("Esc: deselect", a.m_selected < 0);
        a.loadScene(SCENE_CORNELL);
        return 3;
    });
    add("keys", [this] {
        a.m_savedSig = a.wzxSceneJson(false);   // nothing to save: F4 loads at once
        a.m_docDirty = false;
        return key(ImGuiKey_F4);
    });
    add("keys", [this] {
        if (a.m_confirmAction) a.m_confirmAction = 0;
        check("F4: Concept scene", a.m_sceneId == SCENE_CONCEPT, fmt("scene %d", a.m_sceneId));
        const int cube = obj("Cube");
        check("Concept: the cube selected, Wireframe Override", cube >= 0 && a.m_selected == cube && a.m_rs.wireframe == 2);
        return 1;
    });

    // ================================================================= viewport header buttons
    add("buttons", [this] { fresh(); a.m_tool = TOOL_SELECT; return 3; });
    add("buttons", [this] { return clickItem("atlMove"); });
    add("buttons", [this] { check("Move button", a.m_tool == TOOL_MOVE); return clickItem("atlRotate"); });
    add("buttons", [this] { check("Rotate button", a.m_tool == TOOL_ROTATE); return clickItem("atlScale"); });
    add("buttons", [this] { check("Scale button", a.m_tool == TOOL_SCALE); return clickItem("atlAdvScale"); });
    add("buttons", [this] { check("Advanced Scale button: on", a.m_advScale); return clickItem("atlAdvScale"); });
    add("buttons", [this] { check("Advanced Scale button: off", !a.m_advScale); return clickItem("atlAdvScale"); });
    add("buttons", [this] { return clickItem("atlMove"); });
    add("buttons", [this] { check("Move button leaves Advanced Scale", !a.m_advScale && a.m_tool == TOOL_MOVE); v["magnet"] = a.m_magnet; return clickItem("atlSnap"); });
    add("buttons", [this] { check("Snapping button", a.m_magnet != (v["magnet"] != 0)); return clickItem("atlSnap"); });
    add("buttons", [this] { check("Snapping button again", a.m_magnet == (v["magnet"] != 0)); v["stats"] = a.m_showStats; return clickItem("atlStats"); });
    add("buttons", [this] { check("Statistics button", a.m_showStats != (v["stats"] != 0)); return clickItem("atlStats"); });
    add("buttons", [this] { check("Statistics button again", a.m_showStats == (v["stats"] != 0)); return clickItem("atlGrid"); });
    add("buttons", [this] { check("Grid button: hides the grid", !th::settings().grid.visible); return clickItem("atlGrid"); });
    add("buttons", [this] { check("Grid button: shows it again", th::settings().grid.visible); return clickItem("atlGrid", 1); });
    add("buttons", [this] {
        check("Shift+click on the Grid button: Grid Options window", a.m_showGridOptions && th::settings().grid.visible);
        return clickItem("Close");
    });
    add("buttons", [this] { check("Grid Options: Close", !a.m_showGridOptions); return 1; });

    // ================================================================= menus inside the view
    auto menu = [this](const char* pill, const char* entry, std::function<void()> then, const char* sub = nullptr) {
        add("menus", [this, pill] { return clickItem(pill); });
        if (sub) add("menus", [this, sub] { return clickItem(sub); });
        add("menus", [this, entry] { return clickItem(entry); });
        add("menus", [then] { then(); return 1; });
    };
    add("menus", [this] { fresh(); return 3; });
    menu("vmCamera", "Top", [this] {
        float3 r, u, f;
        a.m_scene.camera.basis(r, u, f);
        check("Camera menu: Top", a.m_viewMode == 5 && f.y < -0.99f, fmt("view %d, forward y %.3f", a.m_viewMode, f.y));
    });
    menu("vmCamera", "Perspective", [this] { check("Camera menu: Perspective", a.m_viewMode == 0); });
    menu("vmCamera", "Turntable", [this] { check("Camera menu: Turntable on", a.m_turntable); });
    menu("vmCamera", "Turntable", [this] { check("Camera menu: Turntable off", !a.m_turntable); });
    menu("vmCamera", "Depth of Field", [this] { check("Camera menu: Depth of Field on", a.m_scene.camera.dof); });
    menu("vmCamera", "Depth of Field", [this] { check("Camera menu: Depth of Field off", !a.m_scene.camera.dof); });
    menu("vmDisplay", "Manipulators", [this] { check("Display menu: Manipulators hidden", !a.m_showGizmo); });
    menu("vmDisplay", "Manipulators", [this] { check("Display menu: Manipulators shown", a.m_showGizmo); });
    menu("vmDisplay", "Statistics", [this] { v["stats2"] = a.m_showStats; });
    menu("vmDisplay", "Statistics", [this] { check("Display menu: Statistics toggled twice", a.m_showStats != (v["stats2"] != 0)); });
    menu("vmDisplay", "Wireframe", [this] { check("Display menu: Wireframe", a.m_rs.wireframe == 1); });
    menu("vmDisplay", "Shaded", [this] { check("Display menu: Shaded", a.m_rs.wireframe == 0); });
    menu("vmDisplay", "Wireframe Override", [this] { check("Display menu: Wireframe Override", a.m_rs.wireframe == 2); });
    menu("vmDisplay", "Grid", [this] { check("Display menu: Grid hides the grid", !th::settings().grid.visible); });
    menu("vmDisplay", "Grid", [this] { check("Display menu: Grid shows it", th::settings().grid.visible); });
    menu("vmDisplay", "Grid Options...", [this] { check("Display menu: Grid Options...", a.m_showGridOptions); });
    add("menus", [this] { return clickItem("Close"); });
    menu("vmDisplay", "Selection: Outline", [this] { check("Display menu: Selection Outline", a.m_rs.selectionStyle == 0); });
    menu("vmDisplay", "Selection: Blue Glow", [this] { check("Display menu: Selection Blue Glow", a.m_rs.selectionStyle == 1); });
    menu("vmDisplay", "MSAA x8", [this] { check("Display menu: Anti-aliasing MSAA x8", a.m_msaa == 8); }, "Anti-aliasing");
    add("menus", [this] { check("MSAA x8 in the renderer", a.m_renderer.msaaSamples() == 8, fmt("%u samples", a.m_renderer.msaaSamples())); return 1; });
    menu("vmDisplay", "MSAA x16", [this] { check("Display menu: Anti-aliasing MSAA x16", a.m_msaa == 16); }, "Anti-aliasing");
    add("menus", [this] {
        check("MSAA x16 in the renderer", a.m_renderer.msaaSamples() == 16,
              fmt("%u samples%s", a.m_renderer.msaaSamples(), a.m_renderer.msaaTemporal() ? " (8 x 2 frames)" : ""));
        return 1;
    });
    menu("vmDisplay", "Off", [this] { check("Display menu: Anti-aliasing Off", a.m_msaa == 1); }, "Anti-aliasing");
    add("menus", [this] { check("anti-aliasing off in the renderer", a.m_renderer.msaaSamples() == 1); return 1; });
    menu("vmDisplay", "MSAA x4", [this] { check("Display menu: Anti-aliasing MSAA x4", a.m_msaa == 4); }, "Anti-aliasing");
    menu("vmRender", "Progressive", [this] { check("Renderer menu: Progressive (path tracing)", !a.m_rs.raster); });
    menu("vmRender", "Lit", [this] { check("Renderer menu: Lit", a.m_rs.raster && a.renderMode() == 0); });
    menu("vmAxes", "Object", [this] { check("Axes menu: Object", a.m_axisMode == 1); });
    menu("vmAxes", "Parent", [this] { check("Axes menu: Parent", a.m_axisMode == 2); });
    menu("vmAxes", "World", [this] { check("Axes menu: World", a.m_axisMode == 0); });
    menu("vmAxes", "Snapping always (else Shift)", [this] { check("Axes menu: Snapping always", a.m_magnet); });
    menu("vmAxes", "Snapping always (else Shift)", [this] { check("Axes menu: Snapping always off", !a.m_magnet); });
    add("menus", [this] { return clickItem("vmGrid"); });
    add("menus", [this] { check("Grid pill: hides the grid", !th::settings().grid.visible); return clickItem("vmGrid"); });
    add("menus", [this] { check("Grid pill: shows it", th::settings().grid.visible); return clickItem("vmGrid", 1); });
    add("menus", [this] { check("Shift+click on the Grid pill: Grid Options", a.m_showGridOptions && th::settings().grid.visible); return 1; });

    // ================================================================= Grid Options window
    add("grid options", [this] { return clickItem("##lines"); });
    add("grid options", [this] { check("Grid lines checkbox: off", !th::settings().grid.lines); return clickItem("##lines"); });
    add("grid options", [this] { check("Grid lines checkbox: on", th::settings().grid.lines); return clickItem("##fade"); });
    add("grid options", [this] { check("Fade checkbox: off", !th::settings().grid.fade); return clickItem("##fade"); });
    add("grid options", [this] { check("Fade checkbox: on", th::settings().grid.fade); return clickItem("Hide Grid"); });
    add("grid options", [this] { check("Hide Grid button", !th::settings().grid.visible); return clickItem("Show Grid"); });
    add("grid options", [this] {
        check("Show Grid button", th::settings().grid.visible);
        th::settings().grid.widthGrid = 3.0f;
        th::settings().grid.subdivisions = 5;
        return clickItem("Reset");
    });
    add("grid options", [this] {
        const GridOptions& g = th::settings().grid;
        check("Reset: default thickness and subdivisions, the grid still shown", g.widthGrid == 1.0f && g.subdivisions == 1 && g.visible);
        return clickItem("Close");
    });
    add("grid options", [this] { check("Close", !a.m_showGridOptions); return 1; });

    // ================================================================= view cube
    // letters X / Y / Z: the view from that axis; faces: the view from that face (the camera on its side)
    auto cubePoint = [this](const float3& dir, float k, float2& p) {
        ImRect r;
        if (!item("##viewCube", r)) return false;
        float3 cr, cu, cf;
        a.m_scene.camera.basis(cr, cu, cf);
        const ImVec2 c = r.GetCenter();
        const float hs = r.GetWidth() * 11.0f / 96.0f;   // half size of the cube (AtelierUI.cpp: 11 of a 96 wide box)
        p = float2(c.x + dot(dir, cr) * hs * k, c.y - dot(dir, cu) * hs * k);
        return true;
    };
    add("view cube", [this] { fresh(); return 3; });
    for (int axis = 0; axis < 3; ++axis) {
        add("view cube", [this, axis, cubePoint] {
            float2 p;
            const float3 n(axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f, axis == 2 ? 1.0f : 0.0f);
            if (!cubePoint(n, 3.6f, p)) { check("view cube on screen", false); return 1; }
            return click(p.x, p.y);
        });
        add("view cube", [this, axis] {
            float3 r, u, f;
            a.m_scene.camera.basis(r, u, f);
            static const char* names[3] = {"X", "Y", "Z"};
            check(std::string("letter ") + names[axis] + ": the view along " + names[axis], f[axis] < -0.99f, fmt("forward %.3f %.3f %.3f", f.x, f.y, f.z));
            return 1;
        });
    }
    for (int face = 0; face < 6; ++face) {
        add("view cube", [this, face, cubePoint] {
            const int ax = face / 2;
            const float sg = (face & 1) ? -1.0f : 1.0f;
            float3 n(0, 0, 0);
            n[ax] = sg;
            // a camera that sees this face well (oblique, so that it is one of the three faces shown)
            Camera& c = a.m_scene.camera;
            bool found = false;
            for (int yi = 0; yi < 8 && !found; ++yi)
                for (int pi = 0; pi < 2 && !found; ++pi) {
                    c.yaw = 0.4f + yi * kPi / 4;
                    c.pitch = pi ? -0.55f : 0.55f;
                    float3 r, u, f;
                    c.basis(r, u, f);
                    found = dot(n, f) < -0.45f;
                }
            a.cameraChanged();
            float2 p;
            if (!found || !cubePoint(n, 1.0f, p)) { check("view cube face on screen", false); return 1; }
            return click(p.x, p.y);
        });
        add("view cube", [this, face] {
            const int ax = face / 2;
            const float sg = (face & 1) ? -1.0f : 1.0f;
            float3 r, u, f;
            a.m_scene.camera.basis(r, u, f);
            static const char* names[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            check(std::string("face ") + names[face] + ": the camera looks at it", f[ax] * sg < -0.99f, fmt("forward %.3f %.3f %.3f", f.x, f.y, f.z));
            return 1;
        });
    }

    // ================================================================= selection with the mouse
    add("selection", [this] { fresh(); a.m_tool = TOOL_SELECT; cam0 = a.m_scene.camera; return 3; });
    add("selection", [this] {
        float2 p;
        if (!emptyPoint(p)) { check("an empty point in the view", false); return 1; }
        v["ex"] = p.x;
        v["ey"] = p.y;
        return click(p.x, p.y);
    });
    add("selection", [this] {
        check("click in the empty view: deselect", a.m_selected < 0 && a.m_multi.empty(), selText());
        const float2 c = centre(obj("Cube"));
        return click(c.x, c.y);
    });
    add("selection", [this] {
        check("click on the cube: selected", selIs({obj("Cube")}), selText());
        const float2 c = centre(obj("Sphere"));
        return click(c.x, c.y, 0, 1);
    });
    add("selection", [this] {
        check("Shift+click on the sphere: added", selIs({obj("Cube"), obj("Sphere")}), selText());
        const float2 c = centre(obj("Cube"));
        return click(c.x, c.y, 0, 1);
    });
    add("selection", [this] {
        check("Shift+click on the cube again: toggled off", selIs({obj("Sphere")}), selText());
        const float2 c = centre(obj("Cube"));
        return click(c.x, c.y, 0, 3);
    });
    add("selection", [this] {
        check("Ctrl+Shift+click on the cube: added", selIs({obj("Cube"), obj("Sphere")}), selText());
        const float2 c = centre(obj("Cube"));
        return click(c.x, c.y, 0, 3);
    });
    add("selection", [this] {
        check("Ctrl+Shift+click on a selected object: kept", selIs({obj("Cube"), obj("Sphere")}), selText());
        const float2 c = centre(obj("Sphere"));
        return click(c.x, c.y, 0, 2);
    });
    add("selection", [this] {
        check("Ctrl+click on the sphere: removed", selIs({obj("Cube")}), selText());
        // rectangle from the empty background around the three shapes
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const char* n : {"Cube", "Sphere", "Cylinder"}) {
            const Rect r = objRect(obj(n), 0);
            x0 = std::min(x0, (float)r.x0); y0 = std::min(y0, (float)r.y0); x1 = std::max(x1, (float)r.x1); y1 = std::max(y1, (float)r.y1);
        }
        const float ox = a.m_rcView.x, oy = a.m_rcView.y;
        return drag(ox + x0 - 25, oy + y0 - 25, ox + x1 + 25, oy + y1 + 25);
    });
    add("selection", [this] {
        check("rectangle: the three shapes", selIs({obj("Cube"), obj("Sphere"), obj("Cylinder")}), selText());
        return click((float)v["ex"], (float)v["ey"]);
    });
    add("selection", [this] {
        check("click in the empty view again: deselect", a.m_selected < 0);
        a.select(obj("Cube"));
        const Rect r = objRect(obj("Sphere"), 0);
        const float ox = a.m_rcView.x, oy = a.m_rcView.y;
        return drag(ox + r.x0 - 20, oy + r.y0 - 20, ox + r.x1 + 20, oy + r.y1 + 20, 0, 1);
    });
    add("selection", [this] {
        check("Shift + rectangle: added to the selection", selIs({obj("Cube"), obj("Sphere")}), selText());
        a.select(obj("Cube"));
        return key(ImGuiKey_W);
    });
    add("selection", [this] {
        // Move tool: a click on another object (off the manipulator) selects it
        const float2 c = centre(obj("Sphere"));
        check("Move tool: the sphere is off the manipulator", a.gizmoHit(c.x, c.y) == MH_NONE);
        return click(c.x, c.y);
    });
    add("selection", [this] {
        check("Move tool: click selects (any tool)", selIs({obj("Sphere")}), selText());
        v["yaw"] = a.m_scene.camera.yaw;
        const float2 c = centre(obj("Cube"));
        return drag(c.x, c.y, c.x + 120, c.y, 0, 4);
    });
    add("selection", [this] {
        check("Alt + left drag: orbit, the selection kept", std::fabs(a.m_scene.camera.yaw - (float)v["yaw"]) > 0.05f && selIs({obj("Sphere")}),
              fmt("yaw %.3f -> %.3f", v["yaw"], a.m_scene.camera.yaw));
        v["yaw"] = a.m_scene.camera.yaw;
        float2 p;
        emptyPoint(p);
        return drag(p.x, p.y, p.x - 100, p.y, 1);
    });
    add("selection", [this] {
        check("right drag: orbit", std::fabs(a.m_scene.camera.yaw - (float)v["yaw"]) > 0.05f);
        v["dist"] = a.m_scene.camera.distance;
        float2 p;
        emptyPoint(p);
        events.push_back([=](ImGuiIO&) { mx = p.x; my = p.y; });
        events.push_back([](ImGuiIO& io) { io.AddMouseWheelEvent(0, 2); });
        return queued();
    });
    add("selection", [this] {
        check("wheel: zoom", a.m_scene.camera.distance < (float)v["dist"] * 0.9f, fmt("distance %.2f -> %.2f", v["dist"], a.m_scene.camera.distance));
        return key(ImGuiKey_Q);
    });
    add("selection", [this] {
        const float3 t = a.m_scene.camera.target;
        v["tx"] = t.x; v["ty"] = t.y; v["tz"] = t.z;
        float2 p;
        emptyPoint(p);
        return drag(p.x, p.y, p.x + 90, p.y + 40, 2);
    });
    add("selection", [this] {
        const float3 t = a.m_scene.camera.target;
        check("Select tool, middle drag: pan", length(t - float3((float)v["tx"], (float)v["ty"], (float)v["tz"])) > 0.05f);
        a.m_scene.camera = cam0;   // the view of the start
        a.cameraChanged();
        return 2;
    });
    add("selection", [this] {
        const float2 c = centre(obj("Cylinder"));
        return doubleClick(c.x, c.y);
    });
    add("selection", [this] {
        // double-click: the camera turns around the clicked point (on the cylinder)
        const Object& cy = a.m_scene.objects[obj("Cylinder")];
        const float3 t = a.m_scene.camera.target;
        const float dxz = std::hypot(t.x - cy.position.x, t.z - cy.position.z);
        check("double-click on the cylinder: pivot on it", dxz < 1.3f && std::fabs(t.y - cy.position.y) < 1.4f, fmt("pivot %.2f %.2f %.2f", t.x, t.y, t.z));
        return 1;
    });

    // ================================================================= manipulators with the mouse
    auto handleDrag = [this](int h, float pxl, int button, int m) {
        const float2 s = a.gizmoHandlePoint(h), d = a.gizmoHandleDir(h, s);
        return drag(s.x, s.y, s.x + d.x * pxl, s.y + d.y * pxl, button, m);
    };
    add("manipulators", [this] { fresh(); a.select(obj("Cube")); return key(ImGuiKey_W); });
    add("manipulators", [this] {
        const float2 s = a.gizmoHandlePoint(MH_Z);
        return moveTo(s.x, s.y);
    });
    add("manipulators", [this, handleDrag] {
        check("hover: the Z axis under the mouse", a.m_gzHover == MH_Z, fmt("hover %d", a.m_gzHover));
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        v["x"] = p.x; v["y"] = p.y; v["z"] = p.z;
        v["undo"] = (double)a.m_undo.size();
        return handleDrag(MH_X, 80, 0, 0);
    });
    add("manipulators", [this] {
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        const float dx = p.x - (float)v["x"];
        check("drag of the X arrow: only X", dx > 0.05f && std::fabs(p.y - (float)v["y"]) < 1e-4f && std::fabs(p.z - (float)v["z"]) < 1e-4f,
              fmt("d=%.3f %.4f %.4f", dx, p.y - v["y"], p.z - v["z"]));
        check("one undo step for the drag", a.m_undo.size() == (size_t)v["undo"] + 1, fmt("%d", (int)a.m_undo.size()));
        return key(ImGuiKey_Z, 2);
    });
    add("manipulators", [this, handleDrag] {
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        check("Ctrl+Z: back", std::fabs(p.x - (float)v["x"]) < 1e-5f);
        return handleDrag(MH_X, 130, 0, 1);
    });
    add("manipulators", [this] {
        const float dx = a.m_scene.objects[obj("Cube")].position.x - (float)v["x"];
        check("Shift while dragging: whole steps", std::fabs(dx - std::round(dx)) < 1e-4f && std::fabs(dx) >= 1.0f, fmt("dx=%.4f", dx));
        v["x"] = a.m_scene.objects[obj("Cube")].position.x;
        return clickItem("atlSnap");
    });
    add("manipulators", [this, handleDrag] { return handleDrag(MH_X, 130, 0, 0); });
    add("manipulators", [this] {
        const float dx = a.m_scene.objects[obj("Cube")].position.x - (float)v["x"];
        check("Snapping on: steps without Shift", a.m_magnet && std::fabs(dx - std::round(dx)) < 1e-4f && std::fabs(dx) >= 1.0f, fmt("dx=%.4f", dx));
        return clickItem("atlSnap");
    });
    add("manipulators", [this] {
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        v["x"] = p.x; v["y"] = p.y; v["z"] = p.z;
        float2 e;
        emptyPoint(e);
        check("an empty point off the manipulator", a.gizmoHit(e.x, e.y) == MH_NONE);
        return drag(e.x, e.y, e.x + 60, e.y - 30, 2);
    });
    add("manipulators", [this] {
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        check("middle drag anywhere: the last handle (X)", std::fabs(p.x - (float)v["x"]) > 1e-3f && std::fabs(p.y - (float)v["y"]) < 1e-4f &&
                                                               std::fabs(p.z - (float)v["z"]) < 1e-4f, fmt("d=%.3f %.4f %.4f", p.x - v["x"], p.y - v["y"], p.z - v["z"]));
        const float3 t = a.m_scene.camera.target;
        v["tx"] = t.x; v["ty"] = t.y; v["tz"] = t.z;
        v["x"] = p.x;
        float2 e;
        emptyPoint(e);
        return drag(e.x, e.y, e.x + 60, e.y + 30, 2, 4);
    });
    add("manipulators", [this] {
        const float3 t = a.m_scene.camera.target;
        check("Alt + middle drag: pan, the object stays",
              length(t - float3((float)v["tx"], (float)v["ty"], (float)v["tz"])) > 0.05f && std::fabs(a.m_scene.objects[obj("Cube")].position.x - (float)v["x"]) < 1e-5f);
        // middle drag pressed on the Y arrow: that handle, not the last used one (X)
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        v["x"] = p.x; v["y"] = p.y; v["z"] = p.z;
        const float2 s = a.gizmoHandlePoint(MH_Y);
        return drag(s.x, s.y, s.x + 5, s.y - 60, 2);
    });
    add("manipulators", [this] {
        const float3 p = a.m_scene.objects[obj("Cube")].position;
        check("middle drag on the Y arrow: along Y", std::fabs(p.y - (float)v["y"]) > 1e-3f && std::fabs(p.x - (float)v["x"]) < 1e-4f && std::fabs(p.z - (float)v["z"]) < 1e-4f,
              fmt("d=%.4f %.3f %.4f", p.x - v["x"], p.y - v["y"], p.z - v["z"]));
        return key(ImGuiKey_E);
    });
    add("manipulators", [this, handleDrag] { return handleDrag(MH_Y, 70, 0, 0); });
    add("manipulators", [this] {
        const Object& o = a.m_scene.objects[obj("Cube")];
        const float3 xa = qRot(a.objRot(o), float3(1, 0, 0));
        check("drag of the Y ring: turns around Y", std::fabs(xa.y) < 1e-4f && xa.x < 0.999f, fmt("x axis %.3f %.3f %.3f", xa.x, xa.y, xa.z));
        return key(ImGuiKey_R);
    });
    add("manipulators", [this, handleDrag] { return handleDrag(MH_CENTER, 50, 0, 0); });
    add("manipulators", [this] {
        const float3 s = a.m_scene.objects[obj("Cube")].scale;
        check("drag of the centre cube: uniform scale", s.x > 1.05f && std::fabs(s.x - s.y) < 1e-5f && std::fabs(s.x - s.z) < 1e-5f, fmt("s=%.3f %.3f %.3f", s.x, s.y, s.z));
        return 1;
    });

    // ================================================================= marking menu (hold the tool key + left button)
    auto gesture = [this](ImGuiKey k, float dx, float dy) {
        flag = false;
        const float cx = a.m_rcView.x + a.m_rcView.w * 0.5f, cy = a.m_rcView.y + a.m_rcView.h * 0.62f;
        events.push_back([=](ImGuiIO& io) { mx = cx; my = cy; mods = 0; io.AddKeyEvent(k, true); });
        return drag(cx, cy, cx + dx, cy + dy, 0, 0, 8);   // (the drag releases the key queued after it)
    };
    auto release = [this](ImGuiKey k) { events.push_back([=](ImGuiIO& io) { io.AddKeyEvent(k, false); }); return queued(); };
    add("marking menu", [this] { fresh(); a.select(obj("Cube")); a.m_axisMode = 1; return 2; });
    add("marking menu", [this, gesture] { return gesture(ImGuiKey_W, 0, -90); });
    add("marking menu", [this, release] { return release(ImGuiKey_W); });
    add("marking menu", [this] {
        check("W + left button: the menu opens", flag);
        check("W menu, up: World", a.m_axisMode == 0 && !a.m_mm.open);
        return 1;
    });
    add("marking menu", [this, gesture] { return gesture(ImGuiKey_W, 100, 0); });
    add("marking menu", [this, release] { return release(ImGuiKey_W); });
    add("marking menu", [this] { check("W menu, right: Step 1 -> 0.5", a.m_stepMove == 0.5f, fmt("step %.2f", a.m_stepMove)); return 1; });
    add("marking menu", [this, gesture] {
        Object& o = a.m_scene.objects[obj("Cube")];
        o.rot = qAxisAngle(float3(0, 1, 0), 0.5f);
        o.useRot = true;
        return gesture(ImGuiKey_E, 70, 70);
    });
    add("marking menu", [this, release] { return release(ImGuiKey_E); });
    add("marking menu", [this] {
        check("E menu, down right: Reset Rotation", std::fabs(a.objRot(a.m_scene.objects[obj("Cube")]).w) > 0.99999f);
        return 1;
    });
    add("marking menu", [this, gesture] { return gesture(ImGuiKey_R, -100, 0); });
    add("marking menu", [this, release] { return release(ImGuiKey_R); });
    add("marking menu", [this] { check("R menu, left: Advanced Scale", a.m_advScale); a.setAdvScale(false); return 1; });
    add("marking menu", [this, gesture] { return gesture(ImGuiKey_Q, 0, 100); });
    add("marking menu", [this, release] { return release(ImGuiKey_Q); });
    add("marking menu", [this] { check("Q menu, down: Deselect", a.m_selected < 0); a.m_stepMove = 1.0f; return 1; });

    // ================================================================= Advanced Scale with the mouse
    add("advanced scale", [this] { fresh(); a.select(obj("Cube")); a.m_tool = TOOL_SELECT; return key(ImGuiKey_T, 4); });
    add("advanced scale", [this] {
        float2 p;
        int h = -1;
        advHandle(1, 0, 0, p, &h);
        v["h"] = h;
        return moveTo(p.x, p.y);
    });
    add("advanced scale", [this] {
        check("hover: the +X face handle", a.m_advHover == (int)v["h"], fmt("hover %d", a.m_advHover));
        AdvBox b;
        a.advScaleBox(b);
        v["minx"] = b.c.x - b.h.x;
        v["undo"] = (double)a.m_undo.size();
        float2 p, c;
        advHandle(1, 0, 0, p);
        a.project(b.c, c);
        const float l = std::max(std::hypot(p.x - c.x, p.y - c.y), 1.0f);
        return drag(p.x, p.y, p.x + (p.x - c.x) / l * 60, p.y + (p.y - c.y) / l * 60);
    });
    add("advanced scale", [this] {
        const float3 s = a.m_scene.objects[obj("Cube")].scale;
        AdvBox b;
        a.advScaleBox(b);
        check("drag of the +X face: X only", s.x > 1.05f && std::fabs(s.y - 1) < 1e-4f && std::fabs(s.z - 1) < 1e-4f, fmt("s=%.3f %.3f %.3f", s.x, s.y, s.z));
        check("drag of the +X face: the -X face stays", std::fabs(b.c.x - b.h.x - (float)v["minx"]) < 1e-3f);
        check("one undo step", a.m_undo.size() == (size_t)v["undo"] + 1);
        return key(ImGuiKey_Z, 2);
    });
    add("advanced scale", [this] {
        check("Ctrl+Z: back", length(a.m_scene.objects[obj("Cube")].scale - float3(1, 1, 1)) < 1e-5f);
        float2 p, c;
        AdvBox b;
        a.advScaleBox(b);
        advHandle(0, 1, 0, p);
        a.project(b.c, c);
        const float l = std::max(std::hypot(p.x - c.x, p.y - c.y), 1.0f);
        return drag(p.x, p.y, p.x + (p.x - c.x) / l * 50, p.y + (p.y - c.y) / l * 50, 0, 1);
    });
    add("advanced scale", [this] {
        const float3 s = a.m_scene.objects[obj("Cube")].scale;
        check("Shift: uniform", s.y > 1.02f && std::fabs(s.x - s.y) < 1e-4f && std::fabs(s.z - s.y) < 1e-4f, fmt("s=%.3f %.3f %.3f", s.x, s.y, s.z));
        a.m_scene.objects[obj("Cube")].scale = float3(1, 1, 1);
        AdvBox b;
        a.advScaleBox(b);
        v["cx"] = b.c.x; v["cy"] = b.c.y; v["cz"] = b.c.z;
        float2 p, c;
        advHandle(1, 0, 0, p);
        a.project(b.c, c);
        const float l = std::max(std::hypot(p.x - c.x, p.y - c.y), 1.0f);
        return drag(p.x, p.y, p.x + (p.x - c.x) / l * 50, p.y + (p.y - c.y) / l * 50, 0, 0, 8, 4);   // Alt from half way
    });
    add("advanced scale", [this] {
        AdvBox b;
        a.advScaleBox(b);
        const float3 s = a.m_scene.objects[obj("Cube")].scale;
        check("Alt during the drag: both sides, the centre stays", s.x > 1.02f && length(b.c - float3((float)v["cx"], (float)v["cy"], (float)v["cz"])) < 1e-3f,
              fmt("s.x=%.3f", s.x));
        a.m_scene.objects[obj("Cube")].scale = float3(1, 1, 1);
        float2 p;
        int h = -1;
        advHandle(1, 1, 1, p, &h);
        v["h"] = h;
        return moveTo(p.x, p.y);
    });
    add("advanced scale", [this] {
        check("corner handle hidden without Ctrl", a.m_advHover != (int)v["h"]);
        return moveTo(mx, my, 2);
    });
    add("advanced scale", [this] {
        check("Ctrl: corner handle under the mouse", a.m_advHover == (int)v["h"], fmt("hover %d", a.m_advHover));
        mods = 0;
        // V: towards the right side of the cylinder (the face goes to the level of its vertex)
        const Object& cy = a.m_scene.objects[obj("Cylinder")];
        const Affine T = cy.transform();
        AdvBox b;
        a.advScaleBox(b);
        float best = -1e30f;
        float3 w;
        for (const Vertex& vx : a.m_scene.meshes[cy.mesh].vertices) {
            const float3 q = T.point(vx.pos);
            if (dot(q, b.ax[0]) + 0.2f * q.y > best) { best = dot(q, b.ax[0]) + 0.2f * q.y; w = q; }
        }
        float2 p, q;
        advHandle(1, 0, 0, p);
        a.project(w, q);
        v["vx"] = dot(w, b.ax[0]);
        return drag(p.x, p.y, q.x, q.y, 0, 0, 10, -1, ImGuiKey_V);
    });
    add("advanced scale", [this] {
        AdvBox b;
        a.advScaleBox(b);
        const float face = dot(b.c, b.ax[0]) + b.h.x;
        check("V: the face snaps to the vertex under the mouse", a.m_adv.snapped || std::fabs(face - (float)v["vx"]) < 1e-3f,
              fmt("face %.4f, vertex %.4f", face, v["vx"]));
        check("V: exact level of the vertex", std::fabs(face - (float)v["vx"]) < 1e-3f);
        a.m_scene.objects[obj("Cube")].scale = float3(1, 1, 1);
        a.m_scene.objects[obj("Cube")].position = float3(0, 1, 0);
        return clickItem("##advCorners");
    });
    add("advanced scale", [this] { check("Corner Handles checkbox: on", a.m_advCorners); return clickItem("##advCorners"); });
    add("advanced scale", [this] { check("Corner Handles checkbox: off", !a.m_advCorners); return clickItem("##advPlay"); });
    add("advanced scale", [this] { check("Show in Game View checkbox: off", !a.m_advShowInPlay); return clickItem("##advPlay"); });
    add("advanced scale", [this] {
        check("Show in Game View checkbox: on", a.m_advShowInPlay);
        img[0] = window();
        th::settings().advScale.showHints = false;
        return 3;
    });
    add("advanced scale", [this] {
        // the modifier hints at the bottom of the view (Ctrl, Shift, Alt, V)
        img[1] = window();
        // (the middle of the bottom strip: the menu at the left moves down when they are hidden)
        const Rect r{(int)(a.m_rcView.x + a.m_rcView.w * 0.3f), (int)(a.m_rcView.y + a.m_rcView.h - 40 * a.m_dpi), (int)(a.m_rcView.x + a.m_rcView.w * 0.7f),
                     (int)(a.m_rcView.y + a.m_rcView.h)};
        const int n = diffCount(img[0], img[1], 30, &r);
        check("modifier hints drawn at the bottom of the view", n > 100, fmt("%d pixels", n));
        th::settings().advScale.showHints = true;
        return 3;   // (the menu goes back up over the hints)
    });
    add("advanced scale", [this] { return clickItem("advSettings"); });
    add("advanced scale", [this] { check("settings button: the settings window", a.m_showAdvSettings); return clickItem("##aHints"); });
    add("advanced scale", [this] { check("settings: modifier hints off", !th::settings().advScale.showHints); return clickItem("Reset"); });
    add("advanced scale", [this] { check("settings: Reset", th::settings().advScale.showHints); return clickItem("Close"); });
    add("advanced scale", [this] {
        check("settings: Close", !a.m_showAdvSettings);
        select({obj("Cube"), obj("Cylinder")});
        return 3;
    });
    add("advanced scale", [this] { return clickItem("advPick"); });
    add("advanced scale", [this] {
        check("eyedropper button", a.m_advPicking);
        const float2 c = centre(obj("Cube"));
        return click(c.x, c.y);
    });
    add("advanced scale", [this] {
        a.outlinerIds();
        check("eyedropper: the cube gives the orientation", !a.m_advPicking && a.m_advRefId == a.m_scene.objects[obj("Cube")].id && selIs({obj("Cube"), obj("Cylinder")}));
        return clickItem("advWorld");
    });
    add("advanced scale", [this] { check("world button: back to the world axes", a.m_advRefId == 0); return 1; });
    add("advanced scale", [this] {
        // the box and its handles drawn over the view (window image, Select tool: no manipulator)
        a.select(obj("Cube"));
        a.m_tool = TOOL_SELECT;
        a.setAdvScale(false);
        return 3;
    });
    add("advanced scale", [this] { img[0] = window(); a.setAdvScale(true); return 3; });
    add("advanced scale", [this] {
        img[1] = window();
        Rect r = objRect(obj("Cube"), 30);
        r.x0 += (int)a.m_rcView.x; r.x1 += (int)a.m_rcView.x; r.y0 += (int)a.m_rcView.y; r.y1 += (int)a.m_rcView.y;
        const int n = diffCount(img[0], img[1], 20, &r);
        check("the box and its handles drawn around the cube", n > 150, fmt("%d pixels", n));
        a.m_game->requestPlay();   // the Play button
        return 3;
    });
    add("advanced scale", [this] {
        check("Play started", a.m_game->playing());
        check("Play: no handle drag of the box", !a.advScaleInput(true));
        a.m_game->requestPlay();
        return 3;
    });
    add("advanced scale", [this] { check("Play stopped", !a.m_game->playing()); a.setAdvScale(false); return 1; });

    // ================================================================= rendering: Concept scene
    add("concept scene", [this] {
        fresh();
        const int cube = obj("Cube"), sphere = obj("Sphere"), cyl = obj("Cylinder");
        int floor = -1;
        for (size_t i = 0; i < a.m_scene.objects.size(); ++i) if (a.m_scene.objects[i].part == "GROUND") floor = (int)i;
        check("Concept: cube, sphere, cylinder and floor", cube >= 0 && sphere >= 0 && cyl >= 0 && floor >= 0);
        if (floor >= 0) {
            const Material& m = a.m_scene.materials[a.m_scene.objects[floor].material];
            check("Concept: glossy floor (roughness 0.18, specular 0.9)", std::fabs(m.roughness - 0.18f) < 1e-4f && std::fabs(m.specular - 0.9f) < 1e-4f);
        }
        const Camera& c = a.m_scene.camera;
        check("Concept: camera of the reference (yaw 42, pitch 24, distance 14)",
              std::fabs(degrees(c.yaw) - 42.0f) < 0.01f && std::fabs(degrees(c.pitch) - 24.0f) < 0.01f && std::fabs(c.distance - 14.0f) < 1e-4f);
        a.loadScene(SCENE_CORNELL);
        check("leaving Concept: Wireframe Override off", a.m_rs.wireframe == 0);
        fresh();
        return 3;
    });

    // ================================================================= rendering: editor grid
    // (no selection: the glow animates; shaded: no wireframe)
    auto gridShot = [this](int slot, std::function<void(GridOptions&)> set) {
        add("grid", [this, set] { GridOptions& g = th::settings().grid; g = GridOptions(); set(g); a.markDirty(); return 3; });
        add("grid", [this, slot] { img[slot] = view(); return 1; });
    };
    add("grid", [this] { fresh(); a.select(-1); a.m_rs.wireframe = 0; return 2; });
    gridShot(0, [](GridOptions& g) { g.visible = false; });
    gridShot(1, [](GridOptions& g) { g.axes = false; });
    gridShot(2, [](GridOptions&) {});
    add("grid", [this] {
        const int n = diffCount(img[0], img[1], 6), total = (int)(img[0].w * img[0].h);
        v["n1"] = n;
        check("grid drawn", n > total / 200, fmt("%d pixels of %d", n, total));
        // on the floor: below the horizon (camera rays going down)
        int floorPx = 0, count = 0;
        for (uint32_t y = 0; y < img[0].h; y += 2)
            for (uint32_t x = 0; x < img[0].w; x += 2) {
                const uint8_t *p = img[0].at(x, y), *q = img[1].at(x, y);
                if (std::abs(p[0] - q[0]) <= 6 && std::abs(p[1] - q[1]) <= 6 && std::abs(p[2] - q[2]) <= 6) continue;
                float3 o, d;
                a.cameraRay(a.m_rcView.x + x * a.m_rcView.w / a.m_vpPx.w, a.m_rcView.y + y * a.m_rcView.h / a.m_vpPx.h, o, d);
                ++count;
                if (d.y < 0) ++floorPx;
            }
        check("grid lines on the floor only (without the axes)", count > 0 && floorPx >= count * 0.99, fmt("%d of %d", floorPx, count));
        // the three axes: changes along each projected axis
        const float L = a.m_rs.editorGridAxis;
        for (int ax = 0; ax < 3; ++ax) {
            float3 e0(0, 0, 0), e1(0, 0, 0);
            e0[ax] = ax == 1 ? 0.0f : -L * 0.9f;
            e1[ax] = L * 0.9f;
            float2 s0, s1;
            int on = 0;
            if (a.project(e0, s0) && a.project(e1, s1)) {
                const float2 p0 = px(s0), p1 = px(s1);
                for (int k = 0; k <= 400; ++k) {
                    const int x = (int)std::lround(p0.x + (p1.x - p0.x) * k / 400), y = (int)std::lround(p0.y + (p1.y - p0.y) * k / 400);
                    if (x < 2 || y < 2 || x >= (int)img[0].w - 2 || y >= (int)img[0].h - 2) continue;
                    bool changed = false;
                    for (int dy = -1; dy <= 1 && !changed; ++dy)
                        for (int dx = -1; dx <= 1 && !changed; ++dx) {
                            const uint8_t *p = img[1].at(x + dx, y + dy), *q = img[2].at(x + dx, y + dy);
                            changed = std::abs(p[0] - q[0]) > 8 || std::abs(p[1] - q[1]) > 8 || std::abs(p[2] - q[2]) > 8;
                        }
                    if (changed) ++on;
                }
            }
            static const char* names[3] = {"X", "Y", "Z"};
            check(std::string("grid axis ") + names[ax] + " drawn", on > 60, fmt("%d points of 401", on));
        }
        return 1;
    });
    gridShot(3, [](GridOptions& g) { g.axes = false; g.widthGrid = g.widthSub = 3.0f; });
    gridShot(4, [](GridOptions& g) { g.axes = false; g.subdivisions = 4; });
    gridShot(5, [](GridOptions& g) { g.axes = false; g.extent = 4.0f; });
    gridShot(6, [](GridOptions& g) { g.axes = false; g.fade = false; });
    gridShot(7, [](GridOptions& g) { g.opacity = 0.0f; });
    add("grid", [this] {
        const int n1 = (int)v["n1"], n3 = diffCount(img[0], img[3], 6), n4 = diffCount(img[0], img[4], 6), n5 = diffCount(img[0], img[5], 6),
                  n6 = diffCount(img[0], img[6], 6), n7 = diffCount(img[0], img[7], 6);
        check("line thickness 3 px: more pixels than 1 px", n3 > n1 * 1.6, fmt("%d vs %d", n3, n1));
        check("subdivisions 4: more lines", n4 > n1 * 1.3, fmt("%d vs %d", n4, n1));
        check("length and width 4: a smaller grid", n5 > 0 && n5 < n1 * 0.6, fmt("%d vs %d", n5, n1));
        check("fade off: more lines far away", n6 > n1, fmt("%d vs %d", n6, n1));
        check("opacity 0: no grid", n7 < 20, fmt("%d pixels", n7));
        // the extent: no grid pixel outside the 4 x 4 square centred on the origin
        int out = 0;
        for (uint32_t y = 0; y < img[0].h; ++y)
            for (uint32_t x = 0; x < img[0].w; ++x) {
                const uint8_t *p = img[0].at(x, y), *q = img[5].at(x, y);
                if (std::abs(p[0] - q[0]) <= 10 && std::abs(p[1] - q[1]) <= 10 && std::abs(p[2] - q[2]) <= 10) continue;
                float3 o, d;
                a.cameraRay(a.m_rcView.x + (x + 0.5f) * a.m_rcView.w / a.m_vpPx.w, a.m_rcView.y + (y + 0.5f) * a.m_rcView.h / a.m_vpPx.h, o, d);
                if (d.y >= 0) { ++out; continue; }
                const float3 hit = o + d * (-o.y / d.y);
                if (std::fabs(hit.x) > 2.3f || std::fabs(hit.z) > 2.3f) ++out;
            }
        check("length and width 4: nothing outside the square", out < 30, fmt("%d pixels outside", out));
        return 1;
    });
    // spacing of the Grid Options instead of the scene spacing (2 units in Concept)
    gridShot(3, [](GridOptions& g) { g.axes = false; g.sceneSpacing = false; g.every = 0.5f; });
    add("grid", [this] {
        const int n = diffCount(img[0], img[3], 6);
        check("grid lines every 0.5 instead of the scene spacing: denser", n > v["n1"] * 1.5, fmt("%d vs %d", n, (int)v["n1"]));
        return 1;
    });
    // the small arrows at the ends of the axes (EditorGrid.hlsl)
    gridShot(3, [](GridOptions& g) { g.arrows = false; });
    add("grid", [this] {
        int n = 0;
        const float L = a.m_rs.editorGridAxis;
        for (int ax = 0; ax < 3; ++ax) {
            float3 e(0, 0, 0);
            e[ax] = L;
            float2 s;
            if (!a.project(e, s)) continue;
            const float2 p = px(s);
            const Rect r{(int)p.x - 40, (int)p.y - 40, (int)p.x + 41, (int)p.y + 41};
            n += diffCount(img[2], img[3], 8, &r);
        }
        check("axis arrows drawn at the ends of the axes", n > 20, fmt("%d pixels", n));
        return 1;
    });
    // colour of the X axis: green instead of red
    gridShot(3, [](GridOptions& g) { g.colorX = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); });
    add("grid", [this] {
        int greener = 0, total = 0;
        float2 s0, s1;
        const float L = a.m_rs.editorGridAxis * 0.8f;
        if (a.project(float3(-L, 0, 0), s0) && a.project(float3(L, 0, 0), s1)) {
            const float2 p0 = px(s0), p1 = px(s1);
            for (int k = 0; k <= 300; ++k) {
                const int x = (int)std::lround(p0.x + (p1.x - p0.x) * k / 300), y = (int)std::lround(p0.y + (p1.y - p0.y) * k / 300);
                if (x < 0 || y < 0 || x >= (int)img[2].w || y >= (int)img[2].h) continue;
                ++total;
                const uint8_t *red = img[2].at(x, y), *green = img[3].at(x, y);
                if (green[1] - green[0] > red[1] - red[0] + 10) ++greener;
            }
        }
        check("X axis colour (Grid Options)", total > 0 && greener > total / 3, fmt("%d of %d points greener", greener, total));
        th::settings().grid = GridOptions();
        a.select(obj("Cube"));
        a.m_game->requestPlay();
        return 3;
    });
    add("grid", [this] {
        check("Play: no editor grid", a.m_game->playing() && a.m_rs.editorGrid == 0.0f);
        check("Play: no manipulator", !a.gizmoShown());
        check("Play: no selection overlay", a.m_fxStats.glints == 0);
        a.m_game->requestPlay();
        return 3;
    });
    add("grid", [this] { check("Stop: the grid back", !a.m_game->playing() && a.m_rs.editorGrid > 0.0f); return 1; });

    // ================================================================= rendering: wireframe display
    add("wireframe", [this] { fresh(); a.select(-1); th::settings().grid.visible = false; a.m_rs.wireframe = 0; a.markDirty(); return 3; });
    add("wireframe", [this] { img[0] = view(); a.m_rs.wireframe = 2; a.markDirty(); return 3; });
    add("wireframe", [this] { img[1] = view(); a.m_rs.wireframe = 1; a.markDirty(); return 3; });
    add("wireframe", [this] {
        img[2] = view();
        const Rect r = objRect(obj("Cube"), 0);
        const int area = (r.x1 - r.x0) * (r.y1 - r.y0), n2 = diffCount(img[0], img[1], 6, &r), n1 = diffCount(img[0], img[2], 6, &r);
        check("Wireframe Override: edges over the shaded cube", n2 > 150 && n2 < area / 2, fmt("%d of %d pixels", n2, area));
        check("Wireframe: the faces are not shaded", n1 > area / 4, fmt("%d of %d pixels", n1, area));
        // facing ratio: on the sphere, the lines facing the camera (centre) and the grazing ones (rim) differ
        const Object& sp = a.m_scene.objects[obj("Sphere")];
        float2 c, e;
        float3 cr, cu, cf;
        a.m_scene.camera.basis(cr, cu, cf);
        const float radius = 1.25f;
        if (a.project(sp.position, c) && a.project(sp.position + cr * radius, e)) {
            const float2 pc = px(c), pe = px(e);
            const float R = std::hypot(pe.x - pc.x, pe.y - pc.y);
            double in = 0, rim = 0;
            int nIn = 0, nRim = 0;
            for (int y = (int)(pc.y - R); y <= (int)(pc.y + R); ++y)
                for (int x = (int)(pc.x - R); x <= (int)(pc.x + R); ++x) {
                    if (x < 0 || y < 0 || x >= (int)img[0].w || y >= (int)img[0].h) continue;
                    const float r = std::hypot(x - pc.x, y - pc.y) / R;
                    const uint8_t *p = img[0].at(x, y), *q = img[1].at(x, y);
                    const int d = std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]);
                    if (d < 12) continue;   // a line pixel
                    if (r < 0.45f) { in += d; ++nIn; }
                    else if (r > 0.75f && r < 0.95f) { rim += d; ++nRim; }
                }
            in = nIn ? in / nIn : 0;
            rim = nRim ? rim / nRim : 0;
            check("Wireframe Override: facing-ratio gradient (sphere centre vs rim)", nIn > 20 && nRim > 20 && std::fabs(in - rim) > 0.2 * std::max(in, rim),
                  fmt("centre %.1f, rim %.1f (%s brighter)", in, rim, in > rim ? "centre" : "rim"));
        } else check("Wireframe Override: sphere on screen", false);
        return 1;
    });

    // ================================================================= rendering: selection glow
    add("selection glow", [this] { fresh(); a.select(-1); th::settings().grid.visible = false; a.m_rs.wireframe = 0; a.markDirty(); return 3; });
    add("selection glow", [this] { img[0] = view(); a.select(obj("Cube")); return 4; });
    add("selection glow", [this] {
        img[1] = view();
        const Rect around = objRect(obj("Cube"), 20);
        const int all = diffCount(img[0], img[1], 6), inside = diffCount(img[0], img[1], 6, &around);
        check("blue glow around the selected cube", all > 300, fmt("%d pixels", all));
        check("the glow stays at the outline", inside >= all * 0.97, fmt("%d of %d around the cube", inside, all));
        double b = 0, r = 0;
        int n = 0;
        for (uint32_t y = 0; y < img[0].h; ++y)
            for (uint32_t x = 0; x < img[0].w; ++x) {
                const uint8_t *p = img[0].at(x, y), *q = img[1].at(x, y);
                if (std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]) < 30) continue;
                r += q[0] - p[0];
                b += q[2] - p[2];
                ++n;
            }
        check("the glow is blue", n > 0 && b > r * 1.3 && b > 0, fmt("added blue %.1f, red %.1f", n ? b / n : 0.0, n ? r / n : 0.0));
        return 30;   // half a second
    });
    add("selection glow", [this] {
        img[2] = view();
        const Rect around = objRect(obj("Cube"), 20);
        check("the glow moves (light running along the outline)", diffCount(img[1], img[2], 2, &around) > 20);
        a.m_rs.selectionStyle = 0;
        a.markDirty();
        return 3;
    });
    add("selection glow", [this] {
        img[3] = view();
        double b = 0, r = 0;
        int n = 0;
        for (uint32_t y = 0; y < img[0].h; ++y)
            for (uint32_t x = 0; x < img[0].w; ++x) {
                const uint8_t *p = img[0].at(x, y), *q = img[3].at(x, y);
                if (std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]) < 30) continue;
                r += q[0] - p[0];
                b += q[2] - p[2];
                ++n;
            }
        check("Selection Outline style: a yellow outline", n > 100 && r > b, fmt("%d pixels, added red %.1f, blue %.1f", n, n ? r / n : 0.0, n ? b / n : 0.0));
        a.m_rs.selectionStyle = 1;
        a.select(obj("Sphere"));
        return 4;
    });
    add("selection glow", [this] {
        img[4] = view();
        const Rect around = objRect(obj("Sphere"), 20);
        const int all = diffCount(img[0], img[4], 6), inside = diffCount(img[0], img[4], 6, &around);
        check("the glow follows the selection (sphere)", all > 300 && inside >= all * 0.97, fmt("%d of %d around the sphere", inside, all));
        return 1;
    });

    // ================================================================= selection overlay (glints, creases, reflection)
    add("selection overlay", [this] { fresh(); a.select(obj("Cube")); return 3; });
    add("selection overlay", [this] {
        const FxStats s = a.m_fxStats;
        check("glints on the outline vertices", s.glints >= 4, fmt("%d glints", s.glints));
        check("outline in the floor reflection (glossy floor)", s.mirrorLines > 0, fmt("%d lines, %d glints", s.mirrorLines, s.mirrorGlints));
        for (Object& o : a.m_scene.objects)
            if (o.part == "GROUND") a.m_scene.materials[o.material].roughness = 0.9f;
        return 2;
    });
    add("selection overlay", [this] {
        check("no reflection on a rough floor", a.m_fxStats.mirrorLines == 0 && a.m_fxStats.mirrorGlints == 0);
        a.m_rs.selectionStyle = 0;
        return 2;
    });
    add("selection overlay", [this] {
        check("no glint with the Outline style", a.m_fxStats.glints == 0);
        a.m_rs.selectionStyle = 1;
        a.select(-1);
        return 2;
    });
    add("selection overlay", [this] {
        check("nothing without a selection", a.m_fxStats.glints == 0 && a.m_fxStats.mirrorLines == 0);
        const Object& c = a.m_scene.objects[obj("Cube")];
        const std::vector<SilEdge>& e = a.silhouetteEdges(c.mesh);
        int straight = 0;
        for (const SilEdge& s : e) {
            const float3 d = s.b - s.a;
            const int moving = (std::fabs(d.x) > 1e-4f) + (std::fabs(d.y) > 1e-4f) + (std::fabs(d.z) > 1e-4f);
            if (moving == 1) ++straight;
        }
        check("cube outline edges: its 12 edges (no diagonal)", e.size() == 12 && straight == 12, fmt("%d edges, %d along an axis", (int)e.size(), straight));
        return 1;
    });

    // ================================================================= anti-aliasing: selection coverage and edges
    auto covLevels = [this](int samples, const char* name) {
        add("anti-aliasing", [this, samples] { a.m_msaa = samples; a.markDirty(); return 4; });
        add("anti-aliasing", [this, samples, name] {
            std::vector<uint8_t> cov;
            uint32_t w = 0, h = 0;
            const bool ok = a.m_renderer.readSelectionCoverage(cov, w, h);
            int inside = 0, frac = 0, onLevel = 0;
            const int hw = samples >= 16 ? 8 : samples;
            for (uint8_t c : cov) {
                if (c >= 250) ++inside;
                else if (c > 5) {
                    ++frac;
                    const float step = 255.0f / std::max(hw, 1);
                    if (std::fabs(c - step * std::round(c / step)) <= 2.5f) ++onLevel;
                }
            }
            if (samples == 1) check(std::string("selection coverage, ") + name + ": all or nothing", ok && inside > 1000 && frac == 0, fmt("%d inside, %d partial", inside, frac));
            else {
                check(std::string("selection coverage, ") + name + ": partial pixels on the outline", ok && inside > 1000 && frac > 150, fmt("%d inside, %d partial", inside, frac));
                check(std::string("selection coverage, ") + name + ": steps of 1/" + std::to_string(hw), frac > 0 && onLevel >= frac * 0.95, fmt("%d of %d", onLevel, frac));
            }
            check(std::string("selection coverage, ") + name + ": the size of the view", w == (uint32_t)a.m_vpPx.w && h == (uint32_t)a.m_vpPx.h, fmt("%u x %u", w, h));
            return 1;
        });
    };
    add("anti-aliasing", [this] { fresh(); a.select(obj("Cube")); th::settings().grid.visible = false; a.m_rs.wireframe = 0; return 2; });
    covLevels(1, "off");
    covLevels(4, "x4");
    covLevels(8, "x8");
    covLevels(16, "x16");
    // geometry edges: Off, x4, x8, x16 (no selection, no grid)
    auto aaShot = [this](int samples, int slot) {
        add("anti-aliasing", [this, samples] { a.select(-1); a.m_msaa = samples; a.markDirty(); return 6; });
        add("anti-aliasing", [this, slot] { img[slot] = view(); return 1; });
    };
    aaShot(1, 0);
    aaShot(4, 1);
    aaShot(8, 2);
    aaShot(16, 3);
    add("anti-aliasing", [this] {
        const std::vector<uint8_t> m = edgeMask(img[0], 24.0f, 1), edges = edgeMask(img[0], 6.0f, 3);
        const double e1 = edgeEnergy(img[0], m), e4 = edgeEnergy(img[1], m), e8 = edgeEnergy(img[2], m), e16 = edgeEnergy(img[3], m);
        check("MSAA x4 smooths the edges", e1 > 0 && e4 < e1 * 0.85, fmt("edge energy x%.2f", e1 > 0 ? e4 / e1 : 0.0));
        check("MSAA x8 smooths them as much or more", e8 < e1 * 0.85 && e8 <= e4 * 1.03, fmt("x%.2f", e1 > 0 ? e8 / e1 : 0.0));
        check("MSAA x16 smooths them", e16 < e1 * 0.85, fmt("x%.2f", e1 > 0 ? e16 / e1 : 0.0));
        const double flat = meanAbsDiff(img[0], img[2], &edges);
        check("MSAA leaves the inside of the surfaces unchanged", flat < 1.0, fmt("mean difference %.3f", flat));
        return 1;
    });
    // x16: two frames averaged while nothing moves: the image is stable; a camera move starts again without a ghost
    add("anti-aliasing", [this] { a.m_msaa = 16; a.markDirty(); return 8; });
    add("anti-aliasing", [this] { img[4] = view(); return 1; });
    add("anti-aliasing", [this] { img[5] = view(); return 1; });
    add("anti-aliasing", [this] {
        img[6] = view();
        const double d1 = meanAbsDiff(img[4], img[5]), d2 = meanAbsDiff(img[5], img[6]);
        const int m = std::max(maxAbsDiff(img[4], img[5]), maxAbsDiff(img[5], img[6]));
        check("MSAA x16 still image: stable from frame to frame", d1 < 0.15 && d2 < 0.15 && m <= 6, fmt("mean %.3f %.3f, max %d", d1, d2, m));
        a.m_scene.camera.yaw += 0.35f;
        a.cameraChanged();
        return 1;
    });
    add("anti-aliasing", [this] { img[4] = view(); return 8; });
    add("anti-aliasing", [this] {
        img[5] = view();
        const double d = meanAbsDiff(img[4], img[5]);
        check("MSAA x16 after a camera move: no ghost of the previous view", d < 1.5, fmt("mean difference %.3f", d));
        a.m_msaa = 4;
        return 1;
    });

    // ================================================================= path tracing: editor overlay
    // Where the overlay must show: the pixels it changes in the raster view (masks). In the path traced view, the change
    // on those pixels must stand out of the noise of two renders (the change on the other pixels).
    auto rasterShot = [this](int slot, std::function<void()> set) {
        add("path tracing", [this, set] { set(); a.markDirty(); return 4; });
        add("path tracing", [this, slot] { img[slot] = view(); return 1; });
    };
    auto ptShot = [this](int slot, std::function<void()> set) {
        add("path tracing", [this, set] { set(); a.markDirty(); return 2; });
        add("path tracing", [this, slot] {
            if (a.m_renderer.sampleCount() < 24) return -1;
            img[slot] = view();
            return 1;
        });
    };
    auto base = [this] { a.select(-1); a.m_rs.wireframe = 0; th::settings().grid.visible = false; };
    add("path tracing", [this] { fresh(); a.m_msaa = 1; return 2; });
    rasterShot(0, base);
    rasterShot(1, [this] { th::settings().grid.visible = true; });
    rasterShot(2, [this] { th::settings().grid.visible = false; a.m_rs.wireframe = 2; });
    rasterShot(3, [this] { a.m_rs.wireframe = 0; a.select(obj("Cube")); });
    add("path tracing", [this, base] {
        for (int k = 0; k < 3; ++k) {
            const Img& b = img[0];
            const Img& o = img[k + 1];
            std::vector<uint8_t>& m = masks[k];
            m.assign((size_t)b.w * b.h, 0);
            for (size_t i = 0; i < m.size() && o.ok() && b.ok(); ++i)
                for (int c = 0; c < 3; ++c) if (std::abs(o.px[i * 4 + c] - b.px[i * 4 + c]) > 12) m[i] = 1;
        }
        base();
        a.setRenderMode(2);
        return 2;
    });
    ptShot(4, [] {});
    ptShot(5, [this] { th::settings().grid.visible = true; });
    ptShot(6, [this] { th::settings().grid.visible = false; a.m_rs.wireframe = 2; });
    ptShot(7, [this] { a.m_rs.wireframe = 0; a.select(obj("Cube")); });
    add("path tracing", [this] {
        static const char* names[3] = {"grid", "wireframe", "selection glow"};
        for (int k = 0; k < 3; ++k)
            overlayCheck(std::string("path tracing: the ") + names[k] + " overlay", k, img[4], img[5 + k], k == 0 ? Rect{0, 0, (int)img[4].w, (int)img[4].h} : objRect(obj("Cube"), 24));
        a.setRenderMode(0);
        a.m_msaa = 4;
        return 2;
    });
    // the other renderers: Metal iOS (raster with probe GI) and GI (ray traced probes), same masks
    for (int mode : {1, 3}) {
        add("render modes", [this, mode] {
            fresh();
            a.m_msaa = 1;
            a.select(-1);
            a.m_rs.wireframe = 0;
            th::settings().grid.visible = false;
            a.setRenderMode(mode);
            return 40;
        });
        add("render modes", [this] { img[4] = view(); th::settings().grid.visible = true; a.markDirty(); return 12; });
        add("render modes", [this] { img[5] = view(); th::settings().grid.visible = false; a.select(obj("Cube")); a.markDirty(); return 12; });
        add("render modes", [this, mode] {
            img[7] = view();
            static const char* modes[4] = {"Lit", "Metal iOS", "Progressive", "GI"};
            check(std::string(modes[mode]) + ": renderer in use", a.renderMode() == mode);
            overlayCheck(std::string(modes[mode]) + ": the grid", 0, img[4], img[5], Rect{0, 0, (int)img[4].w, (int)img[4].h});
            overlayCheck(std::string(modes[mode]) + ": the selection glow", 2, img[4], img[7], objRect(obj("Cube"), 24));
            a.setRenderMode(0);
            a.m_msaa = 4;
            return 2;
        });
    }

    // ================================================================= view cube and grid labels drawn (window image)
    add("overlays", [this] { fresh(); return 3; });
    add("overlays", [this] {
        img[0] = window();
        img[1] = view();
        // the cube drawn over the 3D view: window pixels unlike the view image under them
        ImRect r;
        int drawn = 0, total = 0;
        const bool found = item("##viewCube", r);
        if (found && img[0].ok() && img[1].ok())
            for (int y = (int)r.Min.y; y < (int)r.Max.y; ++y)
                for (int x = (int)r.Min.x; x < (int)r.Max.x; ++x) {
                    const int u = x - (int)a.m_rcView.x, w = y - (int)a.m_rcView.y;
                    if (x < 0 || y < 0 || x >= (int)img[0].w || y >= (int)img[0].h || u < 0 || w < 0 || u >= (int)img[1].w || w >= (int)img[1].h) continue;
                    ++total;
                    if (std::fabs(img[0].lum(x, y) - img[1].lum(u, w)) > 40) ++drawn;
                }
        check("view cube drawn over the view (edges, letters)", drawn > 60, fmt("%s, %d of %d pixels", found ? "found" : "not found", drawn, total));
        th::settings().grid.numbers = true;
        return 3;
    });
    add("overlays", [this] {
        img[1] = window();
        const int n = diffCount(img[0], img[1], 30);
        check("grid numbers on the axes", n > 60, fmt("%d pixels", n));
        th::settings().grid.numbers = false;
        return 1;
    });

    // ================================================================= Village scene: the RTS placement and Play with the new tools
    // (Play writes the Village graph, game/main.graph.json: its bytes are put back afterwards)
    add("village", [this] {
        fresh();
        graphFile = (std::filesystem::path(a.m_exeDir).parent_path().parent_path() / "game" / "main.graph.json").wstring();
        graphBytes.clear();
        if (FILE* f = _wfopen(graphFile.c_str(), L"rb")) {
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) graphBytes.append(buf, n);
            fclose(f);
        }
        a.loadScene(SCENE_STUDIO);
        a.m_savedSig = a.wzxSceneJson(false);
        a.m_docDirty = false;
        return 10;
    });
    add("village", [this] { v["objects"] = (double)a.m_scene.objects.size(); return clickItem("atlVillage"); });
    add("village", [this] {
        check("village mode (houses) on", a.m_game->placementMode());
        check("village mode: no manipulator", !a.gizmoShown());
        a.m_tool = TOOL_MOVE;
        return key(ImGuiKey_R);
    });
    add("village", [this] { check("village mode: R is for the houses, not the Scale tool", a.m_tool == TOOL_MOVE); return clickItem("atlVillage"); });
    add("village", [this] {
        check("village mode off", !a.m_game->placementMode());
        a.m_game->requestPlay();
        return 30;
    });
    add("village", [this] {
        check("Village: Play runs the RTS graph", a.m_game->playing());
        check("Village Play: no editor grid, no manipulator", a.m_rs.editorGrid == 0.0f && !a.gizmoShown());
        a.m_game->requestPlay();
        return 4;
    });
    add("village", [this] {
        check("Village: Stop gives the scene back", !a.m_game->playing() && a.m_scene.objects.size() == (size_t)v["objects"],
              fmt("%d objects, %d before", (int)a.m_scene.objects.size(), (int)v["objects"]));
        fresh();
        return 3;
    });
    add("village", [this] {
        if (!graphBytes.empty()) {   // the graph file as it was
            std::string now;
            if (FILE* f = _wfopen(graphFile.c_str(), L"rb")) {
                char buf[65536];
                size_t n;
                while ((n = fread(buf, 1, sizeof buf, f)) > 0) now.append(buf, n);
                fclose(f);
            }
            if (now != graphBytes)
                if (FILE* f = _wfopen(graphFile.c_str(), L"wb")) { fwrite(graphBytes.data(), 1, graphBytes.size(), f); fclose(f); }
        }
        return 1;
    });

    // ================================================================= robustness
    add("robustness", [this] { fresh(); a.select(obj("Cube")); return 2; });
    for (int n : {8, 1, 16, 4, 8, 16, 1, 4}) {
        add("robustness", [this, n] { a.m_msaa = n; return 2; });
        add("robustness", [this, n] {
            const uint32_t want = n == 1 ? 1u : (uint32_t)n;
            check(fmt("anti-aliasing switched to %d", n), a.m_renderer.msaaSamples() == want && view().ok(), fmt("%u samples", a.m_renderer.msaaSamples()));
            return 1;
        });
    }
    add("robustness", [this] {
        a.m_msaa = 8;
        RECT wr;
        GetWindowRect(a.m_hwnd, &wr);
        v["ww"] = wr.right - wr.left;
        v["wh"] = wr.bottom - wr.top;
        v["vw"] = a.m_vpPx.w;
        const int w = (int)v["ww"] - 220, h = (int)v["wh"] - 140;
        events.push_back([this, w, h](ImGuiIO&) { SetWindowPos(a.m_hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); });
        return queued() + 4;
    });
    add("robustness", [this] {
        std::vector<uint8_t> cov;
        uint32_t w = 0, h = 0;
        a.m_renderer.readSelectionCoverage(cov, w, h);
        const Img im = view();
        check("window resized with MSAA x8 and a selection: the view follows", a.m_vpPx.w < (int)v["vw"] && im.ok() && im.w == (uint32_t)a.m_vpPx.w,
              fmt("view %d -> %d px wide", (int)v["vw"], a.m_vpPx.w));
        check("window resized: the selection coverage follows", w == (uint32_t)a.m_vpPx.w && h == (uint32_t)a.m_vpPx.h, fmt("%u x %u", w, h));
        const int ww = (int)v["ww"], wh = (int)v["wh"];
        events.push_back([this, ww, wh](ImGuiIO&) { SetWindowPos(a.m_hwnd, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); });
        return queued() + 4;
    });
    add("robustness", [this] { check("window size restored", a.m_vpPx.w == (int)v["vw"]); return 1; });
    // GPU time of the raster view per anti-aliasing setting (Concept, cube selected)
    for (int n : {1, 4, 8, 16}) {
        add("performance", [this, n] { a.m_msaa = n; v["gpu"] = 0; v["frames"] = 0; return 12; });
        add("performance", [this, n] {
            v["gpu"] += a.m_renderer.gpuFrameMs();
            v["frames"] += 1;
            if (v["frames"] < 30) return -1;
            v[fmt("ms%d", n)] = v["gpu"] / v["frames"];
            return 1;
        });
    }
    add("performance", [this] {
        note(fmt("GPU time of the view: off %.2f ms, x4 %.2f ms, x8 %.2f ms, x16 %.2f ms", v["ms1"], v["ms4"], v["ms8"], v["ms16"]));
        check("MSAA x16 costs less than 3 times the view without anti-aliasing + 1.5 ms", v["ms16"] < v["ms1"] * 3 + 1.5, fmt("%.2f ms", v["ms16"]));
        a.m_msaa = 4;
        return 1;
    });
}

} // namespace app
