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

Options : `--scene cornell|studio|lookdev|concept`, `--hdri fichier.hdr|sky`, `--open scene.v3d.json`, `--view N`, `--select N`, `--tool 1|2|3`,
`--quality 0|1`, `--raster 0|1`, `--aa 1|4|8|16`, `--shot fichier.png --spp N` (capture fenêtre puis quitte), `--shot-viewport fichier.png`,
`--cam persp|top|front|side|bottom|back|left|render`, `--layout 0..5` (disposition de la vue), `--demo-cameras` (deux caméras de démonstration),
`--open-menu vmCamera|atlLayout|...` (menu ouvert pour une capture).

## Autotest de l'éditeur

```
tools\selftest.bat            compile, lance l'autotest et affiche le rapport (build\selftest_report.txt)
tools\selftest.bat nobuild    sans recompiler ; "debug" en plus : couche de validation DirectX 12 (outils graphiques Windows)
```
L'éditeur s'ouvre et se pilote seul pendant une quinzaine de secondes (ne pas toucher la souris ni le clavier) : vrais
clics et raccourcis sur l'interface (outils QWER, boutons, menus de la vue, Grid Options, cube de vue, sélection, marking
menu, manipulateurs, Advanced Scale), images lues et mesurées (grille, wireframe, glow de sélection, MSAA x4 / x8 / x16,
overlay du path tracing), caméras (vues orthographiques, objets caméra, RenderCam, aperçu, dispositions 2 x 2 et 4 x 4,
Espace), Play du Village, changement de taille de fenêtre. Code : `src/app/SelfTest.cpp` et
`src/app/ManipulatorsTest.inl` (`--gizmo-test rapport.txt` : la partie logique seule).

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

## Caméras et vues

Comme dans Maya (`src/app/Cameras.cpp`, `src/app/Panes.cpp`) :

- **Caméras intégrées** : `persp` (la caméra de la scène) et les vues orthographiques `top`, `front`, `side`, `bottom`,
  `back`, `left` (menu caméra en haut à gauche de la vue). Une vue orthographique ne tourne pas : le clic droit y fait un
  pan, la molette change sa taille. Chaque caméra garde sa position quand on passe de l'une à l'autre.
- **Objets caméra** : Create › Camera from View / Render Camera from View, ou « New Camera from View » du menu caméra. Ils
  apparaissent comme des icônes de caméra dans la vue (cliquables), avec leurs réglages dans l'Inspector (focale,
  orthographique, taille de vue, format de l'aperçu, caméra de rendu, Look Through). Naviguer à travers une caméra la
  déplace. Une seule **caméra de rendu** (orange, « R ») ; elle est enregistrée dans le `.wzx` avec les autres.
- **Persp / RenderCam** : les deux boutons de la barre de la vue passent de la caméra persp à la caméra de rendu (créée
  depuis la vue si la scène n'en a pas).
- **Dispositions** : bouton à quatre cases de l'en-tête du viewport : une vue, deux côte à côte ou superposées, 2 x 2
  (top, persp / front, side), 3 x 3, 4 x 4. La vue active (dernier clic, cadre de couleur) a le rendu complet et les
  outils ; les autres sont des vues raster de leur caméra. **Espace** : la vue sous la souris seule, et retour.
- **Aperçu** : la caméra sélectionnée s'affiche en bas à droite de la vue active (comme Unreal), épinglable, avec un
  bouton Look Through. Pendant Play, la caméra RTS prend la vue persp et rend la vue d'origine à l'arrêt.
- **Espace** (comme Maya) : un appui bref passe d'une vue aux quatre vues, puis met la vue sous la souris seule et
  revient ; maintenu, menu des vues (Top, Side, Front, Persp) et liste des caméras de la scène ; un geste rapide
  choisit sans afficher le menu.

## Snapping, placement, pivot

`src/app/Snapping.cpp` :

- Bouton aimant : snapping toujours actif (sinon Maj pendant le glisser) ; **Maj+clic** : fenêtre Snapping Options.
- Trois icônes de la barre : snap absolu sur la grille aux **intersections**, aux **centres des cases** ou aux
  **milieux des arêtes** (les points sont dessinés autour de l'objet déplacé) ; re-cliquer revient aux pas relatifs.
- **V** maintenu pendant un déplacement : le pivot va sur le sommet le plus proche du curseur.
- **Place** (icône de la barre) : un objet sélectionné glissé au clic gauche suit les surfaces de la scène (le sol à
  défaut), posé dessus, tourné vers la surface si l'option est cochée.
- **Edit Pivot** : D maintenu (ou Insert jusqu'au prochain appui) ; le manipulateur de déplacement bouge le pivot, la
  géométrie reste. **Center Pivot** : Alt+Y (aussi dans le marking menu W, à gauche). Le pivot est enregistré dans
  le `.wzx`.

## Contrôles

- Clic droit (ou Alt+clic gauche) : orbite · Clic milieu / Maj+clic droit : pan · Molette : zoom
- Clic gauche : sélection · Maj+clic / rectangle : sélection multiple · Double-clic : pivot + mise au point
- Q / G / R : sélectionner / déplacer / tourner (gizmos flèches et anneaux) · Flèches, PgUp/PgDn : déplacer de 1 cm
- Ctrl+Z / Ctrl+Y / Ctrl+D / Suppr · F : recadrer · T : rotation auto · F12 : capture PNG · F1 / F2 : scènes
- P : path tracing / viewport · L : GI sondes temps réel (DDGI) · Espace : vue sous la souris seule / disposition

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
