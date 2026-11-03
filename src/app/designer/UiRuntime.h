// Draws a wzui::Doc with Dear ImGui: live (real windows, widgets tied to the program through Bindings) or as the canvas
// of the UI Designer (same drawing, inert widgets, rectangle of every node recorded for picking).
#pragma once
#include "UiDoc.h"
#include "imgui_internal.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wzui {

// Ties widgets to the program by name. Unbound names get their own storage, so a document works (and previews) before
// any code is written for it. Example:
//   bindings.actions["play"] = [&] { host.play(); };
//   bindings.floats["sunAngle"] = &scene.sunAngle;
struct Bindings {
    std::map<std::string, std::function<void()>> actions;
    std::map<std::string, bool*> bools;
    std::map<std::string, float*> floats;
    std::map<std::string, int*> ints;
    std::map<std::string, std::string*> strings;
    std::map<std::string, ImVec4*> colors;
    std::function<void(const std::string&)> onUnboundAction;   // a Button whose action has no code yet

    bool& b(const std::string& k);
    float& f(const std::string& k);
    int& i(const std::string& k);
    std::string& s(const std::string& k);
    ImVec4& c(const std::string& k);

private:
    std::map<std::string, bool> m_b;
    std::map<std::string, float> m_f;
    std::map<std::string, int> m_i;
    std::map<std::string, std::string> m_s;
    std::map<std::string, ImVec4> m_c;
};

struct DrawCtx {
    bool design = false;                         // Designer canvas: widgets inert, hidden nodes drawn faded
    float scale = 1;                             // reference pixels -> screen pixels
    Bindings* bindings = nullptr;
    struct Hit { int uid; ImRect rect; ImRect content; };   // content: where a free-layout container places children
    std::vector<Hit> hits;                       // every node drawn, parents before children
};

// Draws the content of `node` with its top left corner at `pos` (screen pixels); returns its size.
ImVec2 drawNode(Node& node, ImVec2 pos, DrawCtx& ctx);
// Designer: a window drawn as a frame (title bar + content) at `pos`; returns its rectangle.
ImRect drawWindowFrame(Node& window, ImVec2 pos, DrawCtx& ctx, bool selected);
float titleBarHeight(float scale);
// Live: every window of the document as a real ImGui window.
void drawLive(Doc& doc, Bindings& bindings, float scale);

} // namespace wzui
