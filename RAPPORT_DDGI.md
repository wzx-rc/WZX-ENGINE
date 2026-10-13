# Test DDGI : grille de sondes d'éclairage global (12/10/2026)

Copie séparée : `WZX ENGINE\_ddgi-test` (dépôt git propre, branche `ddgi-test`). Le dossier principal et `_nodal-editor` n'ont pas été modifiés.
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
