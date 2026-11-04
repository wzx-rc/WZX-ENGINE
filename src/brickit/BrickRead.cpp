// BrickIt - reading the instructions of a model (an official LEGO set of the LDraw OMR, or any model with STEPs), as
// the booklet but in 3D: step n shows the parts of steps 1..n, the parts added at step n outlined, the camera framed on
// what is built so far (free to orbit), the list of the parts of the step (thumbnail x count, colour), the submodel
// being built named over it. ← / → or the buttons turn the pages, Échap closes the reading.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace brickit {

bool BrickApp::hasSteps() const {
    for (const Brick& b : m_bricks)
        if (b.step >= 0) return true;
    return false;
}

void BrickApp::startReading(int step) {
    if (!hasSteps()) { toast("Ce modèle n'a pas d'étapes à lire"); return; }
    int last = 0;
    for (const Brick& b : m_bricks) last = std::max(last, b.step + 1);
    m_readSteps = last;
    m_reading = true;
    m_readStep = std::clamp(step, 1, m_readSteps);
    select(-1);
    frameReadStep();
    syncObjects();
}

void BrickApp::stopReading() {
    m_reading = false;
    syncObjects();
}

void BrickApp::readStepTo(int step) {
    if (!m_reading) return;
    const int s = std::clamp(step, 1, m_readSteps);
    if (s == m_readStep) return;
    m_readStep = s;
    frameReadStep();
    syncObjects();
}

// the camera on what is built so far, the direction kept (the reader orbits as he likes)
void BrickApp::frameReadStep() {
    float3 mn(1e30f), mx(-1e30f);
    for (const Brick& b : m_bricks) {
        if (b.step < 0 || b.step >= m_readStep) continue;
        const Box bx = m_build.aabbOf(b.part, b.rot, b.pos);
        mn = minv(mn, bx.mn);
        mx = maxv(mx, bx.mx);
    }
    if (mn.x > mx.x) return;
    Camera& cam = m_scene.camera;
    cam.target = (mn + mx) * 0.5f;
    const float r = std::max(length(mx - mn) * 0.5f, 1.0f);
    cam.distance = std::max(4.0f, r / std::tan(cam.fovY * 0.5f) * 1.1f);
    cam.focusDistance = cam.distance;
}

bool BrickApp::readHidden(int i) const {
    if (!m_reading || i < 0 || i >= (int)m_bricks.size()) return false;
    const int s = m_bricks[i].step;
    return s < 0 || s >= m_readStep;
}

std::vector<int> BrickApp::readStepBricks() const {
    std::vector<int> v;
    if (!m_reading) return v;
    for (int i = 0; i < (int)m_bricks.size(); ++i)
        if (m_bricks[i].step == m_readStep - 1) v.push_back(i);
    return v;
}

void BrickApp::readInput() {
    if (!m_reading || ImGui::GetIO().WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_PageDown)) readStepTo(m_readStep + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_PageUp)) readStepTo(m_readStep - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) readStepTo(1);
    if (ImGui::IsKeyPressed(ImGuiKey_End)) readStepTo(m_readSteps);
}

} // namespace brickit
