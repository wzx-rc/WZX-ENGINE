#include "Player.h"
#include "app/Content.h"
#include "vp3d/EnvImage.h"

#include "imgui.h"

#include <fstream>

namespace player {

bool Player::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
                  Diligent::TEXTURE_FORMAT outputFormat, const std::string& shaderDirs, const Options& options,
                  std::string* error) {
    m_settings = lit::Settings();
    std::string hdri = options.hdri;
    if (options.scene == "lookdev") {
        // LookDev scene with the editor's settings for it (App::loadScene): exposure 1, vignette 0.18, ACES.
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
        if (options.scene == "lookdev") m_scene.env.hdriRotation = app::lookDevHdriRotation(*img);
    }
    m_home = m_scene.camera;
    if (!m_renderer.init(device, context, factory, shaderDirs, outputFormat, error)) return false;

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

    if (options.scene != "lookdev" && options.game) {
        m_game = std::make_unique<Game>();
        if (!m_game->init(&m_scene, options.dataDir, options.uiScale, options.touch, error)) return false;
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
