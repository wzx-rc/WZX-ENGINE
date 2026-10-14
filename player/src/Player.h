// WZX Player - the game side shared by every platform: scene, camera input and the Lit renderer.
// The platform files (main_win32.cpp, main_headless.cpp, ios/main_ios.mm) create the Diligent device and
// the window or image, then call frame() for each image.
#pragma once
#include "lit/LitRenderer.h"
#include "vp3d/Scene.h"

#include <memory>
#include <string>

namespace player {

struct Options {
    std::string scene = "lookdev";   // milestone 1: the LookDev scene only
    std::string hdri = "sky";        // "sky" (built-in procedural sky), "none", or an .hdr / .exr file
    bool sun = false;                // scene sun on (as in RTS play), for the parity tests
};

class Player {
public:
    bool init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
              Diligent::TEXTURE_FORMAT outputFormat, const std::string& shaderDirs, const Options& options,
              std::string* error);
    void shutdown() { m_renderer.shutdown(); }
    void frame(Diligent::ITextureView* output, uint32_t width, uint32_t height);

    // Camera: orbit around the target (pixels of drag), zoom (factor on the distance), back to the default view.
    void orbit(float dx, float dy);
    void zoom(float factor);
    void resetView() { m_scene.camera = m_home; }

    vp3d::Scene& scene() { return m_scene; }
    const lit::LitRenderer& renderer() const { return m_renderer; }

private:
    vp3d::Scene m_scene;
    vp3d::Camera m_home;
    lit::Settings m_settings;
    lit::LitRenderer m_renderer;
};

} // namespace player
