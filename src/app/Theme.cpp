#include "Theme.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace app {
namespace th {

static std::vector<ThemeDef> g_themes;
static UiSettings g_settings;
static ThemePalette g_pal;
static std::wstring g_folder;

static ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
static ImVec4 alpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// --------------------------------------------------------------------------- built-in values

// Metrics of the editor (the "Default" theme); theme files override what they set.
static ImGuiStyle editorStyle() {
    ImGuiStyle s;
    s.WindowPadding = ImVec2(10, 8);
    s.FramePadding = ImVec2(8, 5);
    s.CellPadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 11;
    s.GrabMinSize = 10;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 1;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.TabBarOverlineSize = 2;
    s.WindowRounding = 0;
    s.ChildRounding = 4;
    s.FrameRounding = 4;
    s.PopupRounding = 5;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 3;
    s.TabRounding = 4;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.DockingSeparatorSize = 2;
    return s;
}

static ThemePalette defaultDark() {
    ThemePalette p;
    p.bg0 = ImVec4(0.055f, 0.062f, 0.075f, 1);
    p.bg1 = ImVec4(0.082f, 0.090f, 0.106f, 1);
    p.bg2 = ImVec4(0.106f, 0.118f, 0.137f, 1);
    p.field = ImVec4(0.060f, 0.067f, 0.080f, 1);
    p.hover = ImVec4(0.150f, 0.165f, 0.190f, 1);
    p.border = ImVec4(0.150f, 0.163f, 0.185f, 1);
    p.text = ImVec4(0.870f, 0.885f, 0.905f, 1);
    p.dim = ImVec4(0.592f, 0.616f, 0.651f, 1);
    p.faint = ImVec4(0.420f, 0.450f, 0.490f, 1);
    p.accent = ImVec4(0.235f, 0.502f, 0.980f, 1);
    p.select = ImVec4(0.130f, 0.220f, 0.380f, 1);
    p.bar = mix(p.bg0, p.bg1, 0.5f);
    p.strong = p.accent;
    return p;
}

static ThemePalette lightPalette(const ImVec4& accent) {
    ThemePalette p;
    p.bg0 = ImVec4(0.808f, 0.820f, 0.836f, 1);   // slightly dimmed (less glare)
    p.bg1 = ImVec4(0.885f, 0.892f, 0.902f, 1);
    p.bg2 = ImVec4(0.842f, 0.850f, 0.864f, 1);
    p.field = ImVec4(0.930f, 0.934f, 0.940f, 1);
    p.hover = ImVec4(0.775f, 0.790f, 0.815f, 1);
    p.border = ImVec4(0.720f, 0.738f, 0.765f, 1);
    p.text = ImVec4(0.105f, 0.115f, 0.135f, 1);
    p.dim = ImVec4(0.330f, 0.360f, 0.400f, 1);
    p.faint = ImVec4(0.560f, 0.590f, 0.630f, 1);
    p.accent = accent;
    p.select = mix(accent, p.field, 0.72f);
    p.bar = mix(p.bg0, p.bg1, 0.5f);
    p.strong = accent;
    return p;
}

// Ambre theme: graphite surfaces and one amber accent (theme.json of the Ambre UI pack, 01 - INBOX/UI/Ambre_UI_Composants).
static ImVec4 hexv(unsigned h) { return ImVec4(((h >> 16) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, (h & 255) / 255.0f, 1); }
static ThemePalette ambrePalette() {
    ThemePalette p;
    p.bg0 = hexv(0x101114);     // canvas
    p.bg1 = hexv(0x1A1C20);     // panel
    p.bg2 = hexv(0x24272C);     // raised
    p.field = hexv(0x14161A);   // input
    p.hover = hexv(0x29231C);   // warm hover
    p.border = hexv(0x363A40);
    p.text = hexv(0xD9DADF);
    p.dim = hexv(0x959AA3);
    p.faint = hexv(0x787D85);
    p.accent = hexv(0xECA43D);
    p.select = hexv(0x33271B);
    p.bar = hexv(0x171B1E);
    p.strong = p.accent;
    return p;
}
// Metrics of imgui/WzxStyle.h of the pack: 3 px fields, 4 px windows, flat 5 px slider grab.
static ImGuiStyle ambreStyle() {
    ImGuiStyle s = editorStyle();
    s.WindowRounding = 4;
    s.ChildRounding = 4;
    s.FrameRounding = 3;
    s.PopupRounding = 4;
    s.GrabRounding = 1;
    s.ScrollbarRounding = 3;
    s.TabRounding = 3;
    s.GrabMinSize = 5;
    s.FramePadding = ImVec2(8, 6);
    return s;
}

// Anthracite-Corail theme: matte charcoal cards, coral selection, rust primary action
// (theme.json of 01 - INBOX/UI/Anthracite-Corail_UI_Components).
static ThemePalette corailPalette() {
    ThemePalette p;
    p.bg0 = hexv(0x111315);     // shell
    p.bg1 = hexv(0x181A1C);     // panel
    p.bg2 = hexv(0x282C32);     // raised
    p.field = hexv(0x191B1E);   // input
    p.hover = hexv(0x23262A);
    p.border = hexv(0x2B2F34);
    p.text = hexv(0xDDDFE2);
    p.dim = hexv(0x92979F);
    p.faint = hexv(0x626A74);
    p.accent = hexv(0xD8977C);
    p.select = hexv(0x302622);
    p.bar = hexv(0x111315);
    p.strong = hexv(0xB94F2F);
    return p;
}
// Rounded cards (8 px), 5 px fields, flat 5 x 16 slider grab.
// Selection shades of the Corail theme for another selection colour: same proportions as the pack shade (packHex)
// between the #181A1C panel and the #D8977C coral, applied between panel and accent.
static ImVec4 corailShade(const ImVec4& accent, const ImVec4& panel, unsigned packHex) {
    const ImVec4 pa = hexv(0xD8977C), pp = hexv(0x181A1C), ps = hexv(packHex);
    float k = ((ps.x - pp.x) / (pa.x - pp.x) + (ps.y - pp.y) / (pa.y - pp.y) + (ps.z - pp.z) / (pa.z - pp.z)) / 3;
    return mix(panel, accent, k);
}
static ImGuiStyle corailStyle() {
    ImGuiStyle s = editorStyle();
    s.WindowRounding = 8;
    s.ChildRounding = 8;
    s.FrameRounding = 5;
    s.PopupRounding = 8;
    s.GrabRounding = 1;
    s.ScrollbarRounding = 4;
    s.TabRounding = 5;
    s.GrabMinSize = 5;
    s.FramePadding = ImVec2(10, 6);
    s.WindowBorderSize = 0;
    s.PopupBorderSize = 1;
    return s;
}

// Full ImGui colour table from a palette (used by the built-in theme and the light mode).
static void colorsFromPalette(const ThemePalette& p, bool light, ImVec4* c) {
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.faint;
    c[ImGuiCol_WindowBg] = p.bg1;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = alpha(light ? p.field : mix(p.bg1, p.bg2, 0.5f), 0.98f);
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = p.field;
    c[ImGuiCol_FrameBgHovered] = mix(p.field, p.hover, 0.4f);
    c[ImGuiCol_FrameBgActive] = mix(p.field, p.hover, 0.7f);
    c[ImGuiCol_TitleBg] = p.bg0;
    c[ImGuiCol_TitleBgActive] = p.bg0;
    c[ImGuiCol_TitleBgCollapsed] = p.bg0;
    c[ImGuiCol_MenuBarBg] = p.bg0;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = mix(p.hover, p.faint, 0.35f);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(p.hover, p.faint, 0.6f);
    c[ImGuiCol_ScrollbarGrabActive] = p.faint;
    c[ImGuiCol_CheckMark] = light ? p.accent : ImVec4(1, 1, 1, 1);
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = mix(p.accent, ImVec4(1, 1, 1, 1), 0.2f);
    c[ImGuiCol_Button] = p.bg2;
    c[ImGuiCol_ButtonHovered] = p.hover;
    c[ImGuiCol_ButtonActive] = p.select;
    c[ImGuiCol_Header] = p.select;
    c[ImGuiCol_HeaderHovered] = p.hover;
    c[ImGuiCol_HeaderActive] = p.select;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = p.accent;
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = p.accent;
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.accent;
    c[ImGuiCol_Tab] = p.bg2;
    c[ImGuiCol_TabHovered] = p.hover;
    c[ImGuiCol_TabSelected] = p.bg1;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = p.bg2;
    c[ImGuiCol_TabDimmedSelected] = p.bg1;
    c[ImGuiCol_TabDimmedSelectedOverline] = alpha(p.accent, 0.5f);
    c[ImGuiCol_DockingPreview] = alpha(p.accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = p.bg0;
    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = p.accent;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.accent;
    c[ImGuiCol_TableHeaderBg] = p.bg2;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = mix(p.bg1, p.border, 0.5f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = light ? ImVec4(0, 0, 0, 0.025f) : ImVec4(1, 1, 1, 0.015f);
    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_TextSelectedBg] = alpha(p.accent, 0.35f);
    c[ImGuiCol_TreeLines] = p.border;
    c[ImGuiCol_DragDropTarget] = p.accent;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = alpha(p.text, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

// Accent-coloured entries, recoloured when the user picks a main colour.
static void applyAccent(ImVec4* c, const ImVec4& a) {
    const ImGuiCol full[] = {ImGuiCol_SliderGrab, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive, ImGuiCol_ResizeGripHovered,
                             ImGuiCol_ResizeGripActive, ImGuiCol_InputTextCursor, ImGuiCol_TabSelectedOverline, ImGuiCol_PlotLines,
                             ImGuiCol_PlotHistogram, ImGuiCol_DragDropTarget, ImGuiCol_NavCursor, ImGuiCol_TextLink};
    for (ImGuiCol i : full) c[i] = a;
    c[ImGuiCol_SliderGrabActive] = mix(a, ImVec4(1, 1, 1, 1), 0.2f);
    c[ImGuiCol_TabDimmedSelectedOverline] = alpha(a, 0.5f);
    c[ImGuiCol_DockingPreview] = alpha(a, 0.35f);
    c[ImGuiCol_TextSelectedBg] = alpha(a, 0.35f);
}

// --------------------------------------------------------------------------- theme files

static bool parseVec(const std::string& s, float* v, int n) {
    // ImVec4(r, g, b, a) / ImVec2(x, y) / IM_COL32(r, g, b, a) / plain numbers
    size_t open = s.find('(');
    std::string body = open != std::string::npos ? s.substr(open + 1) : s;
    bool ints = s.find("IM_COL32") != std::string::npos;
    for (auto& ch : body) if (ch == ',' || ch == ')' || ch == 'f' || ch == ';') ch = ' ';
    std::istringstream in(body);
    int k = 0;
    while (k < n && (in >> v[k])) ++k;
    if (ints) for (int i = 0; i < k; ++i) v[i] /= 255.0f;
    if (n == 4 && k == 3) { v[3] = 1.0f; k = 4; }
    return k == n;
}

static std::string trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static void derivePalette(ThemeDef& t, const bool given[13]) {
    ThemePalette d = defaultDark();
    auto col = [&](ImGuiCol i, const ImVec4& fb) { return t.hasColors[i] ? t.colors[i] : fb; };
    ThemePalette p;
    p.bg0 = col(ImGuiCol_MenuBarBg, col(ImGuiCol_TitleBg, d.bg0));
    p.bg1 = col(ImGuiCol_WindowBg, d.bg1);
    p.bg2 = col(ImGuiCol_Tab, col(ImGuiCol_Button, d.bg2));
    p.field = col(ImGuiCol_FrameBg, d.field);
    p.hover = col(ImGuiCol_ButtonHovered, col(ImGuiCol_HeaderHovered, d.hover));
    p.border = col(ImGuiCol_Border, d.border);
    p.text = col(ImGuiCol_Text, d.text);
    p.faint = col(ImGuiCol_TextDisabled, d.faint);
    p.dim = mix(p.text, p.faint, 0.55f);
    p.accent = col(ImGuiCol_TabSelectedOverline, col(ImGuiCol_SliderGrabActive, col(ImGuiCol_CheckMark, d.accent)));
    p.select = col(ImGuiCol_Header, d.select);
    ImVec4* dst[13] = {&t.palette.bg0, &t.palette.bg1, &t.palette.bg2, &t.palette.field, &t.palette.hover, &t.palette.border,
                       &t.palette.text, &t.palette.dim, &t.palette.faint, &t.palette.accent, &t.palette.select, &t.palette.bar, &t.palette.strong};
    p.bar = mix(p.bg0, p.bg1, 0.5f);
    p.strong = p.accent;
    ImVec4* src[13] = {&p.bg0, &p.bg1, &p.bg2, &p.field, &p.hover, &p.border, &p.text, &p.dim, &p.faint, &p.accent, &p.select, &p.bar, &p.strong};
    for (int i = 0; i < 13; ++i)
        if (!given[i]) *dst[i] = *src[i];
}

static bool loadThemeFile(const std::wstring& path, ThemeDef& t) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    t.style = editorStyle();
    std::string line;
    bool given[13] = {};
    static const char* roles[13] = {"bg0", "bg1", "bg2", "field", "hover", "border", "text", "dim", "faint", "accent", "select", "bar", "strong"};
    ImVec4* rolePtr[13] = {&t.palette.bg0, &t.palette.bg1, &t.palette.bg2, &t.palette.field, &t.palette.hover, &t.palette.border,
                           &t.palette.text, &t.palette.dim, &t.palette.faint, &t.palette.accent, &t.palette.select, &t.palette.bar, &t.palette.strong};
    // style fields accepted in the files
    struct FloatField { const char* name; float ImGuiStyle::*ptr; };
    static const FloatField floats[] = {
        {"Alpha", &ImGuiStyle::Alpha}, {"WindowRounding", &ImGuiStyle::WindowRounding}, {"ChildRounding", &ImGuiStyle::ChildRounding},
        {"FrameRounding", &ImGuiStyle::FrameRounding}, {"PopupRounding", &ImGuiStyle::PopupRounding}, {"ScrollbarRounding", &ImGuiStyle::ScrollbarRounding},
        {"GrabRounding", &ImGuiStyle::GrabRounding}, {"TabRounding", &ImGuiStyle::TabRounding}, {"WindowBorderSize", &ImGuiStyle::WindowBorderSize},
        {"FrameBorderSize", &ImGuiStyle::FrameBorderSize}, {"ChildBorderSize", &ImGuiStyle::ChildBorderSize}, {"PopupBorderSize", &ImGuiStyle::PopupBorderSize},
        {"TabBorderSize", &ImGuiStyle::TabBorderSize}, {"ScrollbarSize", &ImGuiStyle::ScrollbarSize}, {"GrabMinSize", &ImGuiStyle::GrabMinSize},
        {"IndentSpacing", &ImGuiStyle::IndentSpacing}, {"TabBarBorderSize", &ImGuiStyle::TabBarBorderSize}, {"TabBarOverlineSize", &ImGuiStyle::TabBarOverlineSize},
        {"DockingSeparatorSize", &ImGuiStyle::DockingSeparatorSize}};
    struct VecField { const char* name; ImVec2 ImGuiStyle::*ptr; };
    static const VecField vecs[] = {{"WindowPadding", &ImGuiStyle::WindowPadding}, {"FramePadding", &ImGuiStyle::FramePadding},
                                    {"ItemSpacing", &ImGuiStyle::ItemSpacing}, {"ItemInnerSpacing", &ImGuiStyle::ItemInnerSpacing},
                                    {"CellPadding", &ImGuiStyle::CellPadding}, {"WindowTitleAlign", &ImGuiStyle::WindowTitleAlign}};
    while (std::getline(f, line)) {
        std::string l = trim(line);
        if (l.empty() || l.rfind("//", 0) == 0 || l[0] == '#') {
            // "// WZX theme: Name" comment naming the theme
            size_t k = l.find("theme:");
            if (k != std::string::npos && t.name.empty()) t.name = trim(l.substr(k + 6));
            continue;
        }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(l.substr(0, eq)), val = trim(l.substr(eq + 1));
        size_t cpos = key.find("ImGuiCol_");
        if (cpos != std::string::npos) {
            std::string cname = key.substr(cpos + 9);
            cname = cname.substr(0, cname.find_first_of("] "));
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
                if (cname == ImGui::GetStyleColorName(i)) {
                    float v[4];
                    if (parseVec(val, v, 4)) { t.colors[i] = ImVec4(v[0], v[1], v[2], v[3]); t.hasColors[i] = true; }
                }
            continue;
        }
        if (key.rfind("wzx.", 0) == 0) {
            std::string k = key.substr(4);
            if (k == "name") { std::string n = val; if (!n.empty() && n.back() == ';') n.pop_back(); t.name = trim(n); }
            else if (k == "fontSize") t.fontSize = (float)atof(val.c_str());
            else
                for (int i = 0; i < 13; ++i)
                    if (k == roles[i]) {
                        float v[4];
                        if (parseVec(val, v, 4)) { *rolePtr[i] = ImVec4(v[0], v[1], v[2], v[3]); given[i] = true; }
                    }
            continue;
        }
        std::string field = key;
        size_t dot = field.find_last_of(".>");
        if (dot != std::string::npos) field = field.substr(dot + 1);
        for (const FloatField& ff : floats)
            if (field == ff.name) t.style.*ff.ptr = (float)atof(val.c_str());
        for (const VecField& vf : vecs)
            if (field == vf.name) {
                float v[2];
                if (parseVec(val, v, 2)) t.style.*vf.ptr = ImVec2(v[0], v[1]);
            }
    }
    derivePalette(t, given);
    if (t.fontSize < 8 || t.fontSize > 32) t.fontSize = 15.0f;
    return true;
}

void loadThemes(const std::wstring& folder) {
    g_folder = folder;
    g_themes.clear();
    ThemeDef d;
    d.name = "Default";
    d.style = editorStyle();
    d.palette = defaultDark();
    colorsFromPalette(d.palette, false, d.colors);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) d.hasColors[i] = true;
    g_themes.push_back(d);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"\\*.theme").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        ThemeDef t;
        std::wstring path = folder + L"\\" + fd.cFileName;
        if (!loadThemeFile(path, t)) continue;
        std::wstring stem = fd.cFileName;
        stem = stem.substr(0, stem.find_last_of(L'.'));
        if (t.name.empty()) t.name = utf8(stem);
        t.file = utf8(path);
        // a file named like a built-in theme replaces it
        auto it = std::find_if(g_themes.begin(), g_themes.end(), [&](const ThemeDef& e) { return e.name == t.name; });
        if (it != g_themes.end()) *it = t;
        else g_themes.push_back(t);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::stable_sort(g_themes.begin() + 1, g_themes.end(), [](const ThemeDef& a, const ThemeDef& b) { return a.name < b.name; });
}

const std::vector<ThemeDef>& themes() { return g_themes; }
UiSettings& settings() { return g_settings; }
const ThemePalette& P() { return g_pal; }
const std::wstring& folder() { return g_folder; }

// Main colour: multiplier applied to the near-grey colours, relative to the tint of the Blue 01 theme.
static ImVec4 g_tintMul(1, 1, 1, 1);
static ImVec4 direction(const ImVec4& c) {   // colour / mean, limited so a saturated pick stays a tint
    float m = std::max(1e-3f, (c.x + c.y + c.z) / 3);
    return ImVec4(std::clamp(c.x / m, 0.6f, 1.5f), std::clamp(c.y / m, 0.6f, 1.5f), std::clamp(c.z / m, 0.6f, 1.5f), 1);
}
// Brightness of the main colour (when unlocked): applied to the surfaces only, the text keeps its contrast.
static float g_bright = 1.0f;
ImVec4 tint(const ImVec4& c) {
    float mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z});
    if (mx > 0 && (mx - mn) / mx > 0.35f) return c;   // accents and semantic colours keep their hue
    float lum = (c.x + c.y + c.z) / 3;
    bool surface = g_settings.light ? lum > 0.5f : lum < 0.5f;
    float b = surface ? g_bright : 1.0f;
    return ImVec4(std::clamp(c.x * g_tintMul.x * b, 0.0f, 1.0f), std::clamp(c.y * g_tintMul.y * b, 0.0f, 1.0f), std::clamp(c.z * g_tintMul.z * b, 0.0f, 1.0f), c.w);
}

void apply(float dpi) {
    // the editor theme: Blue 01 (style metrics + dark colours), the built-in theme if the file is missing
    const ThemeDef* t = &g_themes[0];
    for (const ThemeDef& e : g_themes)
        if (e.name == "Blue 01") t = &e;
    ImGuiStyle& s = ImGui::GetStyle();
    ImGuiStyle st = t->style;
    ImVec4 accent = g_settings.customAccent ? g_settings.accent : t->palette.accent;
    ImVec4 ref = direction(ImVec4(0.415f, 0.506f, 0.578f, 1)), want = direction(g_settings.mainColor);
    // unlocked brightness: the main colour also lightens / darkens the surfaces (Blue Slate = 1)
    const ImVec4& mc = g_settings.mainColor;
    float brightness = std::clamp((mc.x + mc.y + mc.z) / 3 / 0.5f, 0.35f, 3.0f);
    auto tintPalette = [](ThemePalette& p) {
        for (ImVec4* c : {&p.bg0, &p.bg1, &p.bg2, &p.field, &p.hover, &p.border, &p.text, &p.dim, &p.faint, &p.bar}) *c = tint(*c);
    };
    if (ambre()) {
        // fixed colours: no main colour tint, no custom accent, no light mode
        g_tintMul = ImVec4(1, 1, 1, 1);
        g_bright = 1.0f;
        g_pal = ambrePalette();
        st = ambreStyle();
        colorsFromPalette(g_pal, false, st.Colors);
        ImVec4* c = st.Colors;
        c[ImGuiCol_WindowBg] = g_pal.bg1;
        c[ImGuiCol_PopupBg] = alpha(hexv(0x1D2024), 0.99f);
        c[ImGuiCol_FrameBgHovered] = hexv(0x1B1E23);
        c[ImGuiCol_FrameBgActive] = hexv(0x1E1A15);
        c[ImGuiCol_Button] = hexv(0x22262B);
        c[ImGuiCol_ButtonHovered] = g_pal.hover;
        c[ImGuiCol_ButtonActive] = g_pal.select;
        c[ImGuiCol_HeaderHovered] = g_pal.hover;
        c[ImGuiCol_CheckMark] = g_pal.accent;
        c[ImGuiCol_SliderGrab] = hexv(0xC7C9CB);         // matte silver handle
        c[ImGuiCol_SliderGrabActive] = hexv(0xF1F0ED);
        c[ImGuiCol_Tab] = hexv(0x1A1D20);
        c[ImGuiCol_TabHovered] = g_pal.hover;
        c[ImGuiCol_TabSelected] = hexv(0x24272C);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_MenuBarBg] = g_pal.bar;
        c[ImGuiCol_ScrollbarGrab] = hexv(0x30343A);
        c[ImGuiCol_ScrollbarGrabHovered] = hexv(0x464C53);
        c[ImGuiCol_ScrollbarGrabActive] = hexv(0x8B6028);
        c[ImGuiCol_TextSelectedBg] = alpha(g_pal.accent, 0.30f);
    } else if (corail()) {
        // main colour of the settings: tints the neutral surfaces (Graphite keeps the pack colours)
        const ImVec4& cm = g_settings.corailMain;
        g_tintMul = direction(cm);
        g_bright = g_settings.lockBrightness ? 1.0f : std::clamp((cm.x + cm.y + cm.z) / 3 / 0.5f, 0.35f, 3.0f);
        g_pal = corailPalette();
        g_pal.accent = g_settings.corailAccent;   // selection colour of the settings and its selection fill
        g_pal.select = corailShade(g_settings.corailAccent, g_pal.bg1, 0x302622);
        st = corailStyle();
        colorsFromPalette(g_pal, false, st.Colors);
        ImVec4* c = st.Colors;
        c[ImGuiCol_WindowBg] = g_pal.bg1;
        c[ImGuiCol_PopupBg] = alpha(hexv(0x1D1F22), 0.99f);
        c[ImGuiCol_Border] = hexv(0x2B2F34);
        c[ImGuiCol_FrameBgHovered] = hexv(0x1E2024);
        c[ImGuiCol_FrameBgActive] = hexv(0x221F1E);
        c[ImGuiCol_Button] = hexv(0x2B3038);
        c[ImGuiCol_ButtonHovered] = hexv(0x323842);
        c[ImGuiCol_ButtonActive] = g_pal.select;
        c[ImGuiCol_Header] = g_pal.select;
        c[ImGuiCol_HeaderHovered] = hexv(0x23262A);
        c[ImGuiCol_HeaderActive] = g_pal.select;
        c[ImGuiCol_CheckMark] = hexv(0x111315);             // dark tick on the coral box (drawn by the editor)
        c[ImGuiCol_SliderGrab] = hexv(0xBEC3C9);            // matte handle
        c[ImGuiCol_SliderGrabActive] = hexv(0xE4E7EA);
        c[ImGuiCol_Tab] = hexv(0x181A1C);
        c[ImGuiCol_TabHovered] = hexv(0x23262A);
        c[ImGuiCol_TabSelected] = hexv(0x1D1F22);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_MenuBarBg] = g_pal.bg0;
        c[ImGuiCol_ScrollbarGrab] = hexv(0x393D42);
        c[ImGuiCol_ScrollbarGrabHovered] = hexv(0x4A4F56);
        c[ImGuiCol_ScrollbarGrabActive] = corailShade(g_settings.corailAccent, hexv(0x181A1C), 0x6E4B3E);
        c[ImGuiCol_TextSelectedBg] = alpha(g_pal.accent, 0.30f);
        for (int i = 0; i < ImGuiCol_COUNT; ++i) c[i] = tint(c[i]);   // accents keep their hue
        tintPalette(g_pal);
    } else if (g_settings.light) {
        // light surfaces carry a softer tint
        ImVec4 r = mix(ImVec4(1, 1, 1, 1), ref, 0.12f), w = mix(ImVec4(1, 1, 1, 1), want, 0.12f);   // 0.12: blue tint of the light palette
        g_tintMul = ImVec4(w.x / r.x, w.y / r.y, w.z / r.z, 1);
        g_bright = g_settings.lockBrightness ? 1.0f : 1 + (brightness - 1) * 0.35f;
        g_pal = lightPalette(accent);
        tintPalette(g_pal);
        colorsFromPalette(g_pal, true, st.Colors);
    } else {
        g_tintMul = ImVec4(want.x / ref.x, want.y / ref.y, want.z / ref.z, 1);
        g_bright = g_settings.lockBrightness ? 1.0f : brightness;
        g_pal = t->palette;
        ImVec4 base[ImGuiCol_COUNT];
        colorsFromPalette(g_pal, false, base);
        for (int i = 0; i < ImGuiCol_COUNT; ++i) st.Colors[i] = tint(t->hasColors[i] ? t->colors[i] : base[i]);
        tintPalette(g_pal);
        if (g_settings.customAccent) {
            g_pal.accent = accent;
            g_pal.select = mix(accent, g_pal.bg1, 0.62f);
            g_pal.strong = accent;
            applyAccent(st.Colors, accent);
            st.Colors[ImGuiCol_Header] = st.Colors[ImGuiCol_HeaderActive] = st.Colors[ImGuiCol_ButtonActive] = g_pal.select;
        }
    }
    st.ScaleAllSizes(dpi);
    st.FontScaleMain = (t->fontSize / 15.0f) * textSize();
    s = st;
}

// --------------------------------------------------------------------------- settings

void loadSettings(const std::wstring& file) {
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        float c[4];
        if (k == "theme" && v == "Default") g_settings.mainColor = ImVec4(0.5f, 0.5f, 0.5f, 1);   // settings of an older version
        else if (k == "uiTheme") g_settings.theme = v == "Ambre" || v == "Corail" ? v : "Atelier";
        else if (k == "light") g_settings.light = v == "1";
        else if (k == "customAccent") g_settings.customAccent = v == "1";
        else if (k == "accent") { if (parseVec(v, c, 4)) g_settings.accent = ImVec4(c[0], c[1], c[2], 1); }
        else if (k == "mainColor") { if (parseVec(v, c, 4)) g_settings.mainColor = ImVec4(c[0], c[1], c[2], 1); }
        else if (k == "corailMainColor") { if (parseVec(v, c, 4)) g_settings.corailMain = ImVec4(c[0], c[1], c[2], 1); }
        else if (k == "corailAccent") { if (parseVec(v, c, 4)) g_settings.corailAccent = ImVec4(c[0], c[1], c[2], 1); }
        else if (k == "textScale") g_settings.textScale = std::clamp((float)atof(v.c_str()), 0.8f, 1.4f);
        else if (k == "iconScale") g_settings.iconScale = std::clamp((float)atof(v.c_str()), 0.8f, 1.4f);
        else if (k == "layoutScale") g_settings.layoutScale = std::clamp((float)atof(v.c_str()), 0.85f, 1.2f);
        else if (k == "globalScale") g_settings.globalScale = std::clamp((float)atof(v.c_str()), 0.8f, 1.3f);
        else if (k == "lockBrightness") g_settings.lockBrightness = v != "0";
        else if (k == "language") g_settings.language = v;
        else if (k == "graphOutline") g_settings.graphOutline = v != "0";
        else if (k == "graphOutlineWeight") g_settings.graphOutlineWeight = std::clamp((float)atof(v.c_str()), 0.1f, 3.0f);
        else if (k == "graphSelectColor") { if (parseVec(v, c, 4)) g_settings.graphSelectColor = ImVec4(c[0], c[1], c[2], 1); }
        else if (k == "graphOutputHeight") g_settings.graphOutputHeight = std::clamp((float)atof(v.c_str()), 70.0f, 2000.0f);
        else if (k.rfind("grid.", 0) == 0) {   // editor grid (Grid Options)
            GridOptions& g = g_settings.grid;
            const std::string n = k.substr(5);
            const float x = (float)atof(v.c_str());
            const bool on = v != "0";
            if (n == "visible") g.visible = on;
            else if (n == "sceneSpacing") g.sceneSpacing = on;
            else if (n == "every") g.every = std::clamp(x, 0.001f, 100000.0f);
            else if (n == "subdivisions") g.subdivisions = std::clamp((int)x, 1, 100);
            else if (n == "extent") g.extent = std::clamp(x, 0.0f, 1000000.0f);
            else if (n == "lines") g.lines = on;
            else if (n == "subdivLines") g.subdivLines = on;
            else if (n == "axes") g.axes = on;
            else if (n == "thickAxes") g.thickAxes = on;
            else if (n == "arrows") g.arrows = on;
            else if (n == "numbers") g.numbers = on;
            else if (n == "fade") g.fade = on;
            else if (n == "widthGrid") g.widthGrid = std::clamp(x, 0.25f, 8.0f);
            else if (n == "widthSub") g.widthSub = std::clamp(x, 0.25f, 8.0f);
            else if (n == "widthAxis") g.widthAxis = std::clamp(x, 0.25f, 8.0f);
            else if (n == "opacity") g.opacity = std::clamp(x, 0.0f, 1.0f);
            else if (parseVec(v, c, 4)) {
                ImVec4 col(c[0], c[1], c[2], c[3]);
                if (n == "colorGrid") g.colorGrid = col;
                else if (n == "colorSub") g.colorSub = col;
                else if (n == "colorX") g.colorX = col;
                else if (n == "colorY") g.colorY = col;
                else if (n == "colorZ") g.colorZ = col;
            }
        }
    }
}

void saveSettings(const std::wstring& file) {
    std::ofstream f(file, std::ios::binary);
    if (!f) return;
    const UiSettings& s = g_settings;
    char a[64], m[64], sc[64], cm[64], ca[64];
    snprintf(a, sizeof(a), "%.3f %.3f %.3f 1", s.accent.x, s.accent.y, s.accent.z);
    snprintf(m, sizeof(m), "%.3f %.3f %.3f 1", s.mainColor.x, s.mainColor.y, s.mainColor.z);
    snprintf(sc, sizeof(sc), "%.3f %.3f %.3f 1", s.graphSelectColor.x, s.graphSelectColor.y, s.graphSelectColor.z);
    snprintf(cm, sizeof(cm), "%.3f %.3f %.3f 1", s.corailMain.x, s.corailMain.y, s.corailMain.z);
    snprintf(ca, sizeof(ca), "%.3f %.3f %.3f 1", s.corailAccent.x, s.corailAccent.y, s.corailAccent.z);
    f << "uiTheme=" << s.theme << "\nlight=" << (s.light ? 1 : 0) << "\nmainColor=" << m << "\ncorailMainColor=" << cm << "\ncorailAccent=" << ca << "\ncustomAccent=" << (s.customAccent ? 1 : 0) << "\naccent=" << a
      << "\ntextScale=" << s.textScale << "\niconScale=" << s.iconScale << "\nlayoutScale=" << s.layoutScale << "\nglobalScale=" << s.globalScale
      << "\nlockBrightness=" << (s.lockBrightness ? 1 : 0) << "\nlanguage=" << s.language
      << "\ngraphOutline=" << (s.graphOutline ? 1 : 0) << "\ngraphOutlineWeight=" << s.graphOutlineWeight
      << "\ngraphSelectColor=" << sc << "\n";
    const GridOptions& g = s.grid;   // editor grid (Grid Options)
    auto col = [](const ImVec4& v) { char b[64]; snprintf(b, sizeof(b), "%.3f %.3f %.3f %.3f", v.x, v.y, v.z, v.w); return std::string(b); };
    f << "grid.visible=" << (g.visible ? 1 : 0) << "\ngrid.sceneSpacing=" << (g.sceneSpacing ? 1 : 0) << "\ngrid.every=" << g.every
      << "\ngrid.subdivisions=" << g.subdivisions << "\ngrid.extent=" << g.extent << "\ngrid.lines=" << (g.lines ? 1 : 0)
      << "\ngrid.subdivLines=" << (g.subdivLines ? 1 : 0) << "\ngrid.axes=" << (g.axes ? 1 : 0) << "\ngrid.thickAxes=" << (g.thickAxes ? 1 : 0)
      << "\ngrid.arrows=" << (g.arrows ? 1 : 0) << "\ngrid.numbers=" << (g.numbers ? 1 : 0) << "\ngrid.fade=" << (g.fade ? 1 : 0)
      << "\ngrid.widthGrid=" << g.widthGrid << "\ngrid.widthSub=" << g.widthSub << "\ngrid.widthAxis=" << g.widthAxis
      << "\ngrid.opacity=" << g.opacity << "\ngrid.colorGrid=" << col(g.colorGrid) << "\ngrid.colorSub=" << col(g.colorSub)
      << "\ngrid.colorX=" << col(g.colorX) << "\ngrid.colorY=" << col(g.colorY) << "\ngrid.colorZ=" << col(g.colorZ) << "\n";
}

} // namespace th
} // namespace app
