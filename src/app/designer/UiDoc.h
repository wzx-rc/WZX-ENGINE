// WZX UI documents (.wzui.json): an interface described as data, built in the UI Designer (F8) and drawn with Dear ImGui
// by wzui::Runtime. A document holds windows; a window holds a tree of nodes (containers and widgets).
//
//   containers: Window, Panel (children placed freely at x / y), Row (children side by side), Column (stacked)
//   widgets   : Button, Label, Checkbox, Slider, Input, Combo, Color, Progress, Separator, Spacer
//   Component : an item designed in "Création Item" (ui/components/<component>.wzcomp.json), clickable like a Button
//
// Positions and sizes are in reference pixels (multiplied by the UI scale when drawn); w / h = 0 means automatic.
// Widgets are tied to the program by name: `bind` is the action of a Button, the value of a Checkbox / Slider / Input /
// Combo / Color / Progress (see Runtime::Bindings). The file is plain JSON so Claude can read and write it directly;
// the Designer reloads it when it changes on disk.
#pragma once
#include "imgui.h"
#include <string>
#include <vector>

namespace wzui {

enum class Kind { Window, Panel, Row, Column, Button, Label, Checkbox, Slider, Input, Combo, Color, Progress, Separator, Spacer, Component, Count };

const char* kindName(Kind k);            // "Button"...
bool kindFromName(const std::string& n, Kind& out);
bool isContainer(Kind k);                // Window, Panel, Row, Column
bool isFreeLayout(Kind k);               // Window, Panel: children placed at x / y

struct Node {
    int uid = 0;                         // runtime identity (not saved)
    Kind kind = Kind::Button;
    std::string id;                      // name used by the code and by Claude ("playButton")
    std::string text;                    // label, title, or the text of a Label
    std::string tooltip;
    std::string bind;                    // action (Button) or value name (other widgets)
    float x = 0, y = 0, w = 0, h = 0;    // reference pixels, relative to the parent's content; w / h = 0: automatic
    float minV = 0, maxV = 1;            // Slider / Progress range
    std::vector<std::string> items;      // Combo choices
    std::string component;               // Component: name of the item in ui/components
    bool hasColor = false, hasBg = false;
    ImVec4 color = ImVec4(1, 1, 1, 1);   // text colour
    ImVec4 bg = ImVec4(0.16f, 0.17f, 0.20f, 1);   // fill of a Panel / Button
    float spacing = 6, padding = 8, rounding = 4;
    bool visible = true;
    std::vector<Node> children;
};

struct Doc {
    std::string name = "Interface";
    bool showInEditor = false;           // drawn live in the WZX editor (real windows, bound to the code)
    std::vector<Node> windows;           // roots, all of kind Window
    int nextUid = 1;

    Node makeNode(Kind k) ;              // a new node with sensible defaults and a unique id
    void assignUids();                   // after loading or pasting
    Node* find(int uid);
    Node* parentOf(int uid);             // nullptr for a window or an unknown uid
    bool contains(int ancestorUid, int uid);   // uid is ancestorUid or inside it
    std::string uniqueId(const std::string& base);
};

bool load(const std::string& path, Doc& doc, std::string* error = nullptr);
bool save(const std::string& path, const Doc& doc);
bool fromJson(const std::string& text, Doc& doc, std::string* error = nullptr);
std::string toJson(const Doc& doc);

} // namespace wzui
