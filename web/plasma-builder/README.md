# PlasmaOrk Builder (web)

Builder modulaire de figurines Ork dans le navigateur : bibliothèque de pièces, rig, poses, viewport réaliste et export STL pour l'impression.
Three.js 0.180 et three-gpu-pathtracer 0.0.24 (chargés depuis jsDelivr par l'importmap de `index.html`), sans étape de build.

## Lancer

Les modules ES ne se chargent pas en `file://` : servir le dossier, par exemple

```
cd web/plasma-builder
python -m http.server 8080      ->  http://localhost:8080
```

## Parcours

Cinq étapes en haut du viewport : **Bibliothèque › Assemblage › Rig › Pose › Rendu**.

- **Bibliothèque** (rail de gauche) : une catégorie par emplacement (tête, corps, bras G/D, arme, jambes, épaulières, sac à dos, socle) et des presets de figurines complètes. Le bouton `+` ou un glisser-déposer sur le viewport importe des STL dans la catégorie ouverte.
- **Assemblage** : clic sur une pièce, un socket ou une vignette du bandeau du bas pour la sélectionner. Outils Q / G / R / S (sélection, déplacer, tourner, échelle), panneau **Personnaliser** › Transformation, miroir X/Y/Z.
- **Rig** : squelette de 20 os. Chaque pièce est **rigide** et suit un seul os (pas de skinning), ce qui garde des pièces imprimables séparément. Les curseurs de l'onglet Rig tournent les os de l'emplacement sélectionné.
- **Pose** : six poses (Garde, Tir, Visée, Charge, Hurlement, Waaagh !) avec transition, vignettes rendues de la figurine en cours.
- **Rendu** : le rendu des dernières recherches du renderer Viewport3D (WZX-ENGINE `src/vp3d`), onglet **Rendu** du panneau de droite :
  - transformées ACES (fit de Hill), AgX, AgX Punchy, Standard, exposition en EV et vignettage, exactement comme `ToneCurves.hlsli` / `Tonemap.hlsl`, contour de sélection jaune ;
  - temps réel comme `Raster.hlsl` : MSAA 4x, ombre en 12 prises de Poisson, lobe spéculaire élargi par la taille des lumières de surface, terme de cavité sur l'ambiant ;
  - éclairage du Material Studio : environnements Studio / Coucher de soleil / Entrepôt avec lumières key / fill / rim, ciel procédural, ou HDRI `.hdr` / `.exr` (intensité, rotation, visible en fond) ; le soleil d'un ciel ou d'un HDRI devient une lumière ombrée en temps réel ;
  - scène **Modèle test : Ork Boy 32 mm** (sculpt extrait de BOY_32mm.mb par `tools/mb_mesh`), pour juger le rendu sur un vrai modèle ; texture basecolor chargée depuis `assets/test/` ou via le bouton de l'onglet ;
  - qualité **Temps réel**, **Haute qualité** (résolution et ombres plus fines) ou, dans l'app Windows seulement (`desktop/`), **Path tracing** progressif : rebonds, filtre glossy, débruitage mélangé à l'image brute au fil des échantillons, profondeur de champ macro à l'échelle de la figurine ;
  - affichages Résine / Peint / Filaire, rotation de l'éclairage, vues caméra Face / Trois-quarts / Dos / Profil.

**Manipulation** (reprise du BrickIt Builder de VIEWPORT3D) : un clic sur une pièce de la bibliothèque la prend en main, elle vole jusqu'à son socket (fantôme, socket allumé, anneau et ligne de chute) puis s'y pose avec un petit rebond. Glisser une vignette dans le viewport affiche les sockets de réception compatibles (anneaux) et vise le plus proche du pointeur : une arme va en main droite ou gauche, à la ceinture (gauche, droite) ou dans le dos, un bras à gauche ou à droite ; lâcher pose, Échap annule.

**Kits & options** : accessoires (rokkits, bombe, mine), plaques d'armure, sacoches, ornements et un curseur d'usure.

## Rig Lab (`rig.html`)

Le rig Maya de l'Ork Boy (`BOY_32mm.mb`) dans le navigateur, pour poser la figurine avec de vraies déformations :
60 joints QuickRig, skinCluster du basemesh low, high-poly avec poids transférés. Même viewport et même rendu que le builder.

- **Sélection** : clic sur la figurine (l'os dont l'influence domine sous le pointeur, sa zone s'allume), sur une sphère d'os ou dans la liste. Flèches haut / bas : os parent / enfant, Échap : rien.
- **Rotation (R ou E)** : gizmo de rotation locale sur l'os, curseurs X / Y / Z en degrés relatifs au repos.
- **Déplacer en IK (W)** : on déplace une main ou un pied, bras / jambe suivent (IK deux os : le coude ou le genou garde son côté, la main garde son orientation). En IK, le bassin se déplace ; **Pieds ancrés** garde les pieds au sol pendant qu'on bouge ou tourne le bassin.
- **Symétrie auto** recopie chaque geste sur l'autre côté ; **Os → opposé** (M) une seule fois ; **G ⇄ D**, **G → D**, **D → G** pour toute la pose.
- **Mains** : un curseur par main ferme les doigts (de la main ouverte au poing).
- **Poses de départ** (Repos, Garde, Charge, Waaagh !, Tir) et **Mes poses** enregistrées dans le navigateur, export / import `.json`.
- **Affichage** : High-poly ou Low (le maillage skinné de Maya), os en rayons X (B), poids de l'os sélectionné en carte de chaleur (H).
- **Exporter STL** : la figurine posée (high-poly réduit + tête, dents, yeux) en millimètres, échelle 32 / 54 / 75 mm à la hauteur des yeux.

Le modèle est `assets/ork_boy.glb`, produit par `tools/mb2glb/mb2glb.py` à partir du `.mb` (lecteur du format binaire Maya en Python, sans Maya) :

```
pip install numpy scipy fast-simplification
python tools/mb2glb/mb2glb.py BOY_32mm.mb web/plasma-builder/assets/ork_boy.glb --body-tris 220000 --head-tris 120000
```

Le high-poly n'est pas riggé dans Maya : chaque sommet reçoit les poids du point le plus proche sur la surface du low (interpolés entre les trois sommets du triangle), la tête high est rigide sur le joint Head comme la tête low (contrainte sur HEAD_ROT dans le rig). Trois.js limite à 4 influences par sommet : les 4 plus fortes sont gardées et renormalisées (le skin Maya en a jusqu'à 10).

## Export

- **Exporter STL** : la figurine fusionnée dans la pose courante, en millimètres, Z vers le haut, posée sur le plateau.
- Menu ▾ : pièces séparées (un STL par emplacement dans un ZIP, plots d'assemblage optionnels) ou projet `.plasmaork.json`.
- Échelle 32 / 54 / 75 mm : c'est la hauteur des yeux de l'Ork (convention des échelles de figurines), la hauteur totale est affichée dans l'onglet Impression.

Le projet est sauvegardé dans le navigateur (localStorage), avec annuler / rétablir (Ctrl+Z / Ctrl+Y). Les STL importés ne sont pas conservés au rechargement.

## Code

- `js/main.js` : état, historique, interface et événements
- `js/viewport.js` : viewport (passes HDR, masque de sélection, aides, caméra, gizmo) et moteur de vignettes hors écran
- `js/render.js` : portage du renderer Viewport3D (shading raster, passe Tonemap, environnements, studio, HDRI)
- `js/pathtrace.js` : mode path tracing (chargé à la demande)
- `js/hold.js` : pièce en main, visor, sockets de réception et rebonds (BrickIt)
- `js/testmodel.js` : modèle test du rendu
- `desktop/` : app Windows (Electron, hors ligne, path tracing), voir son README
- `js/figure.js` : la figurine assemblée (pièces accrochées aux os, kits, options, groupes d'export)
- `js/rig.js` : os, poses, os réglables par emplacement
- `js/parts.js` : catalogue et pièces de départ **procédurales** (à remplacer par les vrais STL sculptés)
- `js/io.js` : lecture STL, export STL / ZIP / JSON
- `rig.html`, `js/riglab/model.js` (ork riggé : pose, IK, miroir, doigts, picking, géométrie posée), `js/riglab/app.js` (interface du Rig Lab), `css/riglab.css`

Pour ajouter une pièce sculptée au catalogue : l'exporter centrée sur son articulation (l'origine = le pivot de l'os) et l'ajouter comme variante dans `parts.js`, ou l'importer depuis l'interface.
