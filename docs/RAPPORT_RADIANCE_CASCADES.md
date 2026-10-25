# Radiance Cascades : GI temps réel sans accumulation temporelle (25/10/2026)

Branche `claude/project-thread-0x1y2n`, worktree `_worktrees\radiance-cascades`, pas encore fusionnée.
GPU : NVIDIA GeForce RTX 5060, Release, viewport ≈ 1 200 × 900.

## Principe

Radiance Cascades en 3D, dans l'espace monde, avec le ray tracing matériel (DXR 1.1, RayQuery inline).

- **Cascade 0** = la grille de sondes DDGI (≈ 8 000 sondes par défaut), 8 × 8 directions octaédriques par sonde, rayons courts : intervalle [0, 1 espacement].
- **Cascade i** : sondes 2 fois plus espacées (8 fois moins nombreuses), 4 fois plus de directions, intervalle 4 fois plus long ([L₀(4ⁱ−1)/3, L₀(4ⁱ⁺¹−1)/3]). La dernière cascade trace jusqu'à l'infini et voit le ciel.
- **Fusion** de la cascade la plus haute vers la cascade 0 : ce qu'un rayon ne touche pas dans son intervalle est pris dans la cascade du dessus (moyenne des 4 directions plus fines, trilinéaire sur les 8 sondes parentes).
- **Visibilité des parents** (`RCVisibility.hlsl`) : une sonde ne fusionne que les parents qu'elle voit (1 rayon d'ombre par parent). Sans ce test, les parents situés derrière un mur apportent la lumière, ou le noir, de l'autre côté. Résultat avant le correctif : fuites le long des murs et GI 30 à 40 % trop sombre.
- **Intégration** (`RCGather.hlsl`) : la cascade 0 remplit les atlas des sondes DDGI (irradiance 8 × 8, distances 16 × 16, état des sondes). Tout ce qui lit les sondes marche donc sans changement : passe raster, ombrage ray tracé `GIShade`, affichage des sondes, bake du mode Lit.
- **Aucune accumulation temporelle** par défaut : tout est recalculé à chaque image, et les échantillons de lumière sont fixes, donc l'image ne scintille pas. Une lumière qui bouge est suivie immédiatement, alors que la DDGI traîne derrière son hystérésis. Option « Hystérésis » > 0 pour comparer.
- **Rebonds multiples** : les impacts lisent les sondes de l'image précédente, comme en DDGI (option).

Fichiers : `src\vp3d\shaders\RCCommon.hlsli`, `RCVisibility.hlsl`, `RCTrace.hlsl`, `RCGather.hlsl` ; `Renderer::recordRC` dans `src\vp3d\Renderer.cpp` ; réglages `RenderSettings::giMethod` (1 = Radiance Cascades) et `rc*` dans `Renderer.h`.

## Viewer solo : `RCViewer.exe`

`build_rcviewer.bat` compile seulement le viewer (shaders, vp3d, ImGui). `build.bat` compile l'éditeur **et** le viewer. Sortie : `build\bin\RCViewer.exe`.

- **Scènes** : Cornell Box (F1), LookDev HDRI (F2), Atrium (F3). L'Atrium est un hall fermé façon Sponza, éclairé par le soleil à travers une fente du toit, donc presque tout y est de la lumière rebondie.
- **Modes** (touches 1 à 6) :
  1. Radiance Cascades + raster, sans temporel ;
  2. Radiance Cascades + ombrage ray tracé ;
  3. DDGI + raster ;
  4. DDGI + ray tracé ;
  5. path tracing de référence ;
  6. raster sans GI.
- **Inspection** : complet / indirect seul / direct seul, affichage des sondes, fil de fer, exposition, tone mapping.
- **Réglages** : nombre de sondes, de cascades et de directions de la cascade 0, longueur d'intervalle, échantillons de lumière, rebonds, hystérésis.
- **Statistiques** : ms GPU (tracé des cascades, intégration), rayons par image, tableau des cascades.
- **Animer la lumière** (touche A) : la lumière de la Cornell glisse, l'HDRI tourne, le soleil de l'Atrium tourne.
- **Ligne de commande** : `--scene cornell|lookdev|atrium --mode 0..5 --view 0|1|2 --probes N --cascades N --dirres N --interval x --hysteresis x --show-probes 1 --camview N --anim 1 --size WxH --shot f.png [--frames N] --shot-window f.png`. Le fichier `f.png.txt` reçoit les temps GPU de la dernière image.

## Mesures (Cornell, 10 693 sondes, 4 cascades)

| Directions C0 | Rayons / image | Cascades (tracé + intégration) | GPU image entière |
|---|---|---|---|
| 4 × 4 | 0,39 M | 0,23 + 0,25 ms | 1,4 ms |
| **8 × 8 (défaut)** | 1,56 M | 0,68 + 0,80 ms | **2,9 ms** |

Atrium (10 530 sondes) : 1,66 M rayons, 1,0 + 0,9 ms, 3,3 à 3,8 ms par image.

Visuellement, l'éclairage indirect est proche de la DDGI convergée et du path tracing. Avec l'ombrage ray tracé (mode 2), la Cornell est quasiment identique à la référence.

## Limites connues

- Les 4 × 4 directions de la cascade 0 laissent un léger damier sur les murs, d'où le défaut à 8 × 8.
- Une petite tache sombre peut apparaître là où une sonde tombe presque sur une surface (Cornell avec 8 192 sondes : mur du fond, en haut à droite). Elle disparaît en changeant la densité de sondes. Piste : déplacer les sondes hors des surfaces (relocalisation, comme RTXGI).
- Pas encore de « bilinear fix » complet (un rayon par parent et par direction). Le test de visibilité par sonde en est une version bon marché.
- Les reflets brillants restent ceux de la passe raster (environnement) : en intérieur, le sol prend un reflet bleuté du ciel. C'est le même défaut en DDGI raster ; il faudrait une occlusion spéculaire.
- Pas encore branché dans l'éditeur (`Viewport3D.exe`) : seul `RenderSettings::giMethod` est à exposer dans le menu Rendu.
