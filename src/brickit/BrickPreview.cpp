// BrickIt - 3D viewer of the part sheet (web: partViewer / pvRender): the part alone in the thumbnail studio (same
// lights, plastic, ACES 0.92, camera 24 degrees along (1, 0.8, 1.3) at radius / sin 12 x 1.16), turned around its own
// centre in camera space (yaw around Y, pitch around the camera right axis) so that the lighting stays the thumbnail one.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>

namespace brickit {

// Linear value whose ACES (Hill fit) x exposure then sRGB gives the 8-bit target (the card colour behind the part).
float acesInverse(float target8, float exposure) {
    auto aces = [](float x) {
        float a = x * (x + 0.0245786f) - 0.000090537f, b = x * (0.983729f * x + 0.4329510f) + 0.238081f;
        return std::clamp(a / b, 0.0f, 1.0f);
    };
    auto srgb = [](float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; };
    float lo = 0, hi = 4;
    for (int i = 0; i < 40; ++i) {
        float m = (lo + hi) * 0.5f;
        if (srgb(aces(m * exposure)) * 255 < target8) lo = m;
        else hi = m;
    }
    return (lo + hi) * 0.5f;
}

void BrickApp::renderPreview() {
    if (m_infoPart < 0) return;
    if (!m_pvBuilt) {
        m_pv = Scene();
        m_pvMat = setupStudio(m_pv);
        m_pv.geometryVersion = 0x50000000u;   // never equal to the viewport or thumbnail scenes
        m_pvBuilt = true;
        m_pvObj = -1;
    }
    const Part& p = m_lib.parts[m_infoPart];
    if (m_pvObj < 0 || m_pv.objects[m_pvObj].name != p.name) {   // one part at a time
        m_pv.meshes.clear();
        m_pv.objects.clear();
        Object o;
        o.name = p.name;
        o.mesh = m_pv.addMesh(p.name, p.mesh);
        o.material = m_pvMat;
        o.useRot = true;
        m_pvObj = m_pv.addObject(o);
        ++m_pv.geometryVersion;
    }
    // background: the card colour (#25262b) after the tone mapping
    const float bg[3] = {acesInverse(0x25, 0.92f), acesInverse(0x26, 0.92f), acesInverse(0x2b, 0.92f)};
    m_pv.env.top = m_pv.env.horizon = m_pv.env.bottom = float3(bg[0], bg[1], bg[2]);
    Camera& c = m_pv.camera;
    c.fovY = radians(24.0f);
    c.yaw = std::atan2(1.0f, 1.3f);
    c.pitch = std::atan2(0.8f, std::sqrt(1.0f + 1.69f));
    c.target = float3(0);
    const float3 centre = (p.bmin + p.bmax) * 0.5f;
    const float rad = std::max(length(p.bmax - p.bmin) * 0.5f, 0.2f);
    c.distance = rad / std::sin(radians(12.0f)) * 1.16f;
    c.focusDistance = c.distance;
    float3 r, u, f;
    c.basis(r, u, f);
    const float4 q = qMul(qAxisAngle(r, m_pvPitch), qAxisAngle(float3(0, 1, 0), m_pvYaw));
    Object& o = m_pv.objects[m_pvObj];
    o.rot = q;
    o.position = float3(0) - qRot(q, centre);
    ++m_pv.objectsVersion;
    RenderSettings rs = m_rs;
    rs.raster = true;
    rs.exposure = 0.92f;
    rs.tonemapper = 0;
    rs.idPass = false;
    rs.vignette = 0.0f;
    rs.msaaSamples = 4;
    rs.pixelScale = 1;
    const float w = std::max(16.0f, m_pvRect.z - m_pvRect.x), h = std::max(16.0f, m_pvRect.w - m_pvRect.y);
    ViewRect vp{0, 0, (int)(m_pvRect.z > 0 ? w : 404 * m_ui), (int)(m_pvRect.z > 0 ? h : 323 * m_ui)};
    m_renderer.renderToTexture(m_pv, rs, vp, -1);
}

} // namespace brickit
