// Shader Library: the materials of a folder on the PC (one sub-folder per category, one folder per material with its
// graph, thumbnail and tags) and the materials of the Maya scene (listed by wzx_shader_bridge.scene_materials).
#pragma once
#include "app/ImGuiLayer.h"
#include "app/Json.h"
#include "game/Graph.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace mayashader {

class Library {
public:
    struct Host {
        std::function<bool(const std::string&)> send;                  // Python line to Maya (false: not connected)
        std::function<bool()> mayaConnected;
        std::function<const game::Graph&()> graph;                      // current editor graph (root)
        std::function<void(const std::string& graphJson)> openInGraph;  // replace the current material by this graph
        std::function<std::string()> currentKey;                        // key of the graph opened in Maya
        bool acescg = true;
    };
    void init(app::ImGuiLayer& gui, const std::wstring& settingsFile, Host host);
    // floating windows: the materials of the library folder, and the materials of the Maya scene
    void drawLibraryWindow(bool* open, float dpi);
    void drawSceneWindow(bool* open, float dpi);
    // test / scripts: another library folder (not remembered), and saving the current material into it
    void setRoot(const std::wstring& root) { m_root = root; strncpy_s(m_rootBuf, utf8Root().c_str(), _TRUNCATE); scan(); }
    void setTab(int t) { m_forceTab = t; }
    void setSceneList(bool on) { m_sceneList = on; }
    bool saveMaterial(const std::string& name, const std::string& category, const std::string& tags) { return saveCurrent(name, category, tags); }

private:
    struct Item {
        std::string name, category, tags;
        std::wstring folder;
        ImTextureID thumb = 0;
        long long thumbTime = 0;
    };
    struct SceneItem {
        std::string sg, shader, shaderType, wzx, graph;
        int objects = 0, unknown = 0;
        ImTextureID thumb = 0;
    };
    void scan();
    bool saveCurrent(const std::string& name, const std::string& category, const std::string& tags);
    bool saveGraph(const game::Graph& g, const std::string& name, const std::string& category, const std::string& tags);
    ImTextureID loadThumb(const std::wstring& png);
    ImTextureID graphThumb(const std::string& graphJson, int size);
    void pollScene();
    void freeSceneThumbs();

    app::ImGuiLayer* m_gui = nullptr;
    Host m_host;
    std::wstring m_settingsFile, m_root;
    std::vector<Item> m_items;
    std::vector<std::string> m_categories;
    int m_category = -1;   // -1 = all
    char m_search[96] = {}, m_rootBuf[512] = {}, m_sceneFilter[96] = {};
    int m_sceneSel = -1;          // selected sample slot
    bool m_sceneList = false;     // compact list instead of the sample slots
    float m_slotSize = 74;
    float m_cardSize = 120;
    char m_saveName[96] = {}, m_saveCat[96] = "Default", m_saveTags[160] = {};
    int m_tab = 0, m_forceTab = -1;
    std::string utf8Root() const;
    std::string m_status;
    // scene
    std::vector<SceneItem> m_scene;
    std::wstring m_scenePath, m_importPath;
    long long m_sceneTime = 0, m_importTime = 0;
    bool m_scenePending = false, m_importPending = false, m_sceneAsked = false;
};

} // namespace mayashader
