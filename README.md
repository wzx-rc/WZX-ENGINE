# Viewport 3D (vp3d)

Viewport 3D interactif en C++ / DirectX 12 avec ray tracing matériel (DXR 1.1, inline RayQuery).
Path tracing progressif : GI, area lights (MIS), SSS random-walk, DOF thin-lens, bevel shader,
verre/plastique transparent, vernis, débruitage à-trous, tonemapping ACES.

## Compiler / lancer

```
build.bat            (Release)  ->  build\bin\Viewport3D.exe
build.bat debug
```
Prérequis : Visual Studio 2022 Build Tools (C++), Windows SDK 10.0.22621+, GPU compatible DXR 1.1.

`build.bat` compile tous les `.cpp` de `src\vp3d` et `src\app` et lie tous les `.obj` de `build\obj` :
après la suppression d'un fichier source, supprimer aussi son `.obj` dans `build\obj`.

Options : `--scene cornell|studio`, `--open scene.v3d.json`, `--view N`, `--select N`, `--tool 1|2`,
`--quality 0|1`, `--raster 0|1`, `--shot fichier.png --spp N` (capture fenêtre puis quitte), `--shot-viewport fichier.png`.

## Scènes

- **Cornell Box** (F1, par défaut) : scène de test du path tracing (shader balls, matériaux).
- **Studio** (F2) : sol quadrillé et lumières douces, sans objets.
- Ctrl+S / Ctrl+O : enregistrer / ouvrir une scène au format `.v3d.json` (`vp3d-scene` v1, liste `objects`).

## Modes de rendu

- **Viewport (raster)** par défaut : PBR direct, shadow map PCF, MSAA 4x, ~1 ms GPU.
- **Path tracing** (touche P ou bouton en haut du viewport) : le rendu progressif complet.

## Transformations de vue

ACES (fit de Hill, RRT+ODT), AgX, AgX Punchy, Standard. Espace de travail : linéaire Rec.709 / sRGB.

## Contrôles

- Clic droit (ou Alt+clic gauche) : orbite · Clic milieu / Maj+clic droit : pan · Molette : zoom
- Clic gauche : sélection · Maj+clic / rectangle : sélection multiple · Double-clic : pivot + mise au point
- Q / G / R : sélectionner / déplacer / tourner (gizmos flèches et anneaux) · Flèches, PgUp/PgDn : déplacer de 1 cm
- Ctrl+Z / Ctrl+Y / Ctrl+D / Suppr · F : recadrer · T : rotation auto · F12 : capture PNG · F1 / F2 : scènes

## Structure

- `src/vp3d/` : **module réutilisable** (lib statique `build/lib/vp3d.lib`)
  - `Renderer` : device DX12, BLAS/TLAS, accumulation, débruitage, tonemap
  - `Scene`, `MeshGen` : description de scène, meshes procéduraux (box, sphère, lathe, shader ball)
  - `UI` : UI immédiate Direct2D/DirectWrite au-dessus du swapchain (D3D11On12), sans dépendance externe
  - `shaders/` : `PathTrace.hlsl`, `Denoise.hlsl`, `Tonemap.hlsl`, `Raster.hlsl`
- `src/app/` : éditeur de démo
  - `Content` : palette, préréglages de matériaux, primitives, scènes Cornell Box et Studio
  - `App`, `AppUI`, `Panels`, `Gizmo`, `SceneIO` : boucle, interface, panneaux ancrables, gizmos, fichiers de scène
- `assets/` : modèles (copiés à côté de l'exe au build) · `00 - REFERENCES/` : concepts visuels
- `references/` : images de référence · `captures/` : rendus de test

## Intégrer le module dans une autre app

```cpp
vp3d::Renderer r;  r.init(hwnd, w, h, L"shaders");
vp3d::Scene s;     /* meshes, materials, objects, lights, camera */
r.setScene(s);
// chaque frame :
r.render(s, settings, viewportRect, selected);  /* dessiner l'overlay */  r.present(true);
```
