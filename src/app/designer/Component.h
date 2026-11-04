// UI components ("Création Item"): items designed like in Figma (a button, a badge, a card...) as a stack of layers
// (rectangle, ellipse, text, line) with solid / gradient fills, strokes, soft shadows, and a look per interaction state
// (normal, hover, pressed, disabled). Saved as ui/components/<name>.wzcomp.json, edited in the Designer's
// "Création Item" space, drawn by any interface through the Component widget (Kind::Component).
#pragma once
#include "imgui.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace wzui {

enum class Shape { Rect, Ellipse, Text, Line, Count };
enum State { StNormal, StHover, StActive, StDisabled, StCount };
const char* shapeName(Shape s);          // "rect"...
const char* stateKey(int st);            // "normal", "hover", "active", "disabled"
const char* stateLabel(int st);          // "Normal", "Survol", "Appuyé", "Désactivé"

struct Fill {
    bool on = true;
    bool gradient = false;
    ImVec4 a = ImVec4(0.85f, 0.58f, 0.27f, 1), b = ImVec4(0.70f, 0.42f, 0.16f, 1);
    float angle = 90;                    // degrees, 90 = top to bottom
};

// what can change from one state to another
struct Look {
    Fill fill;
    bool stroke = false;
    ImVec4 strokeColor = ImVec4(1, 1, 1, 0.25f);
    float strokeWidth = 1;
    bool shadow = false;
    ImVec4 shadowColor = ImVec4(0, 0, 0, 0.45f);
    ImVec2 shadowOffset = ImVec2(0, 2);
    float shadowBlur = 6;
    ImVec4 textColor = ImVec4(1, 1, 1, 1);
    float opacity = 1;
    ImVec2 offset = ImVec2(0, 0);        // e.g. pressed: 1 px down
};

struct Layer {
    int uid = 0;
    Shape shape = Shape::Rect;
    std::string name;
    float x = 0, y = 0, w = 40, h = 20;  // component units (px at 100 %)
    float radius = 0;                    // Rect corners
    std::string text = "{text}";         // Text; {text} = the text of the instance
    float fontSize = 14;
    int align = 1;                       // 0 left, 1 centre, 2 right
    bool visible = true;
    Look look[StCount];
    bool hasState[StCount] = {true, false, false, false};   // a state without its own look uses the normal one
    const Look& lookFor(int st) const { return hasState[st] ? look[st] : look[StNormal]; }
};

struct Component {
    std::string name = "composant";
    float w = 140, h = 40;
    std::vector<Layer> layers;           // bottom to top
    int nextUid = 1;
    Layer* find(int uid);
    Layer makeLayer(Shape s);
};

Component defaultButton(const std::string& name);   // a ready-made amber button
bool loadComponent(const std::filesystem::path& file, Component& c, std::string* error = nullptr);
bool saveComponent(const std::filesystem::path& file, const Component& c);

// Draws `c` stretched into [pos, pos + size] (screen px). `scale` scales radii, strokes, text and shadows.
// The disabled state also dims the whole item when it has no look of its own.
void drawComponent(ImDrawList* dl, const Component& c, ImVec2 pos, ImVec2 size, float scale, int state, const std::string& text);

// Library of the components of ui/components, shared by the editor and the interfaces.
struct ComponentLibrary {
    std::filesystem::path folder;
    std::map<std::string, Component> items;
    std::map<std::string, std::filesystem::file_time_type> times;
    void setFolder(const std::filesystem::path& f);
    void refresh();                      // (re)loads the files changed on disk
    const Component* find(const std::string& name) const;
};
ComponentLibrary& components();

} // namespace wzui
