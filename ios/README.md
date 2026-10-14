# iOS player

The iOS player is built from `player/` (CMake), not from a hand-written Xcode project:
`player/ios/main_ios.mm` (UIKit, CAMetalLayer, touch) around the shared game (`player/src/Player.cpp`, `Game.cpp`,
`ImGuiDiligent.cpp`) and the Lit renderer `src/lit` (Diligent Engine, Vulkan translated to Metal by MoltenVK).

Build: GitHub Actions workflow `.github/workflows/ios-build.yml` (macOS runner, unsigned .ipa; started by hand or by a
push on the branch of the player), then Sideloadly on the PC to sign it with an Apple ID and install it by USB.
The editor is not part of it.

Test build: the game of the editor in Play, Studio scene (no rim light), graph `game/main.graph.json`, houses of
`assets/HOUSE`, HUD and sun dial in Dear ImGui. Touch: one finger = the mouse (tap = click, drag = move a house,
HUD buttons, touch buttons for R / C / Delete), two fingers = camera (drag, pinch, twist).
The same game runs on Windows: `WZXPlayer.exe` (DirectX 12) and `WZXPlayerHeadless.exe --scene studio` (tests).
