#include "Player.h"
#include "app/Content.h"
#include "vp3d/EnvImage.h"

namespace player {

bool Player::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::IEngineFactory* factory,
                  Diligent::TEXTURE_FORMAT outputFormat, const std::string& shaderDirs, const Options& options,
                  std::string* error) {
    // LookDev scene with the editor's settings for it (App::loadScene): exposure 1, vignette 0.18, ACES.
    app::buildLookDevScene(m_scene);
    m_settings = lit::Settings();
    m_settings.exposure = 1.0f;
    m_settings.vignette = 0.18f;
    if (options.hdri != "none") {
        auto img = std::make_shared<vp3d::EnvImage>();
        bool ok = true;
        if (options.hdri == "sky") {
            vp3d::makeProceduralSky(*img);
        } else {
            std::string err;
            ok = vp3d::loadEnvImage(app::fromUtf8(options.hdri), *img, &err);
            if (!ok && error) *error = "HDRI not loaded: " + err;
        }
        if (!ok) return false;
        m_scene.env.hdri = img;
        m_scene.env.hdriRotation = app::lookDevHdriRotation(*img);
    }
    m_home = m_scene.camera;
    return m_renderer.init(device, context, factory, shaderDirs, outputFormat, error);
}

void Player::frame(Diligent::ITextureView* output, uint32_t width, uint32_t height) {
    m_renderer.render(m_scene, m_settings, output, width, height);
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
