# Starter RTS jouable dans WZX Engine (13/10/2026)

> État au 14/10/2026 : ce starter a été construit dans une copie séparée (`_rts-test`, aujourd'hui archivée localement dans `_archive/rts-test`). Il est intégré à `main` (tag `v0.1`) sous une autre forme : plus de `RTSGame.exe` ni de `--build-rts`, la logique RTS passe par les nœuds du graphe, Play (F5) et Build (F7) de l'éditeur (voir le README). Les sections ci-dessous décrivent la copie d'origine.

Copie séparée d'origine : `WZX ENGINE\_rts-test`. Les assets de la maison ont été lus depuis `_rts-test\assets\HOUSE` (copie identique de `00 - REFERENCES\RTS GAME - Starter`).
GPU de test : NVIDIA GeForce RTX 5060, Release.

## Lancer et compiler

- **Compiler** : `build.bat` (Release) dans `_rts-test`. Produit `build\bin\Viewport3D.exe` (éditeur) et `build\bin\RTSGame.exe` (le jeu).
- **Play** : bouton ▶ au centre de la barre du haut de l'éditeur (dispositions Default et Atelier), ou lancer directement `build\bin\RTSGame.exe`.
- **Build** : menu *Build › Build RTS game for PC (DX12)* de l'éditeur. Copie `RTSGame.exe`, `shaders\` et `assets\HOUSE\` dans `build\dist\RTSGame\` (15 Mo), un dossier qui se lance seul. Même chose en ligne de commande : `Viewport3D.exe --build-rts`.
- **Tests scriptés** : `powershell -ExecutionPolicy Bypass -File tools\test_rts.ps1` (captures et journaux dans `captures\rts_*.png|log`).

## Contrôles du jeu

ZQSD / WASD / flèches (touches lues par position physique, Maj = rapide), bords de l'écran, molette (zoom vers le curseur), A / E (Q / E en QWERTY) ou clic molette pour tourner, clic droit maintenu pour glisser la vue. Clic = sélection, glisser = déplacer avec calage sur la grille, R = tourner d'un quart de tour, Suppr = supprimer, Échap = désélectionner, F1 = aide, P = path tracing.

## Ce qui a été fait (code C++)

### Moteur (`src/vp3d`)
- **Import OBJ + image** (`AssetImport.h/.cpp`, nouveau) : `loadObj` lit `v / vt / vn / f` (polygones en éventail, indices négatifs), dédoublonne les sommets par (position, UV, normale) et, sans normales dans le fichier (cas de l'export ZBrush), les calcule avec un angle de lissage de 60°. `loadImage` décode jpg / png / bmp avec WIC et peut réduire l'image (2 048 px pour la maison). Le `.mtl` pointe vers un `House01_low.bmp` absent : le jeu détecte l'absence et prend `*basecolor*.jpg` du dossier.
- **UV et textures** : `Vertex` et `GpuVertex` ont un `uv` ; `Scene::textures` (RGBA8 sRGB) ; `Material::baseColorTex`. `Renderer::uploadTextures` crée la chaîne de mips sur le CPU et place jusqu'à 16 textures dans une table de descripteurs partagée par le raster et le path tracing (vues nulles pour les cases vides). `Raster.hlsl` et `PathTrace.hlsl` multiplient la couleur de base par la texture.
- **Soleil** : `Scene::sun` (direction, couleur, éclairement). En raster, c'est la lumière principale qui reçoit la shadow map (zone d'ombre fixée par `Scene::shadowCenter/shadowRadius`, 17 m autour du centre de la grille). En path tracing, il devient une petite lumière surfacique lointaine de même éclairement.
- **Grille de jeu** : nouveau drapeau `MAT_FLAG_TILES` (traits fins tous les mètres, plus marqués toutes les 5 cases, limités à `Scene::gridExtent`), en raster et en path tracing.
- **Contours et teinte** : `Object::outline` (1 cyan survol, 2 vert sélection, 3 rouge invalide) et `Object::tint`. La passe d'identifiants du raster écrit le code de contour ; `Tonemap.hlsl` trace un trait net (≈ 3 px à 1080p) puis un halo jusqu'à 17 px, masqué par les objets placés devant. Le contour jaune de l'éditeur est inchangé.

### Jeu (`src/game`, nouveau)
- `Grid` : cases de 1 m, 20 × 20, propriétaire par case ; `footprintCells` (`ceil(taille − 0,2)` comme le starter), `snapOrigin`, `canPlace`, `place`, `remove`, `findFreeNear` (anneaux).
- `RtsCamera` : cible au sol, tangage 55°, distance 1,5 à 30 m, bornes ±50 m, lissage exponentiel ; déplacement clavier et bords, zoom vers le point sous la souris, glisser au clic droit (le point attrapé reste sous le curseur), rotation.
- `RtsGame` : charge la maison (deux tailles comme le starter : petite 0,81 × 0,90 m = 1 case, grande 2,52 × 2,78 m = 3 × 3 cases), construit la scène (sol, ciel, soleil, 3 maisons), gère survol / sélection / glisser / dépose / retour à la place d'origine / ajout / rotation / suppression, et anime les maisons à pas fixe (1/120 s) avec les réglages du `Clickable` Unity : soulèvement 0,35 m avec flottement, inclinaison dans le sens du mouvement (max 14°), chute avec rebonds (restitution 0,45) et écrasement, grossissement ×1,04 au survol. Interface Dear ImGui : bouton « + Ajouter une maison » (clic = la maison tombe sur la case libre la plus proche du centre de l'écran ; glisser = la maison est en main, lâcher sur une case invalide ou sur l'interface l'annule), cadran du soleil relatif à la caméra (flèche = direction des ombres) avec un curseur de hauteur, aide et compteurs.
- `GameMain.cpp` : fenêtre, renderer, boucle, clavier par codes physiques (`MapVirtualKey`), et un mode test (`--test NOM --shot fichier.png --frames N --log fichier.txt`) qui pilote la souris et les touches par script.

### Éditeur (`src/app`)
- `App::launchRtsGame` (bouton Play) et `App::buildRtsGame` (menu Build), option `--build-rts`. Les anciens boutons Play « prochaine étape » sont branchés.
- `build.bat` : étape 5 qui compile `src\game` et lie `RTSGame.exe` (réutilise `ImGuiLayer.obj` et `vp3d.lib`).

## Résultats des tests

Compilation Release : OK (un seul avertissement, C4244 dans `RendererRaster.cpp`, déjà présent avant ces changements). Chargement : OBJ 78 700 positions / 157 504 triangles en ≈ 100 ms, texture 2 048² en ≈ 40 ms, scène prête en ≈ 250 ms.

| Scénario (souris et touches simulées) | État vérifié dans le journal | Capture |
|---|---|---|
| overview | 3 maisons, 11 cases occupées | maison texturée, ombres, grille, HUD |
| hover | `hover=1` | contour cyan |
| select | `selected=1` | contour vert |
| drag-valid | `drag=1 valid=1` | maison soulevée, inclinée, contour vert |
| drag-invalid (grande maison sur une petite) | `valid=0` | contour et teinte rouges |
| drop | case libre, 11 cases occupées | maison posée à la nouvelle place |
| spawn (clic sur le bouton) | 4 maisons, 12 cases | maison qui tombe |
| spawn-drag (glisser depuis le bouton) | `drag=3 valid=1` | nouvelle maison en main |
| rotate (R) | grande maison tournée de 90° | contour vert |
| sun (glisser sur le cadran) | azimut 220° → −26° | ombres et cadran mis à jour |
| camera (avancer, tourner, molette) | lacet 20° → 104°, distance 12 → 6,3 m | vue déplacée, bord de grille visible |
| path tracing (`--pt`, 300 images) | – | même texture et soleil qu'en raster |

Aussi vérifié : l'éditeur se lance toujours en raster et en path tracing (Cornell Box), `--build-rts` crée `build\dist\RTSGame` et l'exe copié se lance seul depuis ce dossier.

## Étapes du plan

Faites : 1 (import), 2 (textures raster + PT), 3 (sol et grille), 4 (module Grid), 5 (caméra RTS), 6 (sélection, glisser, rouge, dépose animée), 7 (bouton d'ajout), 8 (contour), 9 (soleil et cadran), et une première version de 11 (Play et Build).
Restent : 10 (comparaison côte à côte avec le prototype Unity, non faite), et la suite de 11 : mode Play dans l'éditeur (aujourd'hui un exe séparé qui ne reçoit pas la scène de l'éditeur), profils de build avec options par cible, éditeur adapté au RTS (placer les bâtiments, régler la grille, sauvegarder le village).

## Limites

- **Non vérifié à la main** : tout a été testé par entrées simulées (souris injectée dans Dear ImGui, touches injectées dans le jeu). Le ressenti (vitesses de caméra, défilement par les bords, molette), le clavier AZERTY réel et la souris réelle n'ont pas été essayés par une personne.
- Le survol et le clic testent la boîte englobante de la maison, pas ses triangles : on peut attraper la maison un peu au-dessus de son toit.
- Le renderer exige toujours un GPU compatible DXR 1.1, même pour le jeu en raster : le build ne démarre pas sur une machine sans ray tracing matériel.
- Ombres limitées à 17 m autour du centre de la grille (la caméra peut aller à ±50 m). Contours seulement en raster (pas en mode P).
- Une maison en main peut traverser les autres (comme dans le starter). Pas d'annulation, pas de sauvegarde du village, pas de tactile.
- La scène du jeu est construite dans le code (`RtsGame::buildScene`), pas encore depuis l'éditeur.
- Chaque maison réutilise le maillage complet (157 k triangles, sans LOD) : à surveiller avec beaucoup de maisons.
