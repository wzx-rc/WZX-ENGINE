#pragma once
#include <filesystem>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace atelier::material {
struct Material;
struct PreviewSettings;
struct PreviewTexture { void* id = nullptr; int width = 0; int height = 0; };

// The host owns device/context. Call before material UI initialization and shut
// down before releasing Direct3D. All preview work runs on the calling thread.
bool InitializeRenderer(ID3D11Device* device, ID3D11DeviceContext* context);
void ShutdownRenderer();
PreviewTexture RenderPreview(const Material& material, const PreviewSettings& settings,
                             int width, int height);
const std::string& GetRendererStatus();
const std::string& GetShaderSource();
bool ExportPreviewPNG(const std::filesystem::path& filename);
std::string PickTextureFile();
PreviewTexture GetTexturePreview(const std::string& filename);
}
