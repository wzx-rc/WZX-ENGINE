// WZX Player, headless: renders one image of the scene with the Lit renderer into a PNG, without a window.
// Used to compare the targets with each other and with the editor (docs/RENDU_PARITE.md):
//   WZXPlayerHeadless --out lookdev.png [--width 1280 --height 720] [--hdri sky|none|file.hdr]
//                     [--backend d3d12|vk] [--shaders dir1;dir2] [--sun] [--scene lookdev|studio|cornell] [--data dir]
//                     [--ddgi 1 --ddgi-view 0|1|2 --probes N --show-probes 1]   (DDGI probes without ray tracing)
// --scene studio plays the game (graph, houses, HUD) for --frames images; --data is the folder of game/ and assets/.
// Scripted input for the game tests: --click x,y,frame  --drag x0,y0,x1,y1,frame (over 10 frames)  --key K,frame (A..Z, DEL).
// Windows uses DirectX 12 by default, other systems Vulkan (on Linux, Mesa lavapipe renders without a GPU).
#include "Player.h"
#include "imgui.h"

#include "Graphics/GraphicsEngine/interface/EngineFactory.h"
#if D3D12_SUPPORTED
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#endif
#if VULKAN_SUPPORTED
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <objbase.h>
#endif

using namespace Diligent;

namespace {

// Scripted mouse and keys of the game tests, given to ImGui before the frame `f`.
void scriptedInput(const std::vector<std::pair<std::string, std::string>>& events, int f) {
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& e : events) {
        float v[5] = {};
        if (e.first == "--key") {
            char key[16] = {};
            int at = 0;
            if (std::sscanf(e.second.c_str(), "%15[^,],%d", key, &at) != 2) continue;
            ImGuiKey k = std::strcmp(key, "DEL") == 0 ? ImGuiKey_Delete : (ImGuiKey)(ImGuiKey_A + (key[0] - 'A'));
            if (f == at) io.AddKeyEvent(k, true);
            if (f == at + 1) io.AddKeyEvent(k, false);
        } else if (e.first == "--click") {
            if (std::sscanf(e.second.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]) != 3) continue;
            if (f == (int)v[2]) {
                io.AddMousePosEvent(v[0], v[1]);
                io.AddMouseButtonEvent(0, true);
            }
            if (f == (int)v[2] + 1) io.AddMouseButtonEvent(0, false);
        } else if (std::sscanf(e.second.c_str(), "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5) {
            int t = f - (int)v[4];
            if (t == 0) {
                io.AddMousePosEvent(v[0], v[1]);
                io.AddMouseButtonEvent(0, true);
            }
            if (t >= 1 && t <= 10) io.AddMousePosEvent(v[0] + (v[2] - v[0]) * t / 10.0f, v[1] + (v[3] - v[1]) * t / 10.0f);
            if (t == 11) io.AddMouseButtonEvent(0, false);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string out = "lit.png", backend, shaders = "shaders";
    uint32_t width = 1280, height = 720;
    int frames = 3;
    std::vector<std::pair<std::string, std::string>> events;
    player::Options opt;
    opt.scene = "lookdev";   // the parity scene by default
    opt.dataDir = ".";
#if D3D12_SUPPORTED
    backend = "d3d12";
#else
    backend = "vk";
#endif
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--out") out = next();
        else if (a == "--width") width = (uint32_t)std::atoi(next().c_str());
        else if (a == "--height") height = (uint32_t)std::atoi(next().c_str());
        else if (a == "--hdri") opt.hdri = next();
        else if (a == "--backend") backend = next();
        else if (a == "--shaders") shaders = next();
        else if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--sun") opt.sun = true;
        else if (a == "--scene") opt.scene = next();
        else if (a == "--data") opt.dataDir = next();
        else if (a == "--ddgi") opt.ddgi = std::atoi(next().c_str()) != 0;
        else if (a == "--ddgi-view") opt.ddgiView = std::atoi(next().c_str());
        else if (a == "--probes") opt.ddgiProbes = std::atoi(next().c_str());
        else if (a == "--show-probes") opt.showProbes = std::atoi(next().c_str()) != 0;
        else if (a == "--baked") opt.bakedProbes = std::atoi(next().c_str()) != 0;
        else if (a == "--click" || a == "--drag" || a == "--key") events.push_back({a, next()});
    }

#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC decodes the textures (vp3d/AssetImport.cpp)
#endif
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    IEngineFactory* factory = nullptr;
#if D3D12_SUPPORTED
    if (backend == "d3d12") {
        IEngineFactoryD3D12* f = GetEngineFactoryD3D12();
        if (f && f->LoadD3D12()) {
            EngineD3D12CreateInfo ci;
            ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
            ci.DynamicHeapPageSize = 8 << 20;   // the DDGI probe captures map the draw constants thousands of times per frame
            if (std::getenv("WZX_VALIDATE")) ci.SetValidationLevel(VALIDATION_LEVEL_2);   // D3D12 debug layer + GPU validation
            f->CreateDeviceAndContextsD3D12(ci, &device, &context);
            factory = f;
        }
    }
#endif
#if VULKAN_SUPPORTED
    if (backend == "vk") {
        IEngineFactoryVk* f = GetEngineFactoryVk();
        EngineVkCreateInfo ci;
        ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
        ci.DynamicHeapSize = 32 << 20;   // the DDGI probe captures map the draw constants thousands of times per frame
        ci.DynamicHeapPageSize = 2 << 20;
        f->CreateDeviceAndContextsVk(ci, &device, &context);
        factory = f;
    }
#endif
    if (!device) {
        std::fprintf(stderr, "No %s device\n", backend.c_str());
        return 1;
    }
    std::printf("Device: %s (%s)\n", device->GetAdapterInfo().Description, backend.c_str());

    player::Player p;
    std::string err;
    if (!p.init(device, context, factory, TEX_FORMAT_RGBA8_UNORM, shaders, opt, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    TextureDesc td;
    td.Name = "Headless output";
    td.Type = RESOURCE_DIM_TEX_2D;
    td.Width = width;
    td.Height = height;
    td.Format = TEX_FORMAT_RGBA8_UNORM;
    td.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(td, nullptr, &target);
    td.Name = "Headless readback";
    td.BindFlags = BIND_NONE;
    td.Usage = USAGE_STAGING;
    td.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(td, nullptr, &staging);
    if (!target || !staging) {
        std::fprintf(stderr, "Cannot create the %ux%u image\n", width, height);
        return 1;
    }

    for (int f = 0; f < std::max(frames, 1); ++f) {
        scriptedInput(events, f);
        p.frame(target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), width, height);
        context->Flush();
        context->FinishFrame();
        context->WaitForIdle();   // no Present to pace the frames: running far ahead of the GPU crashes the driver
    }
    CopyTextureAttribs copy(target, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->CopyTexture(copy);
    context->WaitForIdle();

    MappedTextureSubresource m;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, m);
    std::vector<uint8_t> rgba((size_t)width * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(&rgba[(size_t)y * width * 4], (const uint8_t*)m.pData + (size_t)y * m.Stride, (size_t)width * 4);
    context->UnmapTextureSubresource(staging, 0, 0);

    if (!stbi_write_png(out.c_str(), (int)width, (int)height, 4, rgba.data(), (int)width * 4)) {
        std::fprintf(stderr, "Cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("Wrote %s (%ux%u, MSAA %ux)\n", out.c_str(), width, height, p.renderer().msaaSamples());
    if (const player::Game* g = p.game()) {
        std::printf("Game: %d visible objects\n", g->objectsShown());
        const vp3d::Camera& c = p.scene().camera;
        std::printf("Camera: target %.4f %.4f %.4f yaw %.4f pitch %.4f distance %.4f (degrees: yaw %.3f pitch %.3f)\n",
                    c.target.x, c.target.y, c.target.z, c.yaw, c.pitch, c.distance, vp3d::degrees(c.yaw), vp3d::degrees(c.pitch));
        for (const std::string& l : g->log()) std::printf("Graph: %s\n", l.c_str());
    }
    p.shutdown();
    return 0;
}
