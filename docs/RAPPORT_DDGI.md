# Test DDGI : grille de sondes d'éclairage global (12/10/2026)

> État au 14/10/2026 : ce test a été mené dans une copie séparée (`_ddgi-test`, aujourd'hui archivée localement dans `_archive/ddgi-test`). Le mode GI sondes est intégré à `main` (tag `v0.1`) ; l'historique du prototype, le viewer d'étude Cornell (`src\viewer`) et `tools\imgcmp.cpp` sont conservés dans la branche GitHub `archive/ddgi-study`. Les chemins ci-dessous se réfèrent à cette copie.

Copie séparée d'origine : `WZX ENGINE\_ddgi-test` (dépôt git propre, branche `ddgi-test`).
GPU : NVIDIA GeForce RTX 5060, Release, viewport 1203 × 700, scène Cornell Box (107 k triangles, 1 lumière surfacique).

## Ce qui a été fait

Nouveau mode de rendu « GI sondes temps réel (DDGI) » (menu Rendu, touche **L**, ou `--ddgi 1`) :

1. **Mise à jour des sondes** (`DDGIUpdate.hlsl`) : grille régulière posée sur la boîte englobante de la scène (≈ 2 048 sondes par défaut, ici 10 × 10 × 23 = 2 300, espacement 6 cm). 128 rayons par sonde et par frame, orientés au hasard à chaque frame. Au point touché : émission + lumière directe (1 rayon d'ombre) + éclairage des sondes de la frame précédente (rebonds infinis).
2. **Fusion** (`DDGIBlend.hlsl`) : irradiance en octaèdre 8 × 8 et distances 16 × 16 par sonde, hystérésis 0,97 (accélérée sur les gros changements). Les sondes coincées dans un objet sont désactivées automatiquement.
3. **Rendu** (`GIShade.hlsl`) : rayon primaire + lumière directe (1 échantillon) + GI diffuse lue dans les sondes (test de visibilité de Tchebychev contre les fuites) + 1 rayon de réflexion brillante, dont l'impact est éclairé par les sondes. Historique d'image plafonné à 32 frames, puis le débruiteur à-trous existant.

Outils de mesure ajoutés : `--bench fichier.csv --frames N --motion 0|1 --vsync 0`, `--probes N`, `--ddgi-view 0|1|2` (complet / indirect seul / direct seul), `--bounces N`, et `tools\imgcmp.exe` (écart à la référence).

## Résultats

Référence : path tracing 4 096 spp (≈ 16 s). « En mouvement » = historique d'image effacé à chaque frame, ce que voit une caméra qui bouge.

| Mode | GPU / frame | Sondes (tracé + fusion) | Scintillement frame à frame | Écart à la référence (RMSE 0-255) | Luminosité vs réf. |
|---|---|---|---|---|---|
| **DDGI, caméra fixe** | 3,9 ms | 0,21 + 0,31 ms | 0,02 % | **6,8** | 99,6 % |
| **DDGI, en mouvement** | 3,6 ms | 0,24 + 0,30 ms | **0,12 %** | **8,5** | 99,2 % |
| DDGI 540 sondes, en mouvement | 2,9 ms | 0,06 + 0,08 ms | 0,13 % | 9,9 | 100 % |
| DDGI 9 216 sondes, en mouvement | 4,8 ms | 0,52 + 1,35 ms | 0,12 % | 8,9 | 101 % |
| Path tracing 1 spp, 4 rebonds, en mouvement | 2,9 ms | – | 0,60 % | 15,9 | 96,6 % |
| Path tracing 1 spp, 10 rebonds, en mouvement | 3,9 ms | – | 0,70 % | 15,1 | 105,8 % |

(Moyennes sur les 300 dernières frames de 600, horodatages GPU. Les temps varient de ± 0,5 ms d'un lancement à l'autre selon la fréquence du GPU.)

- **Stabilité** : en mouvement, le DDGI scintille environ 5 fois moins que le path tracing interactif et son image est deux fois plus proche de la référence. L'éclairage indirect vit dans les sondes (dans le monde), donc il ne bouge pas quand la caméra bouge.
- **Coût** : la mise à jour complète des sondes coûte ≈ 0,5 ms. Le reste (≈ 3 ms) est le rendu par pixel, dominé par le biseau (4 rayons/pixel) et le rayon de réflexion : c'est là qu'il faut optimiser, pas dans les sondes.
- **Démarrage à froid** : image stable après ≈ 17 frames (≈ 0,3 s), puis affinage lent sur quelques secondes (hystérésis).
- **Densité** : 540 sondes suffisent presque pour cette boîte. Au-delà de 2 000, on paie sans gain visible.

Captures : `captures\sheet_ddgi_motion.png` et `captures\sheet_pt4_motion.png` (référence | test | écart × 4), `captures\ddgi_indirect_only.png` (GI des sondes seule), `captures\ui_ddgi.png`.

## Limites observées

- **Pas d'ombres de contact fines dans l'indirect** : les coins et le pied des blocs sont un peu moins sombres que la référence (résolution des sondes : 6 cm). Il faudra un AO écran ou tracé pour compléter.
- **Matériaux approximés dans ce mode** : la cire (SSS) est rendue en diffus, le vernis (coat) est ignoré et le verre n'est pas géré. C'est là que se trouvent les plus gros écarts sur les shader balls.
- **Réflexions** : 1 rayon par pixel, éclairé par les sondes : correct sur le métal brillant, mais encore un peu bruité sur les surfaces rugueuses.
- **Grille unique et fixe** sur la boîte englobante : ce n'est pas adapté tel quel à une grande carte RTS. Il faudra des cascades ou un volume qui suit la caméra.
- **Bords de la lumière** : légère frange de l'aliasing (jitter) autour de l'émetteur.
- Détail d'interface : le bouton du viewport affiche encore « Path tracing » et un compteur d'échantillons en mode DDGI (la barre d'état affiche bien « GI sondes (DDGI) »).
- Non testé : lumière ou objets qui bougent (le code le permet, les sondes se mettent à jour en continu, mais rien n'a été mesuré).

## Recommandation

Le DDGI est la bonne direction pour le jeu : une image stable en mouvement, une GI qui réagit d'elle-même et ≈ 0,5 ms pour les sondes. Le path tracing reste l'outil de référence (rendus finaux, captures, vérification).

Suite proposée, dans l'ordre :
1. Tester l'éclairage dynamique (soleil qui tourne, bâtiment déplacé) et mesurer le temps de réaction des sondes.
2. Brancher la GI des sondes dans le **viewport raster** (lecture des sondes dans `Raster.hlsl`) plutôt que dans un rendu par rayons. Pour un RTS, c'est probablement la voie la plus rentable : raster rapide, ombres en shadow map, GI des sondes.
3. Volume de sondes qui suit la caméra RTS (ou cascades) pour les grandes cartes.
4. Ajouter un AO léger pour les contacts, et gérer coat, SSS et verre si on garde le rendu par rayons.

## Viewer d'étude : `build\bin\DDGIViewer.exe` (13/10/2026)

Petit viewer dédié (`src\viewer\Viewer.cpp`, compilé par `build.bat`) : Cornell Box avec 6 sphères (diffuse blanche, miroir, bleue mate, jaune brillante, or rugueux, orange suspendue et animable), sans éditeur.

- Souris : glisser gauche ou droit = orbite, milieu = déplacement, molette = zoom.
- **1** DDGI, **2** path tracing, **3** raster · **V** vue GI (complet / indirect seul / direct seul) · **P** affiche les sondes (sphères teintées par leur irradiance, rouge = sonde désactivée) · **+/-** nombre de sondes · **H** hystérésis 0,97 / 0,90 · **A** anime la sphère · **L** anime la lumière · **R** vue de départ · **F12** capture.
- Ligne de commande : `--mode 0|1|2 --probes N --probes-visible 1 --view 0|1|2 --anim 1 --vsync 0 --shot fichier.png --frames N` (écrit aussi `fichier.png.txt` avec le temps GPU moyen).

Mesures (1600 × 1000, vsync coupée, 2 448 sondes) : DDGI 6,0 ms GPU (mise à jour des sondes 0,12 ms et fusion 0,26 ms), path tracing 1 spp 5,4 ms, DDGI animé 6,0 ms. Avec la vsync active, le GPU reste à basse fréquence et le temps affiché grimpe à environ 14 ms : ce n'est pas une mesure de charge.
L'affichage des sondes ajoute environ 3 ms (test brut contre toutes les sondes pour chaque pixel, outil de debug uniquement).
En animation, l'historique d'image est effacé à chaque frame : les ombres directes passent alors par le débruiteur à 1 échantillon et deviennent plus floues. Une reprojection temporelle serait la prochaine amélioration.

## Path tracing temporel et rendu unique (13/10/2026)

Décision : le rendu principal est le path tracing temporel. Le viewport raster et le menu de qualité ne sont plus proposés (code conservé, rien supprimé). Le path tracing progressif reste accessible en ligne de commande (`--temporal 0`) pour les images de référence.

### Principe
- Chaque frame trace 1 chemin par pixel (`PathTraceTemporal.hlsl`), mélangé à l'historique (moyenne exponentielle, fenêtre de 256 images par défaut).
- Reprojection : chaque pixel est retrouvé dans la frame précédente par sa position monde (caméra précédente). Un historique n'est accepté que si la surface est la même (distance < 1 % de la profondeur, normale géométrique cohérente), avec recherche sur les 4 voisins.
- Historique tenu dans des textures copiées à chaque frame (`copyHistory`). Il est réinitialisé seulement si l'éclairage ou les matériaux changent (signature de scène), pas quand la caméra bouge.
- Le débruitage à-trous reste actif (il améliore la précision à la fin, voir ci-dessous).

### Mesures (RTX 5060, 1203 x 700, 10 rebonds, référence = path tracing progressif 4096 spp)

| Configuration | Image 30 | Image 120 | Image 270 |
|---|---|---|---|
| Temporel, cap 256, débruitage (défaut) | 12,1 | 8,6 | **5,7** |
| Temporel, cap 256, sans débruitage | – | – | 6,6 |
| Temporel, cap 128, débruitage | – | – | 8,4 |
| Temporel, cap 64, débruitage | – | – | 10,7 |
| Progressif 64 spp (référence de comparaison) | – | – | 8,3 |
| Progressif 256 spp (référence de comparaison) | – | – | 4,9 |

Erreur = RMSE sur 0-255 (sRGB). Le biais de luminosité est de +0,8 % à l'image 270 avec le débruitage.

Temps GPU (moyenne) : temporel 4,1 ms (caméra fixe), 4,3 ms (caméra qui oscille), DDGI 3,5 ms, progressif 9,7 ms (il augmente ses échantillons par frame à l'arrêt, donc non comparable).

Mouvement (oscillation de caméra ±0,15 rad, 121 images) : scintillement moyen 0,32 % (p95 0,46) en temporel, contre 0,74 % (p95 0,78) en progressif, qui repart de zéro à chaque frame.

Sphère animée (viewer, image 150) : cap 256 et cap 64 ne diffèrent que de 4,7 de RMSE, et aucune traînée visible n'apparaît à cap 256 (contrôle visuel seulement).

### Corrections faites en route (pour mémoire)
- L'historique n'était jamais lu : la table de descripteurs des textures u10/u11 n'était pas liée aux passes de calcul.
- Un rejet sur différence de luminosité était trop strict : un chemin à 1 échantillon est très bruité, donc l'historique était remis à zéro en permanence. Supprimé.
- Les silhouettes scintillaient (la surface stockée changeait d'une frame à l'autre) : recherche sur les voisins et normale géométrique (stable malgré le biseau).
- La reprojection ne correspondait pas exactement au point de référence : les deux calculs de direction partagent maintenant la même fonction (`CurPixelDir` / `PrevPixelDir`), donc une caméra immobile est reprojetée au pixel près.

### Limites connues
- Lumière ou matériau modifié : l'historique est effacé. Pendant le mouvement d'une lumière, l'image retombe à 1 échantillon + débruitage (plus bruitée).
- Ombres portées par un objet en mouvement sur une surface fixe : la reprojection ne les voit pas. Une traînée peut persister jusqu'à 256 images (environ 4 s à 60 fps). Pas observé sur la sphère animée, mais pas mesuré sur une scène plus complexe.
- Profondeur de champ : la reprojection utilise le centre du sténopé. Avec la PdC activée, les tests sont plus stricts et l'image peut être plus bruitée (non mesuré).
- Testé seulement sur RTX 5060 et sur les deux résolutions 1203 x 700 et 1600 x 1000.
- Le dépôt principal `WZX ENGINE` n'est pas modifié : le correctif reste à porter après validation.

### Ce qui change dans l'interface et la ligne de commande
- Menu Rendu : « Path tracing temporel » (défaut), « GI sondes (DDGI, expérimental) », « Relancer l'accumulation », « Paramètres de rendu ». Bouton P et bascule viewport supprimés de l'interface.
- Options : `--temporal 0|1`, `--temporal-cap N` (256 par défaut), `--denoise 0|1`, `--sway A` (oscillation de caméra, benchmark), `--orbit 1`, `--snap-every N`, `--bench fichier.csv --frames N`.
- Viewer : touches 1 (DDGI) et 2 (path tracing temporel) ; options `--light-anim 0|1`, `--cap N`.

### Pistes suivantes
1. Valider le rendu sur la scène RTS réelle, puis porter le correctif dans `WZX ENGINE`.
2. Historique « souple » pour une lumière qui bouge (réduction progressive au lieu d'un effacement).
3. Reprojection tenant compte de la PdC.
4. Mesurer la traînée d'ombre sur une scène avec objets mobiles.

## Lissage des polygones, anti-aliasing et scintillement des contours (13/10/2026)

### Nouvelles options (menu Inspecteur > Rendu)
- **Lissage des polygones** : normales interpolées (défaut) ou faces plates (normales géométriques). Effet faible sur les sphères, qui sont finement maillées ; les boîtes sont déjà plates.
- **Anti-aliasing** : désactivé (pas de jitter), standard (1 chemin jitté par image, défaut), élevé (4 chemins par image).

### Scintillement de la sphère or
Cause : pas les reflets ni la zone dense, mais les contours (silhouettes des sphères, arêtes des boîtes). L'historique était validé avec la surface touchée par l'échantillon jitté. Quand cet échantillon tombait de l'autre côté d'une arête, l'historique était rejeté, et l'image repartait d'un seul échantillon. Écrêter les fireflies à 8 au lieu de 25 ne change presque rien (-4 %), donc ce n'est pas la passe de réflexion.

Correction : la validation utilise maintenant la surface du rayon au centre du pixel, qui ne change pas d'une frame à l'autre. Les échantillons jittés sont toujours moyennés, donc l'anti-aliasing reste correct.

Mesures (1203 x 700, 320 images, écart-type de la luminance par pixel sur les images 200 à 319, échelle 0-255) :

| Configuration | Écart-type moyen | Écart-type sphère or | Coût GPU | Écart à la référence |
|---|---|---|---|---|
| Avant correction | 0,76 | 3,74 | 4,0 ms | 5,65 |
| Après correction (défaut) | 0,40 | 0,69 | 4,0 ms | 4,54 |
| Après correction, AA désactivé | 0,38 | 0,67 | 4,0 ms | 4,75 |
| Après correction, AA élevé (4 chemins) | 0,29 | 0,50 | 10,8 ms | 4,37 |
| Avant correction, écrêtage 8 | 0,72 | 3,57 | 4,0 ms | – |

Pour l'anti-aliasing, « désactivé » donne des contours plus crénelés, « élevé » les lisse davantage mais coûte 2,7 fois plus de temps GPU.

### Limites
- Le mode DDGI (`GIShade`) garde l'ancienne validation : la correction n'y est pas encore portée.
- Le lissage plat ne se voit presque pas sur cette scène. Il servira davantage sur des maillages grossiers (lathes, modèles importés).

## Sphère « Or rugueux » du DDGIViewer : scintillement (13/10/2026)

Mesure dans le DDGIViewer (1600 x 1000, images 200 à 319, écart-type de la luminance par pixel, échelle 0-255, gain 20). Outils : `--stat-from`, `--stat-rect`, `--stat-gain` (viewer et application), `--clamp`, `--denoise`, `--aa`, `--hysteresis`.

| Configuration | Sphère + contours | Intérieur | Point brillant (reflet du plafonnier) |
|---|---|---|---|
| DDGI, validation sur l'échantillon jitté (avant) | 1,34 | 0,76 | – |
| DDGI, validation sur le rayon central | 0,82 | 0,56 | 1,21 |
| + lumière stratifiée et 4 rayons de reflet sur les surfaces brillantes | 0,80 | 0,51 | 1,18 |
| Mode PT temporel (viewer) | 0,75 | – | 1,28 |

Ce qui a été réduit : les contours (silhouettes, même cause que pour l'application : validation sur l'échantillon jitté) et le motif de maillage à l'intérieur de la sphère (rejet de rayons de reflet sous la facette géométrique, qui dépend de la tessellation).

Ce qui reste : un petit point brillant en haut de la sphère, qui est le reflet du plafonnier. Tests qui ne le réduisent pas : écrêtage des fireflies (à 200 : identique), lumière stratifiée, 4 rayons de reflet, jitter désactivé (1,17), hystérésis des sondes (0,90 : 1,07). Le débruitage le réduit de moitié (2,44 sans débruitage). C'est une variance d'échantillonnage : un seul chemin par pixel et par image, avec un reflet très vif. La réduire demande plus d'échantillons sur ces pixels (le mode « Anti-aliasing élevé » de l'application réduit ce type de point d'environ 27 %, pour 2,7 fois le temps GPU).

Observation : avec une hystérésis de 0,995 (valeur hors interface), l'image DDGI devient noire. Non traité, car la touche H n'utilise que 0,97 et 0,90.

## Halos temporels : validation des contours (13/10/2026)

Constat : la validation de l'historique sur le rayon central (commits a1bec5c et 9970f1d) supprime le scintillement des contours, mais elle garde pendant 256 images le mélange des pixels de bord. Quand un objet bouge, l'ancienne couleur reste sur les bords : des halos apparaissent autour des objets.

Décision : retour à la validation précédente par défaut (contours sans halo). La validation au centre reste disponible avec la case « Contours stables (halos possibles) » (`--stable-edges 1` dans l'application et le viewer).

| Réglage | Sphère + contours (écart-type, images 200 à 319) | Aspect |
|---|---|---|
| Défaut (validation précédente) | 1,32 | contours propres, sans halo |
| Contours stables | 0,80 | points brillants sur les bords, halos possibles |

Contrôle visuel (sphère orange animée, image 150) : défaut propre ; contours stables avec points brillants sur le bord.

Piste non implémentée : garder la validation au centre pour la stabilité, mais limiter la mémoire des pixels dont le bord change (par exemple 16 images au lieu de 256). Les halos durent alors moins longtemps, et les contours restent plus stables qu'avec le défaut.

Viewer : l'animation avance d'un pas fixe de 1/60 s, pour que les comparaisons soient reproductibles.

## DDGI sans ray tracing : sondes raster et visibilité raster (14/10/2026)

Objectif : la DDGI tourne sans aucun rayon (iOS), avec un rendu proche de la DDGI ray tracing de l'éditeur.

### Ce qui a été fait
- `--ddgi-raster 1` ne lance plus aucun rayon : captures des sondes en raster (6 faces de 16 × 16 par sonde, fond
  d'environnement dessiné sous la géométrie), rayons lus dans les captures (`DDGIRaster.hlsl`), fusion
  (`DDGIBlend.hlsl`), puis la **passe raster avant** (`Raster.hlsl`, la même que Lit) remplace son ambiance
  hémisphérique par la lecture des sondes (`DDGIProbe.hlsli`, même code que les passes compute). Pas de TLAS, pas
  d'historique d'image, pas de G-buffer (l'essai G-buffer archivé sur `archive/ddgi-raster-gbuffer` est abandonné).
- Module Lit (Diligent, `src/lit`) : mêmes passes (`LitRenderer::recordProbes`), mêmes shaders, grille de sondes et
  constantes calculées par le code partagé `LitPrep` (`placeProbeGrid`, `ddgiConstants`). Lecteur : `--ddgi 1`,
  `--ddgi-view 0|1|2`, `--probes N`, `--show-probes 1`, scène `--scene cornell`.

### Corrections trouvées en mesurant
1. **Fusion des sondes raster** : seules les sondes capturées dans l'image sont fusionnées. Les rayons des autres
   avaient été pris avec une autre rotation du jeu de rayons (tirée par image) ; les refusionner chaque image étalait
   leur radiance dans toutes les directions (indirect à 1,46 de la référence). Hystérésis par cycle de capture
   (`0,97^(total / sondes par image)`, plancher 0,5).
2. **Shadow map de la lumière clé** (éditeur et Lit) : visée le long de la normale de la lampe, plan proche à la lampe.
   Avant, elle visait le centre des bornes de scène (le sol du Cornell file vers la caméra) et le plafond, derrière la
   lampe, occultait toute la scène : le viewport raster du Cornell n'avait plus de lumière directe.
3. **Captures** : la sonde est le point de vue de son shading (normale et V), et la capture ne contient plus le terme
   d'ambiance (le rebond vient des sondes, sinon il comptait deux fois).

### Mesures (Cornell, 1335 × 726, 400 images, référence = DDGI ray tracing de l'éditeur, `tools\imgcmp`)

| Test | Luminance test / référence | Écart RMSE (0-255) |
|---|---|---|
| Éditeur DX12, sondes raster + passe raster, indirect seul | 1,07 | 8,2 |
| Éditeur DX12, sondes raster + passe raster, image complète | 1,07 | 17,2 |
| Éditeur DX12, direct seul | 1,08 | 19,6 |
| Sondes raster contre sondes RT (sphères de debug) | 0,99 | 5,6 |
| Lit (Diligent, DirectX 12) contre éditeur DX12 sondes raster, image complète | 1,00 | **1,6** |
| Lit contre éditeur, indirect seul | 0,99 | 2,0 |
| Lit contre la référence ray tracing | 1,06 | 17,2 |

Les 7 % restants viennent de la passe raster elle-même, pas des sondes : reflets brillants lus dans les sondes autour
de la direction réfléchie (flous) au lieu de rayons de réflexion, ombres PCF au lieu de la lumière surfacique
échantillonnée. Diagnostic : `--ddgi-raster-view 1` (passe raster sur les sondes ray tracing) donne 1,07 / 5,0.

### Limites et suite
- Non testé sur iOS (Metal via MoltenVK) : les passes compute (`RWTexture2D` RGBA16F lues et écrites) y restent à
  vérifier ; la compilation glslang de `DDGIBlend.hlsl` et `DDGIRaster.hlsl` aussi.
- Reflets : les surfaces brillantes lisent les sondes autour de la réflexion (flou). Une réflexion écran ou une
  cubemap locale serait la suite pour les métaux.
- Les sondes mettent `total / sondes par image` images à se rafraîchir (72 images pour 2 300 sondes à 32 par image) :
  une lumière qui bouge se voit avec ce retard. `--ddgi-capture N` règle le compromis.

Outils : `tools\imgcmp` (écart et planche référence | test | différence × 4), `tools\shaders.bat` (shaders seuls),
`VP3D_DUMP_CAPTURE=chemin` (captures des sondes de la dernière image, éditeur), `--show-probes 1`.
