# BrickIt Builder : portage natif PC (WZX ENGINE, DX12)

Demande de Wzx (01/11/2026) : porter le builder web BrickIt en natif **PC uniquement** dans WZX ENGINE.
Référence fonctionnelle : `docs/brickit/web/BrickIt_Builder.html` (build web v26, une seule page, three.js 0.160),
en ligne sur https://claude.ai/artifact/EJ41ZbKiB62Fny3ncNKvyg. iPad (Metal) plus tard, pas dans ce chantier.

## Priorités, dans l'ordre

1. **Performances.** Des milliers de briques dans le viewport, fluide.
   Cible : 10 000 briques à 60 i/s en temps réel, 50 000 encore utilisables.
   Le path tracing progressif doit rester interactif, avec le débruiteur.
2. **Rendu path tracing beau et photographique.** Ce n'est pas une affaire de nombre de rebonds, c'est le résultat graphique :
   - lumineux, « photo produit » : HDRI de studio, exposition haute, tonemapping ACES ou AgX ;
   - matériau plastique ABS crédible : vernis léger, roughness d'environ 0,2 à 0,3, SSS discret sur les couleurs claires ;
     transparents en verre teinté, couleurs métal en métal ;
   - **traces de doigts** dans le matériau : maps du dossier `FingerPrint` dans `Ressources` sur le PC (trouver le chemin exact).
     Elles modulent la roughness, et un peu le spéculaire. Projection triplanaire en espace objet à l'échelle de la brique,
     avec un décalage aléatoire par instance pour éviter la répétition. L'effet doit rester subtil et visible surtout dans les reflets ;
   - **beaux bevels** sur toutes les arêtes (rayon d'environ 0,02 à 0,035 cm), plus la **rainure** entre deux briques qui se touchent :
     Wzx y tient, comme dans la version web. Le moteur a déjà un bevel shader dans le path tracer, à réutiliser ou améliorer.
3. **UI compacte pour un moniteur 4K.** Thème ImGui existant, dense : petites marges, icônes, pas de gros boutons tactiles.
   Tester l'échelle à 150 % et à 100 %.
4. **Captures d'écran de suivi.** Wzx n'est pas devant le PC. Il faut une capture à chaque jalon,
   et au moins toutes les heures de travail. Les mettre dans `docs/brickit/suivi/NN_description.png`
   (avec les temps GPU et CPU et le nombre de briques dans le nom ou dans `docs/brickit/suivi/JOURNAL.md`),
   puis commit et push sur la branche `brickit-native`. Le fil BrickIt les récupère et les montre à Wzx.

## Données

- **Pièces** : tout est LDraw, avec une conversion minimale : 1 LDU = 0,04 cm, rotation de 180° autour de X,
  origine au coin minimum, et l'origine LDraw gardée dans `info.ldrawOrigin`. Les noms de pièces sont les références LDraw.
  - Base de 50 pièces : JSON `window.__VP3D_LIB` et binaire base64 `window.__VP3D_BIN` dans le HTML.
    Le binaire contient, par pièce, des sommets float32 ×6 (position et normale) puis des index uint32 :
    voir `loadLibrary()`.
  - Technic : 730 pièces dans `docs/brickit/web/technic/`.
    - `index.json` : liste des pièces, sous-familles, numéro de pack.
    - `g{n}.wasm` : en réalité du binaire deflate, pas du wasm. Format : `[u32 longueur json][json metas]`,
      puis pour chaque pièce : positions u16 ×3 quantifiées (`mn + pos*sc`, alignées sur 4 octets),
      normales i8 ×4 (/127), index u16 ou u32 (`q.big`). Voir `ensurePack()` dans le HTML.
    - `t_{sous-famille}.json` : miniatures webp en data URL.
  - Méta par pièce : `voxels` (positions des connecteurs), `connectorTypes`
    (stud, antistud, pin, pinhole, axle, axlehole), `heightPlates`, `topY`, `grab`, `columns`, etc.
  - Outils de conversion utilisés : `ldraw2brickit.mjs`, `build_technic.mjs`, `pack_technic.mjs`.
    Ils lisent la bibliothèque officielle LDraw et les connecteurs de la LDCad shadow library.
    En natif, le mieux est de convertir une fois vers un format binaire maison (un fichier par pack),
    ou de refaire la conversion depuis LDraw en C++ si c'est plus simple.
- **Palette** : codes LDConfig (champ `palette` de `__VP3D_LIB`).
- **Scène** : format `.v3d.json` `vp3d-scene` v1 compatible avec le web. Chaque brique a `lib:"brickit"`, `part`, `color`, `pos`, `rot` (quaternion), `grabVoxel`.

## Architecture conseillée (performances)

- **Une géométrie par type de pièce**, chargée une seule fois sur le GPU, et **une instance par brique** :
  transform, couleur, id et graine pour les traces de doigts, dans un StructuredBuffer.
- **Raster** : instancing plus `ExecuteIndirect`, avec un culling frustum (et si possible occlusion Hi-Z) en compute.
  Pas d'appel de dessin par brique et pas d'objet lourd par brique côté CPU.
- **LOD** : les tenons et les tubes coûtent l'essentiel des triangles. Prévoir un LOD avec des tenons bas poly,
  puis sans détail de dessous au loin.
- **DXR** : un BLAS par type de pièce et un TLAS avec une instance par brique (InstanceID vers la couleur et le matériau).
  Reconstruire ou mettre à jour le TLAS seulement quand la scène change.
  Le picking peut se faire par RayQuery.
- **Snap et collisions** : hachage spatial des connecteurs (voxels typés) pour des requêtes de voisinage en O(1).
  Portage de `computeTarget` et de la logique de snap typé du web.
- **Banc d'essai** : générateur de scènes de 1 000, 5 000, 10 000 et 50 000 briques, avec les temps GPU et CPU affichés
  et notés dans les captures.

## Logique à porter (noms des fonctions dans le HTML)

1. Bibliothèque : `loadLibrary`, `loadTechnicIndex`, `ensurePack`.
   Navigation : familles LDraw (`LDRAW_FAMILIES`, `CAT_FR`), sous-familles Technic (`TECH_SUBS`),
   récents, favoris, recherche, miniatures sur deux rangées en bas, plus une taille de miniature réglable.
2. Placement : brique en main (`spawn`, `beginHold`, `computeTarget`, `drop`), snap typé
   (tenon sur anti-tenon, pin dans un trou rond, axe dans un trou en croix ou rond, jamais un axe sur un tenon),
   rotation et bascule, grille, pivot automatique, sécurité anti-pénétration à 50 %.
3. Édition : sélection avec un contour fin blanc à 45 % plus un léger remplissage, mode focus (le reste assombri à 50 %),
   dupliquer en empilant au-dessus (`stackedCopy`), annuler et rétablir, suppression, couleurs (panneau à droite),
   sauvegarde et chargement `.v3d.json`.
4. Mode Mécanique : `addLdrawMech` et `ldrawMechOf` (connecteurs vers trous, axes et engrenages,
   avec plusieurs couronnes par pièce), `mechBuild` (corps, liaisons, engrènements), `mechTree`
   (bâti = corps à tenons ; liaison coaxiale réaccrochée au grand-parent), `mechSolve`, commandes moteurs et vérins.
5. Export Maya (`maya_export.js` et `maya_template.py`) et module Manuel PDF : plus tard.

## Jalons (une capture à chaque fois)

- **M0** : lire ce document, le HTML et le moteur. Choisir la base de départ.
  La branche `basic-function` est le viewer le plus complet : MSAA, glow de sélection, multi-vues, snapping.
  Faire un plan court dans `docs/brickit/PLAN.md`. Capture 00 de l'état de départ.
- **M1** : import des pièces (base et Technic) et affichage d'une grille de toutes les pièces. Capture 01.
- **M2** : instancing et banc d'essai de 1 000 à 50 000 briques, en raster et en path tracing, avec les temps. Captures 02.
- **M3** : look (plastique, traces de doigts, bevel et rainures, HDRI lumineux, réglages par défaut).
  Captures beauté 03 en gros plan et en plan large.
- **M4** : interaction de construction, UI 4K compacte, bibliothèque, couleurs, snap, annuler, sauvegarde. Captures 04.
- **M5** : mode Mécanique. Captures 05.

Règles : ne pas casser `main`, tout faire sur `brickit-native`, et faire des commits fréquents.
Avant chaque push, compiler (`build.bat`) et lancer une capture (`--shot`).
