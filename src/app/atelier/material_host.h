// Bridge between the Atelier Material Studio and the WZX application (preview rendering, files, textures).
#pragma once
#include "material_renderer.h"
#include <string>

namespace atelier::material {

struct PreviewHost {
    virtual ~PreviewHost() = default;
    virtual void requestPreview(const Material& material, const PreviewSettings& settings, int width, int height) = 0;
    virtual void* previewTexture() = 0;                        // ImTextureID of the last preview image
    virtual bool exportPreview(const std::wstring& path) = 0;
    virtual PreviewTexture loadTexture(const std::string& utf8Path) = 0;
    virtual std::string pickTextureFile() = 0;
};

void SetPreviewHost(PreviewHost* host);

} // namespace atelier::material
