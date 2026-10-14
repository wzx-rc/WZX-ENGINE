// WZX Player - the game side shared by every platform: scene, game, HUD and the Lit renderer.
// The platform files (main_win32.cpp, main_headless.cpp, ios/main_ios.mm) create the Diligent device and
// the window or image, feed the input to Dear ImGui (plus the camera controls), then call frame() for each image.
#pragma once
#include "Game.h"
#include "ImGuiDiligent.h"
#include "lit/LitRenderer.h"
#include "vp3d/Scene.h"

#include <memory>
#include <string>

namespace player {

struct Options {
    std::string scene = "studio";    // "studio" (the game), "lookdev" (the parity scene) or "cornell" (the DDGI parity scene)
    std::string hdri;                // "" = the scene's own (studio: none, lookdev: sky), "sky", "none", or an .hdr / .exr file
    bool sun = false;                // LookDev: scene sun on (as in RTS play), for the parity tests
    bool game = true;                // studio: play the game (graph, houses, HUD)
    std::string dataDir;             // folder holding game/ and assets/ (studio)
    float uiScale = 1.0f;            // HUD scale (DPI)
    bool touch = false;              // touch screen: touch buttons, no screen edge scrolling
    std::string fontFile;            // HUD font (TTF), empty = the built-in one
    // DDGI probes without ray tracing (lit::Settings): on, view (0 full, 1 indirect only, 2 direct only), budget, debug spheres
    bool ddgi = false;
    int ddgiView = 0;
    int ddgiProbes = 2048;
    bool showProbes = false;
    bool bakedProbes = true;    // read <data>/game/probes/<scene>.probes when it exists (Lit without live probes)
};

class Player {
public:
    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
              Diligent::TEXTURE_FORMAT outputFormat, const std::string& shaderDirs, const Options& options,
              std::string* error);
    void shutdown();
    // One image: the game and the HUD (input already given to ImGui), the 3D view, then the HUD over it.
    void frame(Diligent::ITextureView* output, uint32_t width, uint32_t height, float dt = 1.0f / 60.0f,
               const CameraControls& cam = CameraControls());

    // LookDev camera: orbit around the target (pixels of drag), zoom (factor on the distance), back to the default view.
    void orbit(float dx, float dy);
    void zoom(float factor);
    void resetView() { m_scene.camera = m_home; }
    bool playing() const { return m_game != nullptr; }
    const Game* game() const { return m_game.get(); }
    void setSafeArea(float left, float top, float right, float bottom) { if (m_game) m_game->setSafeArea(left, top, right, bottom); }

    vp3d::Scene& scene() { return m_scene; }
    const lit::LitRenderer& renderer() const { return m_renderer; }

private:
    vp3d::Scene m_scene;
    vp3d::Camera m_home;
    lit::Settings m_settings;
    lit::LitRenderer m_renderer;
    ImGuiDiligent m_gui;
    std::unique_ptr<Game> m_game;
    bool m_imgui = false;
};

} // namespace player
