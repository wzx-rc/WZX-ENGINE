// Material preview of the shader graph: the graph is evaluated on the CPU (constants, ramp, checker, noise, image,
// colour utilities, aiStandardSurface / aiMixShader / aiFlat), the base colour is baked into a texture and the
// sphere is path traced by vp3d with an HDRI and a key / fill / rim light rig, as close to Arnold as practical.
#pragma once
#include "app/ImGuiLayer.h"
#include "game/Graph.h"
#include "vp3d/Renderer.h"
#include <string>

namespace mayashader {

class Preview {
public:
    void init(vp3d::Renderer& r, app::ImGuiLayer& gui, const std::wstring& hdri);
    void shutdown() {}
    // Draws the preview in the rectangle (ImGui pixels) and renders the next samples. Returns true when the
    // renderer output texture is drawn this frame (ImGuiLayer::render must then return it to UAV state).
    bool draw(const game::Graph& g, ImVec2 pos, ImVec2 size, float dpi);
    bool busy() const { return m_busy; }
    void setLights(bool on) { m_lights = on; }
    void setAcescg(bool on) { m_acescg = on; m_graphKey.clear(); }   // graph colours in ACEScg (Maya default rendering space)

private:
    void applyGraph(const game::Graph& g);
    void setHdri(int index);

    vp3d::Renderer* m_r = nullptr;
    app::ImGuiLayer* m_gui = nullptr;
    vp3d::Scene m_scene;
    vp3d::RenderSettings m_rs;
    std::string m_graphKey, m_viewKey;
    std::vector<std::wstring> m_hdris;
    int m_hdri = -1;
    float m_yaw = 0.0f, m_pitch = 0.12f, m_dist = 4.6f, m_exposure = 0.0f, m_envRot = 0.0f;
    bool m_lights = true, m_background = true, m_acescg = true;
    bool m_busy = true;
    bool m_sceneDirty = true;
    bool m_paused = false;
    vp3d::ViewRect m_lastView;
};

} // namespace mayashader
