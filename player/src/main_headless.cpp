// WZX Player, headless: renders one image of the scene with the Lit renderer into a PNG, without a window.
// Used to compare the targets with each other and with the editor (docs/RENDU_PARITE.md):
//   WZXPlayerHeadless --out lookdev.png [--width 1280 --height 720] [--hdri sky|none|file.hdr]
//                     [--backend d3d12|vk] [--shaders dir1;dir2] [--sun]
// Windows uses DirectX 12 by default, other systems Vulkan (on Linux, Mesa lavapipe renders without a GPU).
#include "Player.h"

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
#include <vector>

using namespace Diligent;

int main(int argc, char** argv) {
    std::string out = "lit.png", backend, shaders = "shaders";
    uint32_t width = 1280, height = 720;
    int frames = 3;
    player::Options opt;
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
    }

    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    IEngineFactory* factory = nullptr;
#if D3D12_SUPPORTED
    if (backend == "d3d12") {
        IEngineFactoryD3D12* f = GetEngineFactoryD3D12();
        if (f && f->LoadD3D12()) {
            EngineD3D12CreateInfo ci;
            ci.Features.ShaderResourceRuntimeArrays = DEVICE_FEATURE_STATE_OPTIONAL;
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
        p.frame(target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), width, height);
        context->Flush();
        context->FinishFrame();
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
    p.shutdown();
    return 0;
}
