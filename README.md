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

Options : `--scene cornell|studio|lookdev`, `--hdri fichier.hdr|sky`, `--open scene.v3d.json`, `--view N`, `--select N`, `--tool 1|2`,
`--quality 0|1`, `--raster 0|1`, `--shot fichier.png --spp N` (capture fenêtre puis quitte), `--shot-viewport fichier.png`.

## Scènes

- **Cornell Box** (F1, par défaut) : scène de test du path tracing (shader balls, matériaux).
- **Studio** (F2) : sol quadrillé et lumières douces, sans objets.
- **LookDev** (F3) : sol gris neutre et cinq sphères de référence (argile grise, chrome miroir, verre, émissive qui éclaire vraiment, cire de bougie en SSS), éclairées par un HDRI.
- Ctrl+S / Ctrl+O : enregistrer / ouvrir une scène au format `.v3d.json` (`vp3d-scene` v1, listes `objects` et `village`).

## HDRI

Onglet **Render** de l'Inspector, section « HDRI environment » (commune aux quatre modes) : choix du fichier, intensité, rotation, visible ou non par la caméra.
Formats : `.hdr` (RGBE), `.exr` (scanline, sans compression, RLE, ZIP, ZIPS ou PIZ, half ou float), et jpg/png convertis en linéaire (sans vraie dynamique). Au-delà de 4096 de large, l'image est réduite.
La liste propose les `.hdr`/`.exr` de `01 - INBOX` puis les images de `assets/hdri` (HDRI de test Poly Haven, CC0, hors du suivi git) ; « Load… » ouvre n'importe quel fichier. Le chemin est enregistré dans la scène.

- Path tracing (Progressive) : échantillonnage d'importance de l'image (MIS), le soleil de l'HDRI donne des ombres nettes ; les objets émissifs sont aussi échantillonnés comme des lumières ; les ombres traversent le verre (teinte, bord sombre).
- Progressive : « Filter glossy » et « Indirect clamp » (à 0 par défaut, donc exact) enlèvent les étincelles (caustiques) au prix d'un léger biais.
- Real time : ambiance diffuse en harmoniques sphériques, reflets dans les mips flous de l'image, le soleil de l'HDRI devient la lumière principale avec la shadow map.
- GI probes : l'HDRI éclaire les sondes, reflets et verre approximatifs.

## Modes de rendu

Trois modes, choisis dans le menu de rendu en haut du viewport ; leurs réglages sont dans l'onglet **Render** de l'Inspector (icône i sur chaque option).

- **Real time (raster)** par défaut : PBR direct, shadow map PCF, MSAA 4x, ~1 ms GPU.
- **Progressive path tracing** (touche P depuis le raster) : rendu progressif exact, avec débruiteur optionnel (option « Denoiser » de l'onglet Render, désactivée par défaut).
- **GI probes (DDGI)** (touche L) : grille de sondes d'éclairage indirect mise à jour à chaque image, avec historique d'image. Expérimental ; rapport : `docs/RAPPORT_DDGI.md`. Options : `--ddgi 1`, `--probes N`, `--ddgi-view 0|1|2`, `--hysteresis x`.

## Transformations de vue

ACES (fit de Hill, RRT+ODT), AgX, AgX Punchy, Standard. Espace de travail : linéaire Rec.709 / sRGB.

## Contrôles

- Clic droit (ou Alt+clic gauche) : orbite · Clic milieu / Maj+clic droit : pan · Molette : zoom
- Clic gauche : sélection · Maj+clic / rectangle : sélection multiple · Double-clic : pivot + mise au point
- Q / G / R : sélectionner / déplacer / tourner (gizmos flèches et anneaux) · Flèches, PgUp/PgDn : déplacer de 1 cm
- Ctrl+Z / Ctrl+Y / Ctrl+D / Suppr · F : recadrer · T : rotation auto · F12 : capture PNG · F1 / F2 : scènes
- P : path tracing / viewport · L : GI sondes temps réel (DDGI)

## UI Designer (F8)

**F8** ouvre le UI Designer, un constructeur d'interfaces à la Qt Designer, dessiné en Dear ImGui dans l'éditeur.

- **Palette** (à gauche) : Window, Panel, Row, Column, Button, Label, Checkbox, Slider, Input, Combo, Color, Progress, Separator, Spacer. Un clic ajoute le widget dans la sélection, un glisser le pose sur le canevas.
- **Canevas** : clic pour sélectionner, glisser pour déplacer. Lâcher un élément sur un autre conteneur le déplace dedans, et dans une Row ou une Column, la position d'insertion s'affiche. Le coin vert redimensionne, le clic molette fait défiler et Ctrl+molette zoome. Raccourcis : flèches, Suppr, Ctrl+D, Ctrl+Z / Ctrl+Y. « Aperçu » rend les widgets utilisables.
- **Arborescence** : glisser pour changer de parent, clic droit pour dupliquer, supprimer, monter ou descendre, ou mettre dans une Row ou une Column.
- **Propriétés** (à droite) : id, texte, liaison, info-bulle, position, taille, espacement, couleurs, visibilité.

Les interfaces sont des fichiers `ui/*.wzui.json`. Ils sont enregistrés automatiquement après chaque modification et rechargés quand un autre outil les modifie, par exemple Claude ou git. Le format est décrit dans `src/app/designer/UiDoc.h`.
Un document coché « Dans l'éditeur » s'affiche dans WZX en vraies fenêtres. Ses widgets sont reliés au code par nom avec `wzui::designer().bindings` : par exemple, `actions["play"]` est appelé par un bouton dont la liaison est `play`. `play`, `stop`, `build` et `graph` sont déjà reliés dans `GameHost::init`. Une valeur sans code garde sa propre valeur, ce qui permet de construire l'interface avant d'écrire la logique.
API : `POST /cmd {"cmd": "designer", "show"?: true, "file"?: "nom.wzui.json"}` renvoie le fichier ouvert et l'élément sélectionné. Claude sait ainsi quel élément est désigné.
Test sans GPU : `tools/ui_designer_test/designer_test.cpp` (Linux), qui produit aussi des captures.

## Structure

- `src/vp3d/` : **module réutilisable** (lib statique `build/lib/vp3d.lib`)
  - `Renderer` : device DX12, BLAS/TLAS, accumulation, débruitage, tonemap
  - `Scene`, `MeshGen` : description de scène, meshes procéduraux (box, sphère, lathe, shader ball)
  - `AssetImport` : import OBJ (positions, UV, normales) et images (WIC : jpg, png, bmp) ; textures de base (`Material::baseColorTex`) échantillonnées en raster et en traçage
  - `EnvImage` : lecture et conversion des HDRI (.hdr, .exr avec décodeurs ZIP et PIZ intégrés, jpg/png), ciel procédural de test
  - `shaders/` : `PathTrace.hlsl`, `Denoise.hlsl`, `Tonemap.hlsl`, `Raster.hlsl`, GI sondes (`DDGIUpdate.hlsl`, `DDGIBlend.hlsl`, `GIShade.hlsl`, `DDGICommon.hlsli`, `Temporal.hlsli`)
- `src/app/` : éditeur (espaces de travail Atelier dessinés avec Dear ImGui)
  - `App` : fenêtre, boucle, édition (annuler, sélection, ajout…) · `ImGuiLayer` : backends Win32 + DX12, polices, textures · `Theme` : couleurs et réglages
  - `AtelierUI` : espace Scene et barre du haut · `atelier/` : Material Studio, Attribute Editor, palette partagée (`atelier_style.h`)
  - `game/` : éditeur nodal du jeu (onglet Graph : `NodeEditor`, bibliothèque `NodeLib`, exécution `Runtime`, graphe `Graph`) et bases RTS : grille `Grid`, caméra `RtsCamera`, maisons `Placement` (placées et animées dans `src/app/AppRts.cpp`, mode village dans la vue Scene, ou pendant Play ; le village est sauvegardé dans la scène, clé `village`). Les nœuds RTS (placer, supprimer, tourner une maison, « House Clicked ») exécutent la logique des maisons ; les boutons du HUD en Play sont déclarés par des nœuds « HUD Button ». Le nœud C++ « Caméra RTS » ne fait que la caméra RTS et le cadran solaire : il reste inactif tant qu'un lien ne l'active pas. Le nœud « Set Sun » règle le soleil et l'allume pendant le Play
  - `designer/` : UI Designer (F8) : `UiDoc` (format `.wzui.json`), `UiRuntime` (dessin ImGui et liaisons), `Designer` (éditeur)
  - `Editor` : fenêtres Settings, Controls, About et vignettes du Content Browser
  - `Viewport` : image 3D (texture du renderer), caméra, sélection, gizmo, glisser-déposer
  - `Content` : palette, matériaux, primitives, scènes · `SceneIO` : fichiers de scène
- `third_party/imgui/` : Dear ImGui v1.92.9b (MIT), compilé une fois dans `build/lib/imgui.lib`
- `third_party/atelier_reference/` : code de référence de l'UI Atelier
- `assets/` : modèles et thèmes copiés à côté de l'exécutable par `build.bat`
- `docs/` : mémoire du projet et rapports (DDGI, RTS)
- `archive/` : code retiré gardé pour référence (ancien graphe Atelier)
- `tools/` : outils annexes (`curve_cut`)

Dossiers locaux non versionnés (voir `.gitignore`) : `build/`, `builds/`, `captures/`,
`_archive/` (anciens prototypes `ddgi-test`, `nodal-editor`, `rts-test` et `backup`, intégrés à `main` ;
l'historique du prototype DDGI est dans la branche `archive/ddgi-study`), et les dossiers d'échange
`00 - REFERENCES/` et `01 - INBOX/` (fichiers déposés pour Claude).

## Langue

- Interface en anglais par défaut. Settings › Appearance › Language (English uniquement pour le moment, le français viendra plus tard).

## Interface

- Trois espaces de travail plein écran dans le style du Graph Editor Atelier (même barre du haut, boutons Scene / Graph / Materials, panneaux à titre, échelle de référence 1600 × 900).
  - Scene : `src/app/AtelierUI.cpp`, branché sur le moteur.
  - Graph : éditeur nodal du jeu (`src/game`), dans le style Atelier : bibliothèque de nœuds à gauche, canevas avec groupes, Inspector à droite, panneau de sortie en bas (Output, Compiler Results, Execution Trace). Compile sauvegarde le graphe et le valide. L'ancien graphe Atelier est archivé dans `archive/atelier-graph/`.
  - Material Studio : module Atelier de `src/app/atelier`. Le panneau de droite du Material Studio est l'Attribute Editor (`inspector.cpp`, livrable Atelier-Design-AttributEditor).
  - **Play** (F5 ou bouton Play en haut) lance le jeu décrit par le graphe dans la vue ; **Build** (F7, bouton Build en haut ou Project › Build…) ouvre la fenêtre Build : plateforme (Windows ; iOS iPhone + iPad affiché « coming soon »), profil (`game/build_profiles.json`) et ses options (nom du jeu, fenêtre ou plein écran, path tracing, FPS, API de commandes, .zip). Le jeu sort dans `builds/<profil>/<Nom du jeu>/` et, avec l’option zip, dans `builds/<profil>/<Nom du jeu>.zip` à envoyer : on dézippe et on lance le .exe, rien à installer (runtime C++ lié en statique, `/MT`) ; il faut seulement un GPU DirectX 12 avec ray tracing. F3 ouvre le graphe.
  - API de commandes locale pour piloter l'éditeur : `http://127.0.0.1:7878`.
  - Espace au démarrage : `--workspace scene|graph|material`.
- Fenêtre **Settings** (icône engrenage de la barre du haut, ou Project › Settings…), enregistrée dans `build/bin/wzx_settings.ini` :
  - Mode : Dark / Light (le mode clair est volontairement un peu atténué).
  - Main Color : teinte des panneaux et des fonds (Graphite = noir neutre, Blue Slate = bleuté d'origine, Midnight, Forest, Plum, Umber ou couleur libre).
  - Accent Color : sélection, boutons actifs, surlignages.
  - Graph : Node outline (contours des nœuds, activable, dans la couleur du node à 50 %) et Outline weight (épaisseur, de 0,1 à 3 px), et Selection outline (couleur du contour du node sélectionné).
  - Text size, Icon size, Layout size (espacement : plus compact en dessous de 100 %, plus aéré au-dessus).
- Couleurs partagées par tous les espaces : `src/app/atelier/atelier_style.h`, recalculées à chaque image depuis ces réglages. Métriques et couleurs de base : `assets/themes/Blue 01.theme`.
- Tests en ligne de commande (sans enregistrer) : `--light 0|1`, `--main-color r,g,b`, `--sizes texte,icônes,layout`, `--settings`.
- `assets/` : modèles (copiés à côté de l'exe au build) · `00 - REFERENCES/` : concepts visuels · `captures/` : rendus de test

## Intégrer le module dans une autre app

```cpp
vp3d::Renderer r;  r.init(hwnd, w, h, L"shaders");
vp3d::Scene s;     /* meshes, materials, objects, lights, camera */
r.setScene(s);
// chaque frame :
r.render(s, settings, viewportRect, selected);  /* dessiner l'overlay */  r.present(true);
```
