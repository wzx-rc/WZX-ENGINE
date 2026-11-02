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

## Ombre du soleil ray tracée dans la passe raster

- **Pourquoi** : la shadow map PCF (2 048 px pour toute la scène) donnait une ombre floue et crénelée.
- **Principe** : en mode raster avec GI, l'ombre du soleil est tracée par pixel contre la scène (`SunShadowRT` dans `Raster.hlsl`, pixel shaders compilés en SM 6.5, TLAS en t9 de la passe raster).
- **Pénombre** : 8 rayons dans le disque du soleil (diamètre 0,53°, réglable), sur une spirale de Vogel tournée par pixel.
- **Anti-crénelage** : chaque rayon part d'un point différent de l'empreinte du pixel (`ddx` / `ddy`), ce qui adoucit les bords sans accumulation temporelle.
- **Réglages** : `RenderSettings::rtSunShadows`, `sunAngle`, `rtShadowSamples`. Le viewer les expose dans la section « Ombre du soleil », et en ligne de commande avec `--rtshadow`, `--sunangle`, `--rtsamples`.
- **Coût** (Atrium, 1 188 × 900) : 5,7 ms avec la shadow map, 6,7 ms avec 4 rayons, 8,3 ms avec 8 rayons.
- **Comparaison** : capture 22 (shadow map / ray tracé / path tracing).

**HDRI** : le moteur éclaire déjà par HDRI, que ce soit en path tracing, en raster ou dans les sondes et cascades. Le viewer permet maintenant de choisir un HDRI sur toutes les scènes (capture 24 : Atrium avec le ciel procédural).
- **Coût** : environ 48 ms GPU en mode RC écran dans une grande fenêtre. L'échantillonnage d'importance de l'HDRI est fait à chaque impact des 9 M rayons écran.
- **Piste** : n'éclairer les impacts écran que par le soleil de l'HDRI et laisser le ciel passer par les cascades.

### Version filtrée (sans temporel)

La première version, avec les rayons tracés dans le pixel shader, laissait un bord granuleux et un liseré pointillé sur la ligne jour / nuit des sphères. Elle est remplacée par deux passes compute avant la passe raster :

1. **`SunShadowTrace.hlsl`** :
   - rayon caméra par pixel, puis 8 rayons dans le disque du soleil (spirale de Vogel tournée par pixel) ;
   - les rayons partent de la surface lissée décrite par les normales des sommets (« Hacking the shadow terminator », Hanika 2021), ce qui supprime le liseré des sphères facettées ;
   - écrit la visibilité, la profondeur et la normale.
2. **`SunShadowFilter.hlsl`** : filtre bilatéral 5 × 5 guidé par la profondeur (2 %) et la normale. La pénombre est lisse et ne déborde pas d'un objet à l'autre.

La passe raster lit le masque filtré (t8), ses pixel shaders repassent en SM 6.0.

Coût sur l'Atrium (1 188 × 900), image entière :
- shadow map : 5,63 ms
- 4 rayons : 6,56 ms (+0,93 ms)
- **8 rayons (défaut) : 7,06 ms (+1,43 ms)**

Captures 25 (gros plan shadow map / ray tracé filtré / path tracing) et 26 (couloir).

## Reflets ray tracés (métaux et surfaces brillantes)

Dans les modes raster avec GI, le reflet de l'environnement est remplacé par un reflet tracé par pixel :
- **`ReflectTrace.hlsl`** :
  - rayon caméra, puis, pour les métaux et les surfaces de rugosité inférieure à 0,5 : 1 rayon miroir exact (rugosité < 0,04) ou 4 rayons GGX (échantillonnage des normales visibles, ensemble R2 tourné par pixel) ;
  - les impacts sont éclairés par l'émission, la lumière directe et l'HDRI (4 échantillons, les mêmes pour tous les pixels, pour qu'un miroir ne montre pas de grain) et par les sondes (cascades ou DDGI) pour le reste ;
  - un rayon qui ne touche rien voit l'environnement.
- **`ReflectFilter.hlsl`** : filtre bilatéral dont le rayon grandit avec la rugosité. Il est guidé par la profondeur, la normale et la rugosité ; les miroirs ne sont pas filtrés.
- **Passe raster** : utilise ce reflet (t9) avec son poids de Fresnel / rugosité (`EnvBRDFApprox`). Aucun temporel.

Coût (1 188 × 900, sans → avec reflets) :
- Cornell : 6,96 → 7,21 ms
- LookDev : 6,13 → 6,66 ms
- Atrium : 6,98 → 7,28 ms

Captures :
- 27 : bille dorée de la Cornell, avant / après / path tracing ;
- 28 : chrome du LookDev, avant / après / path tracing ;
- 29 et 30 : LookDev et Cornell entiers.

**Limite** : le verre reste celui de la passe raster (pas de réfraction tracée).

## Split Radiance Cascades dans l'éditeur (31/10/2026)

Article : Freeman, Sannikov et Pitt-Francis, *Split Radiance Cascades: Real-Time Global Illumination via Sparse Radiance Probes* (arXiv 2607.20384, juillet 2026). Implémentation propre d'après le texte de l'article, sans code repris. PC seulement (DXR), rien pour iOS.

**Dans l'éditeur** : nouveau mode de rendu « Split RC » (menu de rendu à côté de Lit / Metal iOS / Progressive / GI, ou `Viewport3D.exe --split 1`). Le mode Lit n'est pas touché. Options dans l'onglet Render : taille des sondes en pixels, premier intervalle, nombre de cascades, jitter animé, rebonds, vue indirect seul, ombre du soleil et reflets ray tracés. Les stats du viewport affichent le temps GPU de l'image et de Split RC.

**Principe** (fichiers `SRC*.hlsl`, `Renderer::recordSplitRC`) :
- **Sondes clairsemées** : une table de hachage par cascade, des sondes seulement dans les cellules qui contiennent une surface visible (et leurs parents). Cascade i : espacement s0·2ⁱ, (8·2ⁱ) × (4·2ⁱ) directions en projection à aires égales (32 à 2 048), intervalle ]t0·4ⁱ⁻¹, t0·4ⁱ], la dernière jusqu'à l'infini.
- **Niveaux de détail** : selon la distance de Tchebychev à la caméra, une cellule de cascade 0 fait environ 12 px à l'écran à toute distance. Les LOD se recouvrent sur 10 % et sont fondus.
- **Ray splitting** : un seul rayon par pixel, lancé depuis la surface visible. Selon sa distance d'impact, il dit « rien dans mon intervalle » aux cascades du dessous et dépose la radiance de l'impact dans la cascade de cette distance (sommes atomiques entières). Les directions viennent d'une suite R2 découpée en segments contigus par sonde et par parent, donc chaque cascade est couverte uniformément.
- **Fusion** de haut en bas (L = R + T × cône du dessus, trilinéaire sur les parents existants), irradiance 6 × 6 par sonde de cascade 0, puis lecture par pixel dans la texture d'irradiance écran (t7) de la passe raster.
- **Aucune accumulation temporelle**. Les rebonds suivants lisent la grille RC monde, qui n'a pas de temporel non plus.

**Fantômes** (Cornell 1 068 × 581, vue indirect seul). Une scène est animée, capturée en mouvement à l'image 480, puis comparée à la même pose immobile et convergée (écart RMS, sur 255) :

| Test | Split RC | DDGI (même passe raster) | GI (DDGI + historique) |
|---|---|---|---|
| Lumière qui se déplace | **3,0** (surtout le contour du plafonnier) | 9,0 | 10,8 |
| Bloc qui se déplace | **1,0** | 3,3 | 4,6 |

La DDGI laisse la lumière là où elle était (mur rouge trop éclairé, captures 31 et 33). Split RC suit dès l'image suivante (captures 32 et 34). Caméra qui oscille : même scintillement que la DDGI (écart type 7,58 contre 7,61).

**Coût GPU** (RTX 5060, 1 068 × 581, image entière) :

| Scène | Split RC | dont passes Split RC | DDGI raster | GI (DDGI) |
|---|---|---|---|---|
| Cornell | 4,6 ms | 1,4 ms | 2,4 ms | 1,5 ms |
| LookDev | 6,6 ms | 2,5 ms | | |
| Village (vide) | 4,4 ms | 1,1 ms | 3,3 ms | |

Le reste du coût de Split RC vient de la grille RC monde (cache des rebonds, environ 1,6 ms) et des ombres et reflets ray tracés.

**Qualité** :
- L'indirect seul est proche de la DDGI convergée, avec un meilleur contact au pied des blocs (captures 37 et 38).
- Le LookDev est proche du path tracing, avec un sol plus juste que la DDGI (captures 35 et 36).
- Avec des sondes de 8 px, il reste des taches au plafond de la Cornell (trop peu de rayons par direction) : d'où le défaut à 12 px.
- Le disque du soleil d'un HDRI vu directement par un rayon est plafonné comme les fireflies du path tracer (`fireflyClamp`). Sans cela, il fait des taches sur le sol.

**Limites et suite** :
- Le contour du plafonnier qui bouge garde une image de retard.
- La grille RC monde des rebonds pourrait être remplacée par un cache secondaire Split RC, comme dans l'article.
- Pas d'accumulation temporelle optionnelle (l'article en propose une pour les scènes fixes).
- Les sondes de la cascade 0 sont limitées à pixels / 24 (au-delà, la cellule est ignorée).

Test : `Viewport3D.exe --scene cornell --split 1 [--split-px 12] [--ddgi-view 1] --anim-object 6 --anim-move 0,0,12 --anim-period 240 --shot-viewport f.png --spp 480`. `--anim-light 1` fait bouger la lumière, `--anim-at F` fige la pose de l'image F (référence), et `f.png.txt` reçoit les temps GPU.

### Accumulation quand rien ne bouge, 6 cascades, vues d'inspection (02/11/2026)

- **Accumulation** (« Accumulate while still », 16 images par défaut, 0 = coupé) :
  - l'irradiance de chaque sonde de cascade 0 est moyennée avec celle de la même cellule monde à l'image précédente (table de la cascade 0, irradiance et nombre d'images copiés dans un second tampon, u22). Le jitter des rayons tourne alors à chaque image ;
  - la caméra peut bouger sans perdre l'historique, puisqu'il est dans l'espace monde (capture 41 : presque tout est blanc, c'est-à-dire plein historique, pendant que la caméra oscille) ;
  - l'historique est effacé autour d'un objet qui bouge (sa boîte avant et après, agrandie de la moitié de sa taille ; au-delà de 8 objets, tout est effacé), et partout quand une lumière, un matériau, l'environnement ou un réglage change.
- **Mesures** (Cornell, 1 335 × 726) :
  - scintillement, caméra fixe : écart type moyen par pixel 0,31 sans accumulation, **0,05** avec ;
  - fantômes (écart à la pose figée) : bloc qui bouge 0,92 sans, **0,28** avec ; lumière qui bouge 2,47 sans, 2,41 avec. L'accumulation ne crée donc pas de fantômes ;
  - coût : environ 0,5 à 0,8 ms de plus.
- **Cascades** : 2 à 6. Les cascades 4 et 5 ont 8 192 et 32 768 directions.
- **Inspection** (onglet Render, section Inspection, ou `--split-debug N`) :
  1. cellules des sondes d'une cascade au choix (capture 39) ;
  2. irradiance de la sonde la plus proche, sans mélange ;
  3. cascade où le rayon a trouvé sa lumière (capture 40) ;
  4. rayons par sonde ;
  5. LOD ;
  6. images accumulées.

### Petits carrés sombres (02/11/2026)

- **Cause** : des sondes isolées recevaient trop peu de rayons vers la sphère émissive. C'est surtout visible en vue proche, où les cellules du LOD 0 devenaient grandes à l'écran, et là où l'historique est effacé (objet déplacé). Capture 43 : indirect seul, sans accumulation, sondes de 6 px.
- **Correctifs** :
  - filtre spatial des sondes (« Probe filter », activé par défaut) : chaque texel d'irradiance est moyenné avec celui des 6 cellules voisines, la sonde elle-même compte double. L'historique garde la version non filtrée ;
  - espacement de base 16 fois plus fin, donc les LOD gardent environ 12 px même tout près de la caméra (LOD bornés à 15).
- **Résultat** : capture 44, même vue, propre sans accumulation.
  - scintillement caméra fixe : 0,28 sans accumulation, 0,05 avec ;
  - fantôme du bloc : 0,75, contre 3,3 en DDGI.
- **Coût** : une vue proche du LookDev monte à environ 10 ms de Split RC, contre 4,6 ms de plus loin, parce que plus de rayons touchent les sphères (ombrage des impacts).

### Ombres de la lampe de la Cornell (02/11/2026)

En raster avec GI, seul le soleil avait une ombre ray tracée. Sans soleil, la lumière principale est la lampe rectangulaire, qui n'avait que la shadow map, et sur la Cornell aucune ombre n'apparaissait.

Désormais, quand il n'y a pas de soleil, `SunShadowTrace` trace l'ombre de la lampe principale :
- 16 rayons par pixel vers des points répartis sur son rectangle (suite R2 tournée par pixel) ;
- puis le même filtre bilatéral que pour le soleil.

La passe raster lit ce masque pour la lampe principale. Capture 45 : ombres douces des blocs et des billes. Le mode Lit n'est pas touché.

### Liseré blanc aux bords, ombres de lampe, mémoire vidéo (02/11/2026)

- **Liseré** :
  - cause : avec le MSAA, un pixel de bord ombre les deux surfaces qu'il couvre, mais l'ombre, la GI écran et les reflets tracés sont calculés pour la seule surface vue par le centre du pixel. L'autre surface prenait donc les valeurs de sa voisine, d'où des pointillés blancs ou sombres le long des arêtes ;
  - correctif : la passe raster lit ces textures au pixel du voisinage 3 × 3 dont la profondeur au centre correspond à celle du fragment (t10 = la trace brute du soleil, qui contient cette profondeur) ;
  - avant / après : captures 46 et 47. Les rebords très fins (soucoupes) gardent un léger pointillé.
- **Ombre de la lampe** : filtre bilatéral 9 × 9 (sigma 3 px) au lieu de 5 × 5, plus proche de l'ombre douce du mode GI.
- **Mémoire vidéo** :
  - les cascades étaient dimensionnées pour le pire cas, soit 200 Mo ou plus avec 4 cascades et davantage avec 6. À côté d'une autre application 3D (un second éditeur ouvert), elles débordaient de la mémoire vidéo et chaque passe devenait environ 50 fois plus lente (Split RC à 250 ms) ;
  - désormais, la capacité de chaque cascade suit le nombre de clés de l'image précédente (×2), lu sur le GPU : Cornell 2,3 ms en 4 cascades ;
  - les stats de l'onglet Render affichent sondes / capacité ;
  - `VP3D_SRC_PROFILE=1` écrit le temps de chaque passe dans le `.txt` des captures.
- **Contact bloc / sol** : le long de la ligne où un bloc touche le sol, les deux surfaces ont la même profondeur, donc les pointillés restaient. Le choix du pixel compare maintenant aussi la normale. La ligne est propre : capture 48.

### Réglages de qualité AAA (02/11/2026)

Onglet Render > Quality (modes GI raster ; Lit reste en MSAA 4x et n'est pas touché) :

- **Anti-aliasing** :
  - MSAA 4x (comme avant) ;
  - MSAA 8x ;
  - AAA : MSAA 8x résolu en espace tonemappé (`ResolveTM.hlsl`, poids 1 / (1 + max rgb) par échantillon, Karis). Les arêtes claires sur fond sombre ne font plus d'escalier.
  - Le nombre d'échantillons change à la volée : les pipelines MSAA et les cibles sont recréés.
- **Ombres** (soleil et lampe principale, de « low » à « AAA ») :
  - rayons par pixel pour le soleil : 2, 4, 8, 16, 32, et deux fois plus pour la lampe ;
  - rayon du filtre bilatéral : jusqu'à 8 px ;
  - le filtre compare la profondeur à celle du plan du pixel (pente locale), donc un sol vu en rasant n'écarte plus ses voisins ;
  - « high » est le réglage précédent.
- **Inspection** : nouvelle vue « Shadow mask » (le masque d'ombre ray tracé).
- **Coût** (Cornell, 1 335 × 726) : 7,8 ms en standard, 12,2 ms en AAA, dont environ 3 ms pour les 64 rayons de lampe et 1 à 2 ms pour le MSAA 8x.
- **Limite** : au coin mur / sol (deux surfaces au même pixel et sans voisin de la même surface), un léger pointillé peut rester. Pour l'effacer, il faudrait tracer l'ombre par fragment sur ces pixels de bord.
- Ligne de commande : `--aa 0..2 --shadow-quality 0..4 --split-debug 7`. Capture 49 : standard à gauche, AAA à droite.
