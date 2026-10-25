# Radiance Cascades : GI temps réel sans accumulation temporelle (25/10/2026)

Branche `claude/project-thread-0x1y2n`, worktree `_worktrees\radiance-cascades`, pas encore fusionnée.
GPU : NVIDIA GeForce RTX 5060, Release, viewport ≈ 1 200 × 900.

## Principe

Radiance Cascades en 3D, dans l'espace monde, avec le ray tracing matériel (DXR 1.1, RayQuery inline).

- **Cascade 0** = la grille de sondes DDGI (≈ 8 000 sondes par défaut), 8 × 8 directions octaédriques par sonde, intervalle [0, 3 espacements] (voir « Correctif Atrium »).
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

## Correctif Atrium (25/10, soir)

L'Atrium était trop sombre sur le côté droit, qui ne reçoit que la lumière renvoyée par la bande de sol ensoleillé. Mesure de la luminance moyenne par zone, en % du path tracing (1 500 images) :

| Zone | Avant (raster) | Après (raster) | Après (ray tracé) | DDGI raster |
|---|---|---|---|---|
| Bannière rouge G | 90 | 88 | 95 | 90 |
| Bannière bleue G | 104 | 85 | 95 | 104 |
| Plafond G | 139 | 129 | 108 | 109 |
| Plafond D | 91 | 101 | 88 | 92 |
| Pilier D | **50** | 92 | 94 | 93 |
| Sol à l'ombre D | 115 | 119 | 114 | 105 |
| Mur du fond | 85 | 83 | 102 | 79 |
| Bannière verte D | **61** | 93 | 93 | 94 |

- **Cause** : la visibilité des sondes (test de Tchebychev à partir des distances), pas l'énergie de la fusion. Les tests le montrent : hystérésis 0,9 et 5 cascades ne changent rien, alors que la densité des sondes et l'intervalle font varier le résultat de 53 à 131 %. Avec des rayons de cascade 0 courts (1 espacement), les distances au-delà venaient des cascades parentes, mesurées depuis une autre sonde. Les sondes au-dessus du toit ensoleillé ou derrière les bannières passaient donc le test et fuyaient dans l'ombrage.
- **Correctif** : intervalle de la cascade 0 porté à 3 espacements (`rcInterval = 3`). Il couvre toute la portée du test de visibilité (2,6 espacements), donc les distances sont exactes. Coût : 3,2 ms GPU par image au lieu de 3,3 à 3,8 ms.
- **Passe raster** : le reflet du ciel est occulté par le rapport irradiance des sondes / irradiance du ciel ; le sol intérieur ne reflète plus le ciel bleu.
- **« Bilinear fix »** : essayé (un segment tracé par sonde parente, option « Correction bilinéaire ») mais moins bon ici, avec un plafond à 291 %, et 2 fois plus cher. Désactivé par défaut.
- **Écart restant** : le mode « RC + ombrage ray tracé » est dans ±15 % du path tracing partout. Le mode raster garde un plafond gauche à +29 %, dû à l'ombrage raster plus qu'aux cascades.

Captures après correctif : `docs/rc_screens/8_atrium_RC_v2.png`, `9_atrium_RC_v2_indirect_seul.png`, `10_atrium_RC_v2_ombrage_raytrace.png`.

## Limites connues

- Les 4 × 4 directions de la cascade 0 laissent un léger damier sur les murs, d'où le défaut à 8 × 8.
- Une petite tache sombre peut apparaître là où une sonde tombe presque sur une surface (Cornell avec 8 192 sondes : mur du fond, en haut à droite). Elle disparaît en changeant la densité de sondes. Piste : déplacer les sondes hors des surfaces (relocalisation, comme RTXGI).
- Pas encore de « bilinear fix » complet (un rayon par parent et par direction). Le test de visibilité par sonde en est une version bon marché.
- Les reflets brillants restent ceux de la passe raster (environnement) : en intérieur, le sol prend un reflet bleuté du ciel. C'est le même défaut en DDGI raster ; il faudrait une occlusion spéculaire.
- Pas encore branché dans l'éditeur (`Viewport3D.exe`) : seul `RenderSettings::giMethod` est à exposer dans le menu Rendu.

## Recherche et mode « RC écran » (25/10, soir)

**Recherche** (dépôts clonés pour étude, hors du dépôt) :
- **Sam Pavlovic** : aucun dépôt public trouvé, ni aucun code de lui sur Radiance Cascades.
- **Articles** :
  - Sannikov, *Radiance Cascades* (JCGT, source LaTeX) ;
  - Osborne et Sannikov 2024 (*bilinear fix*) ;
  - Freeman et Sannikov, *Split Radiance Cascades* (arXiv 2607.20384, juillet 2026).
- **Codes étudiés** :

  | Dépôt | Ce que c'est | Licence |
  |---|---|---|
  | three-rc (Cody Bennett) | Sondes en espace écran, tracé dans un BVH, sans aucun temporel | Tous droits réservés : étudié, rien copié |
  | jumeder/dx12-radiance-cascades | Grille 3D en DXR | MIT |
  | tmpvar/radiance-cascades-3d-grid | Grille 3D en OpenGL | MIT |
  | dequevedo/radiance-cascades-lab | Godot, voxels + champ proche par pixel ; journal de bord très détaillé | MIT |

- **Conclusion** : les images propres sans temporel (Sponza de three-rc, post de Sam) viennent de sondes posées sur les surfaces visibles, et non d'une grille 3D. Avec une grille, les sondes proches des surfaces donnent des « lobes » et des fuites, comme le confirme le journal du lab Godot.

**Mode « RC écran »** (`RenderSettings::rcScreen`, mode 0 du viewer, implémentation propre) :
- **Sondes** : chaque cascade i pose une sonde par tuile de 8·2ⁱ pixels, là où le rayon caméra du centre de la tuile touche la scène (`RCSProbes.hlsl`).
- **Tracé** : directions 8·2ⁱ par côté, intervalles ×4 en DXR dans toute la scène, donc la géométrie et les lumières hors écran comptent (`RCSTrace.hlsl`).
- **Fusion** : les 4 sondes parentes à l'écran, pondérées par la distance au plan de la surface et par la normale.
- **Rebonds suivants** : ils viennent de la grille RC 3D, qui reste le cache.
- **Gather par pixel** (`RCSGather.hlsl`) : les 4 sondes de cascade 0 voisines, intégrées en cosinus, écrites dans une texture d'irradiance écran lue par la passe raster (t7).
- **Cascades** : ajoutées jusqu'à ce que les rayons traversent la scène (5 ici). Aucun temporel.

Mesures sur l'Atrium (% du path tracing), viewport 1 188 × 900 :

| Zone | RC grille + raster | **RC écran + raster** |
|---|---|---|
| Bannière rouge G | 88 | 95 |
| Bannière bleue G | 85 | 94 |
| Plafond G | 129 | 157 |
| Plafond D | 101 | 110 |
| Pilier D | 92 | 76 |
| Sol à l'ombre D | 119 | 108 |
| Mur du fond | 83 | 85 |
| Bannière verte D | 93 | 94 |
| GPU image entière | 3,2 ms | 12,1 ms (5,8 M rayons écran) |

- **Ce qu'apporte le mode écran** : un vrai champ proche, avec les ombres de contact du rebond au pied des piliers, des blocs et des sphères, et des coins nets. Plus aucune trace de la grille, et plus de tache sombre sur la Cornell.
- **Ce qu'il coûte** : environ 4 fois plus cher. En moyenne, il n'est pas plus juste que la grille.
- **Variantes testées** :
  - 4 px et 4 directions : 7 ms, mais moins juste (bannière verte 76 %) ;
  - 4 px et 8 directions : 21 ms ;
  - 8 px et 8 directions : le compromis retenu.
- **Plafond gauche toujours trop clair** : les rebonds suivants lisent la grille 3D, qui fuit encore un peu au-dessus du toit.

**Suite possible** :
- appliquer le « ray splitting » de Split RC (rayons partant des surfaces visibles, versés dans la cascade de leur distance) pour réduire le coût ;
- passer à des sondes en hémisphère (deux fois plus de directions utiles) ;
- utiliser les sondes écran aussi pour les reflets brillants.

Captures : `docs/rc_screens/11` à `14`.

## Corrections du soir : contact, plafonds, coût du mode écran

- **Halo bleu sous les sphères** : au point de contact, les sondes voisines sont cachées par l'objet, la confiance tombait et la passe raster retombait sur l'ambiance du ciel. Désormais, la lumière normalisée des sondes est gardée partout dans le volume (`DDGIProbe.hlsli`). Avant / après / référence : capture 18.
- **Reflets** :
  - le reflet de l'environnement est occulté par le rapport entre l'irradiance des sondes et le ciel le plus clair (hémisphère de N ou ciel autour de la réflexion), au carré ;
  - ce que le ciel ne donne plus est remplacé par la lumière des sondes, pondérée par l'ajustement split-sum (`EnvBRDFApprox`) ;
  - effets : les métaux ne sont plus noirs, et les plafonds ne reflètent plus le zénith. Le plafond gauche de l'Atrium passe de 145 à 111 %.
- **Coût du mode écran** : il venait du gather par pixel (4 sondes × 64 directions, angles solides recalculés), pas des rayons. 4 fois moins de rayons ne faisait gagner que 1,5 ms. Chaque sonde de cascade 0 est maintenant projetée en harmoniques sphériques d'ordre 2 (`RCSProbeSH.hlsl`), et le pixel n'évalue plus que 4 SH : **12,1 → 5,8 ms** pour la même image (1 188 × 900).
- **Fusion des cascades écran** :
  - tolérance de plan des parents proportionnelle à la tuile ;
  - quand aucun parent n'est sur la surface (face étroite, arête), la grille RC 3D prend le relais dans la direction voulue au lieu des parents les plus proches.
- **Ray splitting de Split RC** : non fait. Le coût étant dans le gather, il n'aurait pas aidé ici.

Atrium, RC écran, % du path tracing (1 188 × 900, 5,8 ms GPU) :

| Zone | % |
|---|---|
| Bannière rouge G | 97 |
| Bannière bleue G | 89 |
| Plafond G | 116 |
| Plafond D | 85 |
| Pilier D | 75 |
| Sol à l'ombre D | 104 |
| Mur du fond | 85 (le direct raster y est à 31 % contre 49 % en ray tracé : ombre du soleil de la passe raster) |
| Bannière verte D | 91 |

Captures 15 à 21 dans `docs/rc_screens`.
