// Equivalences dashboard: for each node of the graph and each of its pins, the matching node / attribute in every target
// application (Maya / Arnold, Blender / Cycles, Unreal) and the value rules (scale, offset, invert, clamp, power).
// The rules are tools/maya/mappings/equivalences.json; the Maya bridge builds the Hypershade networks from them.
#pragma once
#include "app/Json.h"
#include "game/Graph.h"
#include "imgui.h"
#include <functional>
#include <map>
#include <string>

namespace mayashader {

class Equivalences {
public:
    bool load(const std::wstring& path);
    bool save();
    const std::wstring& path() const { return m_path; }
    // Draws the dashboard in the rectangle (ImGui pixels). `send` runs a Python line in Maya (false: not connected).
    void draw(bool* open, float dpi, const std::function<bool(const std::string&)>& send, bool mayaConnected);   // floating window
    // Coverage of a node for a target: 2 complete, 1 partial, 0 missing.
    int coverage(const std::string& nodeType, const std::string& target) const;
    // Asks Maya to check the rules (same as the button); false when not connected.
    bool requestCheck(const std::function<bool(const std::string&)>& send);

private:
    app::Json& rule(const std::string& nodeType, const std::string& target);   // created when missing
    void drawPinCell(const std::string& nodeType, const std::string& target, const std::string& pin, bool output, float w, float dpi,
                     const game::Value* ref);
    void pollCheck();

    std::wstring m_path, m_checkPath;
    app::Json m_doc;
    bool m_loaded = false, m_dirty = false;
    std::string m_selected = "shader.standardSurface";
    std::string m_status;
    char m_filter[64] = {};
    // Maya check: report written by wzx_shader_bridge.check_mappings
    app::Json m_check;
    long long m_checkTime = 0;
    bool m_checkPending = false;
};

// JSON text of a value (pretty printed, keys sorted).
std::string jsonText(const app::Json& j, int indent = 0);

} // namespace mayashader
