// Reader of the .wzx scene format shared by the editor (SceneWzx.cpp) and the player (player/src/Player.cpp):
// fills a vp3d::Scene from the parsed file, without anything of the editor. The format is described in SceneWzx.cpp.
#pragma once
#include "Content.h"
#include "Json.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace app {

extern const char* const kWzxMeshFile;   // mesh name prefix of an imported mesh: "file:<path relative to the project>"

struct WzxRead {
    std::map<int, int> placeholderMat;   // red material of a missing mesh placeholder -> material of the file
    std::vector<std::string> warnings;   // missing files, unknown primitives
    // in: the "curve" block of a curve object into the object (editor, CurveEditor.cpp); empty in the player: the
    // curve object is kept without geometry
    std::function<void(const Json&, Object&)> readCurve;
};

// Scene, environment (HDRI aside: the caller loads environment.hdri), sun, collections, lights, materials (and their
// textures), objects and camera. The "render", "village", "graph" and "probes" keys are left to the caller.
// absPath: a path written in the file -> the file on disk. fallbackName: scene name when the file has none.
void readWzxScene(const Json& j, Scene& sc, const std::function<std::wstring(const std::string&)>& absPath,
                  const std::string& fallbackName, WzxRead& out);

// A parsed value as compact JSON text (the graph of a scene for game::Graph::fromJson).
std::string jsonText(const Json& j);

// Every file the scene refers to, as written in the file: HDRI, imported meshes, textures, baked probes.
std::vector<std::string> wzxSceneFiles(const Json& j);

} // namespace app
