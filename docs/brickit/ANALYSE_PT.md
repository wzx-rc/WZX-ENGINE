# Path tracing BrickIt vs WZX Engine — analyse (02/11/2026)

Question de Wzx : BrickIt semble fonctionner beaucoup moins bien que WZX Engine, alors que c'est le même moteur vp3d.

## Méthode

- `BrickIt.exe --cornell` charge la scène Cornell box de WZX Engine telle quelle (`app::buildCornellBox`, sa vue
  principale, l'ambiance et la vignette de `App::loadScene`).
- Même scène, même taille (1200 × 1125), 256 échantillons : `docs/brickit/suivi/25_cornell_brickit_vs_wzx.png`
  (Viewport3D de `main` à gauche avec son interface, BrickIt à droite).
- Coût par échantillon mesuré par `--pt --stride 1 --size 1000 326 --spp 16` (temps GPU d'une image), deux passages
  à chaque fois ; convergence en direct par `--pt-trace` (échantillons et ms à chaque image, 10 s par essai).

## Résultats

| Essai | GPU par échantillon (1000 × 326) |
|---|---|
| Cornell box dans BrickIt | 4,1 – 4,4 ms |
| Briques (scène d'exemple), réglages BrickIt | 61 – 63 ms |
| Briques sans sol | 18 – 26 ms |
| Briques sans HDRI (ciel procédural) | 41 – 42 ms |
| Briques **sans image d'environnement** (`--no-env`) | **3,4 – 3,8 ms** |
| Briques, échantillonnage de l'environnement coupé dans le shader | 3,7 – 4,0 ms |
| Briques, échantillonnage gardé mais sans son rayon d'ombre | 62 – 71 ms |
| Rebonds 0 / 1 / 2 / 4 (sans HDRI) | 2,2 / 34 / 40 / 41 ms |
| Biseau, SSS, traces de doigts, débruiteur coupés | − 2 à − 6 ms chacun |

Sur la Cornell box, BrickIt donne la même image que WZX Engine au même nombre d'échantillons (il est même plus propre :
son débruiteur est actif). Le moteur n'est donc pas en cause.

## Causes

1. **L'échantillonnage de l'image d'environnement coûte ~95 % de l'image** : l'éclairage studio de BrickIt passe par
   l'HDRI 2k importance-samplé à chaque premier rebond (`EnvSample` : deux recherches dichotomiques dans une table de
   2048 × 1024, ~21 lectures dépendantes dans un tampon de 16 Mo par pixel, puis l'`EnvPdf` et l'`EnvMap`). Sur la
   Cornell box il n'y a pas d'image d'environnement, donc ce coût n'existe pas. Ce n'est pas le rayon d'ombre (le
   retirer ne change rien).
2. **En direct, BrickIt trace 16 à 32 fois moins d'échantillons par seconde que WZX Engine** : il trace 1 pixel sur 4
   par image (`traceStride 4`) et 1 échantillon par image, alors que WZX Engine trace tous les pixels et monte jusqu'à
   8 échantillons par image quand la vue est immobile (`m_adaptiveSpp`). Mesuré sur la Cornell box : 1322 images en
   10 s, soit ~330 échantillons par pixel, contre ~2700 en traçant tous les pixels à 4 échantillons par image.
3. Pendant un mouvement, BrickIt rend en plus à demi-résolution (`ptMoveScale 0.5`).

## Corrections proposées (rien n'est encore changé, à choisir)

- A. Table d'importance de l'environnement à basse résolution (par exemple 256 × 128, ~130 Ko : tient dans le cache,
  15 lectures au lieu de 21 sur 16 Mo), ou méthode des alias (2 lectures) ; l'image reste en 2k pour l'éclairage et
  les reflets. Gain attendu : la scène de briques revient vers le coût de la Cornell box.
- B. Échantillonnage adaptatif de WZX Engine dans BrickIt : 1 échantillon pendant un mouvement, jusqu'à 8 quand la vue
  est immobile, tous les pixels (`traceStride 1`) à l'arrêt ; garder 1 pixel sur 4 et la demi-résolution seulement
  pendant le mouvement.
- C. À plus long terme : HDRI plus petit (1k) pour le studio, ou éclairage studio par lumières de surface (comme les
  miniatures).

## Corrections faites (A + B, 02/11/2026)

- A (moteur vp3d, `Renderer::uploadEnvironment`) : la table d'importance de l'environnement est construite sur un niveau
  d'au plus 256 de large (chaque cellule garde la plus forte luminance des texels qu'elle couvre) ; l'image, les reflets
  et la détection du soleil restent en 2k. Viewport3D en profite (même `Renderer`).
- B (BrickIt) : échantillonnage adaptatif de WZX Engine : 1 échantillon et 1 pixel sur 4 pendant un mouvement ; à
  l'arrêt tous les pixels et +1 échantillon par image tant que le GPU reste sous 9 ms (jusqu'à 8), −1 au-dessus de 22 ms.
  Les captures (`--shot`) gardent un nombre fixe.

| Mesure | Avant | Après |
|---|---|---|
| Briques, 512 échantillons (sans interface) | 146,5 s, 480 ms / échantillon | **5,2 s, 6,9 ms / échantillon** |
| Cornell box, 512 échantillons | 18,2 s (36 ms, GPU chargé à ce moment) | 5,1 s, 6,0 ms — image identique au bit près |
| Briques en direct, 10 s, 2000 × 653 | 398 échantillons sur 1 pixel sur 4 (~100 par pixel) | **2448 par pixel** |
| Cornell en direct, ~7 s avant redimensionnement | ~330 par pixel en 10 s | 1610 par pixel |

Pas de biais : à 512 échantillons, la luminance moyenne des briques passe de 171,87 / 163,29 / 143,65 à
171,75 / 163,18 / 143,55 (sur 255), écart moyen de 1,6 = le bruit ; la Cornell (sans HDRI) est identique.
Capture : `suivi/26_pt_avant_apres.png`.
