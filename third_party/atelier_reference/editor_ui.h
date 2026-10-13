#pragma once
#include <array>
#include <string>
#include <vector>

namespace atelier {
struct Texture { void* id = nullptr; int width = 0; int height = 0; };
struct ActorState {
    int id=0,parent=-1,meshIndex=0,mobility=0,lodGroup=0;
    std::string name;
    bool folder=false,visible=true,locked=false,castShadows=true,receiveShadows=true,visibleInGame=true;
    bool collision=true,simulatePhysics=false,gravity=true,tick=true;
    std::array<float,3> position{},rotation{},scale{1,1,1};
    std::array<int,2> materials{};
    float cullDistance=10000,mass=20,friction=.5f;
    std::vector<std::string> tags,components;
};
struct SceneState {
    std::string name;
    int selectedActor=-1;
    float exposure=0,timeOfDay=17.4f,sunIntensity=3.5f,windStrength=.3f;
    bool ambient=true,fog=true,playing=false;
    std::vector<ActorState> actors;
};
void Initialize(const Texture& reference, const Texture& viewport = {});
void Draw();
void Shutdown();
// Read after Draw() to connect the widgets to your renderer/scene.
SceneState GetState();
// Use when your viewport picks an actor. Returns false for an unknown ID.
bool SetSelection(int actorId);
// Configure a writable project file before Initialize().
void SetSceneFile(const std::string& path);
}
