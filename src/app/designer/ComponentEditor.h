// "Création Item": the Designer's space for designing UI components the way Figma does.
//
//   toolbar : component file, new, save, undo / redo, drawing tools (Sélection V, Rectangle R, Ellipse O, Texte T, Ligne L)
//   left    : layers (top first; eye = visible, drag to reorder) and the component itself (size, preview text)
//   centre  : zoomed canvas: draw a shape by dragging with a tool, click to select, drag to move, 8 handles to resize,
//             the amber corner of the frame resizes the whole component (Alt: without stretching the layers),
//             wheel / middle mouse pan, Ctrl+wheel zoom; state bar above it: Normal, Survol, Appuyé, Désactivé, Tester
//   right   : properties of the selected layer: geometry, text, and its look in the current state
//   bottom  : the component in its four states, at 100 %
//
// Components are ui/components/<name>.wzcomp.json (see Component.h), saved automatically and reloaded when changed on disk.
#pragma once
#include "Component.h"
#include "imgui_internal.h"
#include <filesystem>
#include <string>
#include <vector>

namespace wzui {

enum class Tool { Select, Rect, Ellipse, Text, Line };

class ComponentEditor {
public:
    void setFolder(const std::filesystem::path& folder);   // ui/components; creates "bouton_ambre" if empty
    bool open(const std::filesystem::path& file);
    bool openByName(const std::string& name);
    bool create(const std::string& name, bool asButton);  // a new component file, opened
    void tick();                         // every frame: autosave, reload when changed on disk
    void toolbar(float uiScale);         // the row of the tab
    void draw(float uiScale);            // the panels, filling the rest of the window
    void handleKeys();                   // when the Designer window is focused
    bool saveNow();

    // for tests and the Designer
    Component& component() { return m_c; }
    const std::filesystem::path& file() const { return m_file; }
    std::vector<std::filesystem::path> files() const;
    int selected() const { return m_sel; }
    void select(int uid) { m_sel = uid; }
    Tool tool() const { return m_tool; }
    void setTool(Tool t) { m_tool = t; }
    int state() const { return m_state; }
    void setState(int s) { m_state = s; }
    bool testing() const { return m_test; }
    void setTest(bool on) { m_test = on; }
    void setZoom(float z) { m_zoom = z; }
    void center() { m_center = true; }
    ImVec2 toScreen(ImVec2 unit) const { return ImVec2(m_origin.x + unit.x * m_s, m_origin.y + unit.y * m_s); }
    ImRect canvasRect() const { return m_canvasRect; }
    size_t undoCount() const { return m_undo.size(); }
    void undo();
    void redo();
    const std::string& lastClick() const { return m_lastClick; }

private:
    enum class Drag { None, Pending, Move, Handle, Draw, Pan, Frame };

    void layers();
    void componentProps();
    void properties();
    void stateBar();
    void canvas();
    void strip();

    void edited();                       // after any change: undo step (once per gesture) + autosave
    void removeLayer(int uid);
    void duplicateLayer(int uid);
    void moveLayer(int uid, int dir);    // dir +1 = towards the top
    int layerAt(ImVec2 screen) const;
    ImRect layerRect(const Layer& l) const;   // screen, normal look (no state offset)
    int handleAt(ImVec2 screen) const;
    ImVec2 handlePos(const Layer& l, int i) const;
    void toast(const std::string& t);

    std::filesystem::path m_folder, m_file;
    std::filesystem::file_time_type m_fileTime{};
    double m_nextPoll = 0, m_saveAt = -1;
    bool m_dirty = false;

    Component m_c, m_before;
    std::vector<Component> m_undo, m_redo;
    bool m_gesture = false;

    int m_sel = 0, m_hover = 0, m_state = StNormal;
    Tool m_tool = Tool::Select;
    bool m_test = false, m_center = true;
    std::string m_text = "Valider";      // replaces {text} in the preview
    std::string m_lastClick;
    float m_zoom = 3.0f, m_s = 3.0f, m_ui = 1.0f;
    ImVec2 m_pan = ImVec2(0, 0), m_origin = ImVec2(0, 0);
    ImRect m_canvasRect;

    Drag m_drag = Drag::None;
    ImVec2 m_dragStart, m_drawStart;
    int m_handle = -1;
    Layer m_startL;
    Component m_startC;

    std::string m_toast;
    double m_toastUntil = 0;
    char m_newName[64] = "";
};

} // namespace wzui
