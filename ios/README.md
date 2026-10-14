# iOS player (placeholder)

This folder will hold the Xcode project of the iOS player: a Metal renderer and touch input
around the same game code as the Windows build. The Windows editor is not part of it.

Until the project exists, `.github/workflows/ios-build.yml` stops with an explicit error.

Needed later:
- `WZXPlayer.xcodeproj` with the scheme `WZXPlayer`, targeting iPhone and iPad (one build).
- `WZXPlayer/Game/` filled by the workflow from an exported game folder (not committed).
