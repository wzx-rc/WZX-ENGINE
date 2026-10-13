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
- Ctrl+S / Ctrl+O : enregistrer / ouvrir une scène au format `.v3d.json` (`vp3d-scene` v1, listes `objects` et `village`).

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
  - `AssetImport` : import OBJ (positions, UV, normales) et images (WIC : jpg, png, bmp) ; textures de base (`Material::baseColorTex`) échantillonnées en raster et en traçage
  - `UI` : UI immédiate Direct2D/DirectWrite au-dessus du swapchain (D3D11On12), sans dépendance externe
  - `shaders/` : `PathTrace.hlsl`, `Denoise.hlsl`, `Tonemap.hlsl`, `Raster.hlsl`
- `src/app/` : éditeur (espaces de travail Atelier dessinés avec Dear ImGui)
  - `App` : fenêtre, boucle, édition (annuler, sélection, ajout…) · `ImGuiLayer` : backends Win32 + DX12, polices, textures · `Theme` : couleurs et réglages
  - `AtelierUI` : espace Scene et barre du haut · `atelier/` : Material Studio, Attribute Editor, palette partagée (`atelier_style.h`)
  - `game/` : éditeur nodal du jeu (onglet Graph : `NodeEditor`, bibliothèque `NodeLib`, exécution `Runtime`, graphe `Graph`) et bases RTS : grille `Grid`, caméra `RtsCamera`, maisons `Placement` (placées et animées dans `src/app/AppRts.cpp`, mode village dans la vue Scene, ou pendant Play ; le village est sauvegardé dans la scène, clé `village`). Les nœuds RTS (placer, supprimer, tourner une maison, « House Clicked ») exécutent la logique des maisons ; les boutons du HUD en Play sont déclarés par des nœuds « HUD Button ». Le nœud C++ « Caméra RTS » ne fait que la caméra RTS et le cadran solaire : il reste inactif tant qu'un lien ne l'active pas. Le nœud « Set Sun » règle le soleil et l'allume pendant le Play
  - `Editor` : fenêtres Settings, Controls, About et vignettes du Content Browser
  - `Viewport` : image 3D (texture du renderer), caméra, sélection, gizmo, glisser-déposer
  - `Content` : palette, matériaux, primitives, scènes · `SceneIO` : fichiers de scène
- `third_party/imgui/` : Dear ImGui v1.92.9b (MIT), compilé une fois dans `build/lib/imgui.lib`
- `third_party/atelier_reference/` : code de référence de l'UI Atelier
- `assets/` : modèles et thèmes copiés à côté de l'exécutable par `build.bat`
- `docs/` : mémoire du projet et rapports (DDGI, RTS)
- `tools/` : outils annexes (`curve_cut`)

Dossiers locaux non versionnés (voir `.gitignore`) : `build/`, `captures/`, `_backup/`,
copies de travail `_ddgi-test/`, `_nodal-editor/`, `_rts-test/`, et les dossiers d'échange
`00 - REFERENCES/` et `01 - INBOX/` (fichiers déposés pour Claude).

## Langue

- Interface en anglais par défaut. Settings › Appearance › Language (English uniquement pour le moment, le français viendra plus tard).

## Interface

- Trois espaces de travail plein écran dans le style du Graph Editor Atelier (même barre du haut, boutons Scene / Graph / Materials, panneaux à titre, échelle de référence 1600 × 900).
  - Scene : `src/app/AtelierUI.cpp`, branché sur le moteur.
  - Graph : éditeur nodal du jeu (`src/game`), dans le style Atelier : bibliothèque de nœuds à gauche, canevas avec groupes, Inspector à droite, panneau de sortie en bas (Output, Compiler Results, Execution Trace). Compile sauvegarde le graphe et le valide. L'ancien graphe Atelier est archivé dans `archive/atelier-graph/`.
  - Material Studio : module Atelier de `src/app/atelier`. Le panneau de droite du Material Studio est l'Attribute Editor (`inspector.cpp`, livrable Atelier-Design-AttributEditor).
  - Les fonctions pas encore développées (Build, Play…) restent visibles mais désactivées.
  - Espace au démarrage : `--workspace scene|graph|material`.
- Fenêtre **Settings** (icône engrenage de la barre du haut, ou Project › Settings…), enregistrée dans `build/bin/wzx_settings.ini` :
  - Mode : Dark / Light (le mode clair est volontairement un peu atténué).
  - Main Color : teinte des panneaux et des fonds (Graphite = noir neutre, Blue Slate = bleuté d'origine, Midnight, Forest, Plum, Umber ou couleur libre).
  - Accent Color : sélection, boutons actifs, surlignages.
  - Graph : Node outline (contours des nœuds, activable, dans la couleur du node à 50 %) et Outline weight (épaisseur, de 0,1 à 3 px), et Selection outline (couleur du contour du node sélectionné).
  - Text size, Icon size, Layout size (espacement : plus compact en dessous de 100 %, plus aéré au-dessus).
- Couleurs partagées par tous les espaces : `src/app/atelier/atelier_style.h`, recalculées à chaque image depuis ces réglages. Métriques et couleurs de base : `assets/themes/Blue 01.theme`.
- Tests en ligne de commande (sans enregistrer) : `--light 0|1`, `--main-color r,g,b`, `--sizes texte,icônes,layout`, `--settings`.
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
