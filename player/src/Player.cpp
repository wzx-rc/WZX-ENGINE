#include "Player.h"
#include "app/Content.h"
#include "app/WzxRead.h"
#include "vp3d/EnvImage.h"

#include "imgui.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace player {

namespace {

std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

bool Player::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
                  Diligent::TEXTURE_FORMAT outputFormat, const std::string& shaderDirs, const Options& options,
                  std::string* error) {
    m_settings = lit::Settings();
    std::string hdri = options.hdri;
    std::string graphText;          // the graph of a scene file (empty: game/main.graph.json)
    app::Json village;              // the village of a scene file
    bool baked = options.bakedProbes;
    std::string probeFile = (fs::path(options.dataDir) / "game" / "probes" / (options.scene + ".probes")).u8string();
    const bool sceneFile = options.scene == "game" || (options.scene.size() > 4 && options.scene.substr(options.scene.size() - 4) == ".wzx");
    if (sceneFile) {
        // A build folder (game.json -> scene.wzx and graph.json, as the exported Windows game reads it) or a .wzx file.
        // Paths in the scene are relative to the data folder.
        fs::path data = fs::u8path(options.dataDir);
        fs::path scenePath = fs::u8path(options.scene), graphPath;
        if (options.scene == "game") {
            app::Json cfg;
            app::Json::parse(readText(data / "game.json"), cfg);
            scenePath = fs::u8path(cfg["scene"].str("scene.wzx"));
            graphPath = data / fs::u8path(cfg["graph"].str("graph.json"));
        }
        if (scenePath.is_relative()) scenePath = data / scenePath;
        app::Json j;
        if (!app::Json::parse(readText(scenePath), j) || j["format"].str() != "wzx-scene") {
            if (error) *error = "Scene not found or not a .wzx scene: " + scenePath.u8string();
            return false;
        }
        app::WzxRead rd;
        app::readWzxScene(j, m_scene, [&](const std::string& p) {
            fs::path f = fs::u8path(p);
            return (f.is_relative() ? data / f : f).wstring();
        }, scenePath.stem().u8string(), rd);
        for (const std::string& w : rd.warnings) std::fprintf(stderr, "%s\n", w.c_str());
        // live prefab instances: their parts from the .prefab files of the data folder (posed at rest until Play)
        m_dataDir = data.u8string();
        m_prefabs.absPath = [this](const std::string& p) {
            fs::path f = fs::u8path(p);
            return (f.is_relative() ? fs::u8path(m_dataDir) / f : f).wstring();
        };
        app::syncPrefabInstances(m_scene, m_prefabs, true);
        for (const std::string& w : m_prefabs.warnings) std::fprintf(stderr, "%s\n", w.c_str());
        const app::Json& r = j["render"];   // the editor's raster settings that Lit has
        m_settings.exposure = (float)r["exposure"].num(m_settings.exposure);
        m_settings.vignette = (float)r["vignette"].num(m_settings.vignette);
        m_settings.tonemapper = (int)r["tonemapper"].num(m_settings.tonemapper);
        m_settings.lightScale = (float)r["lightScale"].num(m_settings.lightScale);
        if (r["hdriSun"].type == app::Json::Bool) m_settings.envSun = r["hdriSun"].b;
        if (r["bakedProbes"].type == app::Json::Bool) baked = baked && r["bakedProbes"].b;
        const app::Json& h = j["environment"]["hdri"];
        if (hdri.empty()) {
            if (h.type != app::Json::String || h.s.empty()) hdri = "none";
            else if (h.s == "sky") hdri = "sky";
            else hdri = (fs::u8path(h.s).is_relative() ? data / fs::u8path(h.s) : fs::u8path(h.s)).u8string();
        }
        if (j["probes"].type == app::Json::String) probeFile = (data / fs::u8path(j["probes"].s)).u8string();
        if (!graphPath.empty()) graphText = readText(graphPath);
        if (graphText.empty() && j["graph"].type == app::Json::Object) graphText = app::jsonText(j["graph"]);
        if (graphText.empty()) graphText = "{\"format\": \"wzx-graph\", \"version\": 1, \"nodes\": [], \"links\": []}";
        village = j["village"];
    } else if (options.scene == "cornell") {
        // Cornell Box with the editor settings for it (App::loadScene): exposure 1, vignette 0.10, ACES, no HDRI.
        app::buildCornellBox(m_scene);
        m_settings.exposure = 1.0f;
        m_settings.vignette = 0.10f;
        if (hdri.empty()) hdri = "none";
    } else if (options.scene == "lookdev") {
        // LookDev scene with the editor settings for it (App::loadScene): exposure 1, vignette 0.18, ACES.
        app::buildLookDevScene(m_scene);
        m_settings.exposure = 1.0f;
        m_settings.vignette = 0.18f;
        if (hdri.empty()) hdri = "sky";
        m_scene.sun.enabled = options.sun;
    } else {
        // Studio scene as the editor loads it (App::loadScene): exposure 1.3, vignette 0.18, first default view, no HDRI.
        app::buildStudioScene(m_scene);
        m_settings.exposure = 1.3f;
        m_settings.vignette = 0.18f;
        std::vector<app::ViewPreset> views = app::defaultViews(m_scene, false);
        if (!views.empty()) m_scene.camera = views[0].cam;
        if (hdri.empty()) hdri = "none";
    }
    if (hdri != "none") {
        auto img = std::make_shared<vp3d::EnvImage>();
        bool ok = true;
        if (hdri == "sky") {
            vp3d::makeProceduralSky(*img);
        } else {
            std::string err;
            ok = vp3d::loadEnvImage(app::fromUtf8(hdri), *img, &err);
            if (!ok && error) *error = "HDRI not loaded: " + err;
        }
        if (!ok) return false;
        m_scene.env.hdri = img;
        if (options.scene == "lookdev") m_scene.env.hdriRotation = app::lookDevHdriRotation(*img);    }
    m_settings.ddgi = options.ddgi;
    m_settings.ddgiView = options.ddgiView;
    m_settings.ddgiProbeBudget = options.ddgiProbes;
    m_settings.showProbes = options.showProbes;
    m_settings.bakedProbes = baked;
    m_home = m_scene.camera;
    if (!m_renderer.init(device, context, factory, shaderDirs, outputFormat, error)) return false;
    if (baked) {   // probes baked by the editor for this scene (after the renderer exists)
        vp3d::lit::BakedProbes probes;
        if (vp3d::lit::loadBakedProbes(fs::u8path(probeFile), probes))
            m_renderer.setBakedProbes(probes);
    }

    // Dear ImGui: the HUD of the game
    ImGui::CreateContext();
    m_imgui = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 5.0f;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(8, 6);
    if (options.touch) {   // fingers need larger targets
        st.FramePadding = ImVec2(16, 12);
        st.ItemSpacing = ImVec2(12, 10);
        st.GrabMinSize = 24;
    }
    st.ScaleAllSizes(options.uiScale);
    st.FontSizeBase = options.touch ? 17.0f : 15.0f;
    st.FontScaleDpi = options.uiScale;
    bool font = false;
    if (!options.fontFile.empty() && std::ifstream(options.fontFile).good())
        font = io.Fonts->AddFontFromFileTTF(options.fontFile.c_str(), st.FontSizeBase) != nullptr;
    if (!font) io.Fonts->AddFontDefaultVector();
    if (!m_gui.init(device, context, outputFormat, error)) return false;

    if ((options.scene == "studio" || sceneFile) && options.game) {
        m_game = std::make_unique<Game>();
        m_game->setPrefabs(&m_prefabs);
        if (!m_game->init(&m_scene, options.dataDir, options.uiScale, options.touch, error, graphText)) return false;
        m_game->setDevice(options.device);
        if (village.type == app::Json::Array) m_game->loadVillage(village);
    }
    return true;
}

void Player::shutdown() {
    m_renderer.shutdown();
    if (m_imgui) {
        m_gui.shutdown();
        ImGui::DestroyContext();
        m_imgui = false;
    }
    m_game.reset();
}

void Player::frame(Diligent::ITextureView* output, uint32_t width, uint32_t height, float dt, const CameraControls& cam) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    ImGui::NewFrame();
    if (m_game) m_game->frame(io.DeltaTime, (float)width, (float)height, cam);
    ImGui::Render();
    m_renderer.render(m_scene, m_settings, output, width, height);
    m_gui.render(ImGui::GetDrawData(), output);
}

void Player::orbit(float dx, float dy) {
    vp3d::Camera& c = m_scene.camera;
    c.yaw -= dx * 0.005f;
    c.pitch = vp3d::clampf(c.pitch + dy * 0.005f, vp3d::radians(-5.0f), vp3d::radians(85.0f));
}

void Player::zoom(float factor) {
    vp3d::Camera& c = m_scene.camera;
    c.distance = vp3d::clampf(c.distance * factor, 5.0f, 400.0f);
}

} // namespace player
