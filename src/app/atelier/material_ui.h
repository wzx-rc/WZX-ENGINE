#pragma once
#include <string>
#include "material_model.h"

namespace atelier::material {
void Initialize(const std::string& libraryFile);
// 0: stay in Materials, 1: Scene requested, 2: Graph requested.
int Draw();
void Shutdown();
const MaterialLibrary& GetLibrary();
const Material& GetAppliedMaterial();
const PreviewSettings& GetPreviewSettings();
using ApplyCallback = void(*)(const Material&, void* user);
void SetApplyCallback(ApplyCallback callback, void* user = nullptr);
}
