# iOS player

The iOS player is built from `player/` (CMake), not from a hand-written Xcode project:
`player/ios/main_ios.mm` (UIKit, CAMetalLayer, touch) around the shared `player/src/Player.cpp` and the
Lit renderer `src/lit` (Diligent Engine, Vulkan translated to Metal by MoltenVK).

Build: GitHub Actions workflow `.github/workflows/ios-build.yml` (macOS runner, unsigned .ipa), then
Sideloadly on the PC to sign it with an Apple ID and install it by USB. The editor is not part of it.

Milestone 1: the player shows the built-in LookDev scene. Loading an exported game folder comes next.
