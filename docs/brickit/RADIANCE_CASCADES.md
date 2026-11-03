# Radiance Cascades dans BrickIt (portage du rendu Split RC de WZX Engine)

Source : branche `claude/project-thread-0x1y2n` de WZX Engine (17 commits : Split Radiance Cascades, bords MSAA,
choix du pixel par profondeur et normale, modes GI raster, ombres et reflets ray tracés par pixel, réglages AA et
ombres), **fusionnée dans `brickit-native`** (commit de fusion 3b5e91c). Viewport3D garde tous ses modes (Lit, Metal
iOS, Progressive, GI, Split RC) et RCViewer.exe est construit aussi.

## Le mode dans BrickIt

- Réglages › Rendu : Rapide · HQ · PT · **RC** (aussi dans le menu Qualité de l'en-tête : « Radiance Cascades »).
- Réglages › Anti-aliasing (Radiance Cascades) : MSAA 4x · MSAA 8x · AAA (MSAA 8x résolu en espace tonemappé).
- Réglages › Ombres (Radiance Cascades) : Basse · Moyenne · Haute · AAA (rayons d'ombre du soleil / de la lampe : 8 / 16
  en Moyenne, 32 / 64 en AAA).
- Ligne de commande : `--rc`, `--aa 0..2`, `--shadows 0..4` ; enregistré dans `brickit_prefs.json` (`settings.rc`,
  `aa`, `shadows`).
- Réglage du moteur identique à `App::setRenderMode(4)` de WZX Engine : volume DDGI + vue raster des sondes,
  sondes calculées par les Split Radiance Cascades (`giMethod = 1`, `rcSplit`), reconstruites à chaque image, sans
  mélange temporel (accumulation en espace monde seulement quand rien ne bouge). Aspect : celui du path tracing
  (studio clair, HDRI), pour comparer. Le biseau raster (requêtes de rayons) est actif dans ce mode.

## Mesures (RTX 5060, 2000 × 1125, GPU par image)

| Scène | Rapide | HQ | **RC** | PT (par échantillon) |
|---|---|---|---|---|
| Scène d'exemple (58 pièces) | 0,92 ms | 3,60 ms | **11,8 ms** | 5,23 ms |
| 10 000 briques (`--bench 10000`, 3,2 M triangles) | 1,95 ms | 3,77 ms | **14,6 ms** | 5,65 ms |

RC donne une image complète (éclairage global, ombres douces, débordement de couleur) à chaque image ; le path
tracing a besoin de centaines d'échantillons pour converger (512 pour la capture 37b, environ 2,7 s).
Viewport3D, boîte de Cornell en Split RC après la fusion : 7,1 ms dont 2,4 ms de cascades (capture 37f).

Captures : 37a (RC, scène d'exemple), 37b (PT 512 éch., même vue), 37c (RC, 10k briques), 37e (PT, 10k briques),
37d (Réglages), 37f (Viewport3D Split RC après la fusion).

Écarts RC / PT vus sur 37a / 37b : le sol est un peu plus sombre en RC et ses carreaux plus contrastés ; les pièces
transparentes (axes gris translucides) sont plus claires ; le reste (ombres de contact, rebonds colorés) est proche.

## Fusion : ce qui a été résolu

- Constantes raster : hémisphère (BrickIt), biseau raster, ombres / reflets ray tracés, profondeur de centre (RC)
  dans la même structure (Raster.hlsl, RendererRaster.cpp, LitPrep.h) ; copie sans jitter du raster déplacée à 768 o
  (la structure fait 544 o), constantes des vues annexes à 1536 o.
- Registres du raster : t7 reste le tampon des dessins instanciés (BrickIt / main) ; l'écran GI, l'ombre du soleil,
  les reflets et la profondeur de centre (RC) passent en t11..t14, deuxième plage de la même table que les sondes.
- Tas de descripteurs : emplacements RC à la suite des sondes, puis sélection, profondeur (SSAO), biseau, vues.
- Signature racine compute : les paramètres RC (p16..p22) et la table de sélection en p23 (24 paramètres, 42 DWORD).
- MSAA : `createMsaaPipelines` (main) appelé par `createRasterPipelines(error, msaaOnly)` (RC, changement 4x / 8x).
- Ombres / reflets tracés à l'écran et résolution AAA limités à la vue principale (leurs textures ont sa taille).

## Sol infini : les « paquets » (03/11/2026, retour de Wzx)

Symptôme : caméra inclinée, le sol se couvre de carrés clairs / foncés jusqu'à l'horizon, plus gros au loin.
Cause : le Split RC insère une sonde de cascade 0 pour chaque pixel visible ; le sol de BrickIt va jusqu'à l'horizon,
les cellules (de plus en plus grosses avec la distance, LOD) se comptent par dizaines de milliers. Les tables de
hachage sont dimensionnées sur l'image précédente et plafonnées (pixels / 24, 65 536) : quand la caméra s'incline, les
cellules en trop n'ont pas de sonde, leurs pixels retombent sur l'ambiance de Lit (confiance 0) — un damier de
cellules. Pas une question de précision : la même scène vue de haut est propre.

Correction (astuce, sans couper le sol) : **bornes de la GI** (`RenderSettings::giBoundsMin / Max / Fade`, constantes
`SplitRCConstants::giMin / giMax`). BrickIt les règle sur la boîte des briques + une marge (35 % de la taille, 6 cm au
moins), recalculées quand la scène change. Hors de la boîte agrandie de la marge : aucune sonde insérée (SRCInsert) ;
la confiance de la GI s'estompe en douceur (smoothstep) sur la marge (SRCGather) : au-delà, le sol reçoit l'éclairage
analytique du ciel (irradiance SH de la HDRI + hémisphère), sans motif et sans bord visible. Près des pièces, la GI
(ombres de contact, rebonds colorés) reste. Viewport3D n'est pas concerné (bornes désactivées par défaut).
Comparaison dans le même binaire : `--no-gi-bounds`.

| Cadrage | avant | après |
|---|---|---|
| Scène d'exemple, cadrage de Wzx (tangage 14°, zoom 0,45) | 17,6 ms | 15,3 ms |
| Scène d'exemple, tangage 8° | 11,9 ms | 10,6 ms |
| 10 000 briques, tangage 10° | — | 11,3 ms |

Captures : 38a / 38a2 / 38a5 avant (38a5 = cadrage de Wzx), 38b (bas), 38c (haut), 38d / 38e (10k briques), 38g
(avant / après côte à côte), 38h (Viewport3D, boîte de Cornell, inchangée). Path tracing (38a3) et HQ (38a4) n'ont pas
le problème (pas de sondes).
