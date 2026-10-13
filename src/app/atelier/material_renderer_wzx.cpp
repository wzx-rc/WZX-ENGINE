// WZX implementation of the Atelier material preview API (material_renderer.h).
// The Atelier prototype renders the preview with its own Direct3D 11 renderer; in the WZX Engine the preview is
// rendered by the engine (vp3d, DirectX 12) through a host provided by the application.
#include "material_renderer.h"
#include "material_host.h"
#include "material_model.h"

namespace atelier::material {

static PreviewHost* g_host = nullptr;
static std::string g_status = "WZX vp3d renderer (DirectX 12 + DXR)";
static std::string g_shader =
    "// WZX Engine material preview\n"
    "// Rendered by the engine renderer (vp3d): DirectX 12, raster viewport or DXR path tracing.\n"
    "// Mapped parameters: base colour, roughness, metallic, IOR, clearcoat, emission, opacity (transmission).\n"
    "// Texture maps, patterns, anisotropy and sheen are kept in the material data but not rendered yet.\n";

void SetPreviewHost(PreviewHost* host) { g_host = host; }

bool InitializeRenderer(ID3D11Device*, ID3D11DeviceContext*) { return g_host != nullptr; }
void ShutdownRenderer() {}

PreviewTexture RenderPreview(const Material& material, const PreviewSettings& settings, int width, int height) {
    if (!g_host) return {};
    g_host->requestPreview(material, settings, width, height);
    PreviewTexture t;
    t.id = g_host->previewTexture();
    t.width = width;
    t.height = height;
    return t;
}

const std::string& GetRendererStatus() { return g_status; }
const std::string& GetShaderSource() { return g_shader; }

bool ExportPreviewPNG(const std::filesystem::path& filename) { return g_host && g_host->exportPreview(filename.wstring()); }

std::string PickTextureFile() { return g_host ? g_host->pickTextureFile() : std::string(); }

PreviewTexture GetTexturePreview(const std::string& filename) { return g_host ? g_host->loadTexture(filename) : PreviewTexture{}; }

} // namespace atelier::material
