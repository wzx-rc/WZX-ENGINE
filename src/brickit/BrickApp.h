// BrickIt - native brick builder (PC, DirectX 12) on the vp3d renderer, with the WZX Engine Dear ImGui theme.
#pragma once
#include "Library.h"
#include "app/ImGuiLayer.h"
#include "vp3d/Renderer.h"
#include <windows.h>
#include <string>
#include <vector>

namespace brickit {

using namespace vp3d;

struct Options {
    int width = 1600, height = 900;
    std::wstring shotPath;    // --shot file.png: window capture then exit
    int shotSpp = 64;         // --spp N: path tracing samples before the capture
    int shotFrames = 20;      // raster: frames before the capture
    bool grid = false;        // --grid: every part of the library side by side
    int bench = 0;            // --bench N: N random bricks
    bool pathTrace = false;   // --pt: progressive path tracing
    int bounces = 0;          // --bounces N (path tracing)
    int bevelSpp = -1;        // --bevel-spp N (0 = bevel off)
    bool opaque = false;      // --opaque: benchmark bricks in standard colours only
    int denoise = -1;         // --denoise 0|1
    int ppf = 1;              // --ppf N: path tracing samples per frame
    bool noHdri = false, noGround = false;   // --no-hdri, --no-ground (render cost tests)
    float groundSize = 2000.0f;   // --ground size (cm)
    float ptMoveScale = 0.5f;     // path tracing resolution while the view moves
    bool sss = true;              // --no-sss
    bool demo = false;            // --demo: showcase of a few bricks (beauty captures)
    bool cleanGround = false;     // --clean: floor without the grid lines
    float exposure = 1.5f;        // --exposure
    int tonemapper = 2;           // --tonemap 0 ACES, 1 AgX, 2 AgX Punchy, 3 Standard
    float fingerprints = 0.35f;   // --fingerprints: strength of the roughness mask (0 = off)
    float yaw = -1000, pitch = -1000;   // --yaw / --pitch degrees (camera)
    float zoom = 1.0f;        // --zoom f: camera distance x f after framing everything
};

// One brick of the model.
struct Brick {
    int part = -1;            // index in Library::parts
    int color = 0;            // index in Library::palette
    float3 pos;
    float4 rot{0, 0, 0, 1};
};

class BrickApp {
public:
    bool init(HINSTANCE inst, const Options& opt);
    int run();
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    void frame();
    void drawUi();
    void cameraInput();
    void rebuildScene();      // bricks -> vp3d scene (meshes once per part, one object per brick)
    void syncObjects();       // brick transforms and colours only
    int meshOf(int part);     // scene mesh of a part (uploaded once)
    void buildPartGrid();
    void buildBench(int count);
    void buildDemo();
    int colorIndex(int ldrawId) const;   // palette index of an LDraw colour code
    void setupStage();        // ground, HDRI, materials of the palette
    void frameAll();
    bool savePng(const std::wstring& path, std::vector<uint8_t> rgba, uint32_t w, uint32_t h);

    HWND m_hwnd = nullptr;
    Options m_opt;
    std::wstring m_exeDir;
    float m_dpi = 1.0f;
    bool m_running = true, m_minimized = false;
    Renderer m_renderer;
    app::ImGuiLayer m_gui;
    RenderSettings m_rs;
    Scene m_scene;
    Library m_lib;
    std::vector<Brick> m_bricks;
    std::vector<int> m_partMesh;       // part -> scene mesh (-1 = not uploaded)
    std::vector<int> m_colorMaterial;  // palette index -> scene material
    int m_firstBrickObject = 0;        // scene objects before it: the stage
    bool m_geometryDirty = true;
    ViewRect m_vp;
    int m_frame = 0;
    double m_cpuMs = 0, m_frameMs = 0;
    LARGE_INTEGER m_qpf{}, m_last{};
    std::string m_status;
    double m_lastMove = -10.0;   // time of the last camera or scene change (dynamic resolution)
    std::vector<Scene::MeshLod> m_lodsOff;   // the levels of detail while they are turned off (L)
};

} // namespace brickit
