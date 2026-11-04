// UI Designer (F8): builds interfaces like Qt Designer, inside WZX, with Dear ImGui.
//
//   left  : widget palette (click to add into the selection, or drag onto the canvas) and the hierarchy tree
//           (drag to re-parent, right click for duplicate / delete / move / wrap)
//   centre: canvas with the windows of the document; click selects, drag moves (or drops into another container),
//           the corner handle resizes, middle mouse pans, Ctrl+wheel zooms
//   right : properties of the selection (id, text, binding, position, size, colours...) or of the document
//
// Documents are ui/*.wzui.json (see UiDoc.h), saved automatically after each change and reloaded when another tool
// (Claude, git, a text editor) changes them. Documents marked "showInEditor" are drawn live in the editor with
// drawLiveDocs(), their widgets tied to the program through `bindings`.
#pragma once
#include "UiDoc.h"
#include "UiRuntime.h"
#include <filesystem>
#include <string>
#include <vector>

namespace wzui {

class Designer {
public:
    void setFolder(const std::filesystem::path& folder);   // ui/ of the project; creates an example document if empty
    bool open(const std::filesystem::path& file);
    void toggle() { m_visible = !m_visible; m_focusOnShow = m_visible; }
    bool visible() const { return m_visible; }
    void draw(float uiScale);            // full-window Designer (when visible); also watches the file on disk
    void drawLiveDocs(float uiScale);    // the documents marked "showInEditor", as real windows
    Bindings bindings;                   // shared by the canvas preview and the live documents

    // for tests and the command API
    Doc& doc() { return m_doc; }
    int selected() const { return m_sel; }
    void select(int uid) { m_sel = uid; }
    const std::filesystem::path& file() const { return m_file; }
    bool saveNow();
    const DrawCtx& lastCanvas() const { return m_ctx; }          // rectangles of the nodes drawn on the canvas
    ImRect paletteRect(Kind k) const { return m_paletteRects[(int)k]; }
    bool preview() const { return m_preview; }
    void setPreview(bool on) { m_preview = on; }
    void setZoom(float z) { m_zoom = z; }
    void setPan(ImVec2 p) { m_pan = p; }

private:
    enum class Drag { None, Pending, Move, Resize, Pan, Palette };

    void toolbar();
    void palette();
    void hierarchy();
    void hierarchyNode(Node& n, int depth);
    void properties();
    void canvas(float scale);
    void statusBar();

    void pushUndo();
    void changed();                      // after an edit: autosave
    void undo();
    void redo();
    void addNode(Kind k, int containerUid, ImVec2 refPos, bool atPos, int index = -1);
    void removeNode(int uid);
    void duplicate(int uid);
    void moveSibling(int uid, int dir);
    void wrap(int uid, Kind container);
    bool reparent(int uid, int containerUid, int index, ImVec2 refPos, bool atPos);
    int containerFor(int uid);           // the selection if it is a container, else its parent
    int dropContainerAt(ImVec2 mouse, int excludeUid);
    int insertIndex(int containerUid, ImVec2 mouse, int excludeUid);
    const DrawCtx::Hit* hit(int uid) const;
    void refreshFiles();
    void pollDisk();
    void toast(const std::string& t);
    float snap(float v) const;

    bool m_visible = false, m_focusOnShow = false;
    std::filesystem::path m_folder, m_file;
    std::vector<std::filesystem::path> m_files;
    std::filesystem::file_time_type m_fileTime{};
    double m_nextPoll = 0;
    bool m_diskChanged = false;

    Doc m_doc;
    std::vector<Doc> m_undo, m_redo;
    bool m_dirty = false, m_autosave = true, m_preview = false, m_grid = true;
    double m_saveAt = -1;
    int m_sel = 0, m_hover = 0;
    float m_zoom = 1.0f;
    ImVec2 m_pan = ImVec2(40, 40);

    DrawCtx m_ctx;                       // hits of the last canvas frame
    float m_scale = 1;
    ImRect m_canvasRect;
    Drag m_drag = Drag::None;
    ImVec2 m_dragStartMouse, m_grab;
    float m_startX = 0, m_startY = 0, m_startW = 0, m_startH = 0;
    int m_dropTarget = 0;

    struct Pending { int op = 0, uid = 0, target = 0; } m_pending;   // tree actions, applied after the tree is drawn
    std::vector<Doc> m_live;             // other documents marked showInEditor
    double m_nextLive = 0;
    void refreshLive();

    ImRect m_paletteRects[(int)Kind::Count];
    std::string m_toast, m_lastAction;
    double m_toastUntil = 0;
    char m_newName[64] = "";
};

Designer& designer();   // the editor's instance

} // namespace wzui
