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

La base est l'ork riggé de Maya (`assets/ork_boy.glb`, le même que le Rig Lab) ; on y accroche les briques de la
bibliothèque, importées une par une avec l'**Importeur** (`import.html`). Quatre étapes en haut du viewport :
**Bibliothèque › Assemblage › Pose › Rendu**.

- **Bibliothèque** (rail de gauche) : Têtes, Armes, Dos, Épaulières, Torse, Ceinture, Socles. Le `+` ouvre l'importeur.
  Une brique posée sur la tête masque la tête d'origine de l'ork.
- **Sockets de l'ork** : tête, mains droite et gauche, dos, torse, épaules, ceinture droite et gauche, socle. Chacun suit
  un joint du rig (`js/sockets.js`), placé au chargement à partir des joints et de la surface du low. Une brique peut
  offrir ses propres sockets (un dos qui porte un étendard) : ils apparaissent dès qu'elle est posée.
- **Assemblage** : clic sur une brique (viewport, pastille ou bandeau du bas) pour la sélectionner. Outils Q / W / E / S
  (sélection, déplacer, tourner, échelle) ou panneau **Personnaliser** › Transfo : le réglage est un décalage par rapport
  au socket (en cm), gardé quand on change de pose. Miroir X / Y / Z.
- **Pose** : les poses de départ du Rig Lab et les poses enregistrées dans le Rig Lab (« Mes poses »), fermeture des
  mains dans le panneau **Ork**. Les briques suivent leur joint.
- **Low / High** (panneau Ork) : basemesh low et version low des briques, ou high-poly de l'ork et version high des
  briques. L'export STL prend toujours le high.
- **Rendu** : le rendu des dernières recherches du renderer Viewport3D (WZX-ENGINE `src/vp3d`), onglet **Rendu** du panneau de droite :
  - transformées ACES (fit de Hill), AgX, AgX Punchy, Standard, exposition en EV et vignettage, exactement comme `ToneCurves.hlsli` / `Tonemap.hlsl`, contour de sélection jaune ;
  - temps réel comme `Raster.hlsl` : MSAA 4x, ombre en 12 prises de Poisson, lobe spéculaire élargi par la taille des lumières de surface, terme de cavité sur l'ambiant ;
  - éclairage du Material Studio : environnements Studio / Coucher de soleil / Entrepôt avec lumières key / fill / rim, ciel procédural, ou HDRI `.hdr` / `.exr` (intensité, rotation, visible en fond) ; le soleil d'un ciel ou d'un HDRI devient une lumière ombrée en temps réel ;
  - scène **Modèle test : Grot Bomber**, texturé, pour juger le rendu ; texture basecolor chargée depuis `assets/test/` ou via le bouton de l'onglet ;
  - qualité **Temps réel**, **Haute qualité** (résolution et ombres plus fines) ou, dans l'app Windows seulement (`desktop/`), **Path tracing** progressif : rebonds, filtre glossy, débruitage mélangé à l'image brute au fil des échantillons, profondeur de champ macro à l'échelle de la figurine ;
  - qualité **HQ BrickIt** : imitation WebGL du mode HQ de BrickIt natif (WZX-ENGINE, branche `brickit-native`, `js/hqgi.js`). Le natif trace des rayons (DXR), WebGL non : on garde les mêmes idées en raster. **GI douce** : une grille de sondes bornée à la figurine capture de petits cubes de la scène éclairée (sol, ombres, environnement) projetés en harmoniques sphériques sur le GPU ; le raster lit l'éclairement des 8 sondes voisines à la place de celui du ciel (ciel occulté dans les creux, lumière qui rebondit, plusieurs rebonds au fil des passes). Sondes progressives, convergées puis au repos (coût nul), figées pendant qu'une pièce bouge (réglage), sondes enfermées dans le maillage écartées. **Occlusion HDRI** des reflets par le rapport sondes / ciel. **SSAO** demi-résolution avec flou 4 × 4, multiplié avant le tonemap. Réglages : intensité GI, figer la GI, force et rayon du SSAO ;
  - affichages Résine / Peint / Filaire, rotation de l'éclairage, vues caméra Face / Trois-quarts / Dos / Profil.

**Manipulation** (reprise du BrickIt Builder de VIEWPORT3D) : un clic sur une brique de la bibliothèque la prend en main, elle vole jusqu'à son socket (le socket sélectionné s'il est compatible, sinon son socket d'origine) puis s'y pose avec un petit rebond. Glisser une vignette dans le viewport affiche les sockets compatibles (anneaux) et vise le plus proche du pointeur : une arme va en main droite ou gauche ; lâcher pose, Échap annule.

## Importeur (`import.html`)

On dépose les fichiers d'une brique dans l'aperçu : modèle `.fbx`, `.obj`, `.glb` / `.gltf` (low et high dans le même
fichier ou en deux fichiers), texture `.jpg` / `.png` (un nom avec « mask » est rangé comme masque de couleur de
faction, réservé pour plus tard), sockets `.json` du script Maya `tools/maya/plasma_export_brick.py`.

- **Low / high** : un objet ou un fichier dont le nom contient high, hd, decim… va en high, le reste en low (modifiable).
  Deux fichiers sans indice : le plus lourd est le high. Pas de high : le low sert aux deux.
- **Locators** `SOCKET_<type>_<nom>` (sockets offerts par la brique) et `PLUG_<type>` (le point qui vient sur le
  socket). Types : head, grip, back, shoulder, chest, belt, base, any. Lus dans le FBX ou le glTF, ou dans le `.json`
  du script Maya pour l'OBJ (qui ne sait pas porter de locators). On peut aussi en ajouter au clic sur la brique et les
  régler au gizmo (W / E).
- **Placement** : *en place* (la brique a été modélisée sur l'ork dans la scène de `BOY_32mm.mb` : elle retombe là où
  elle a été modélisée, deviné automatiquement avec la catégorie), *au plug*, ou *à l'origine* de la scène.
- **Unités** : cm par défaut (Maya), m pour le glTF, lues dans le FBX ; option Z vers le haut.
- **Contrôles** : triangles low / high, UV, texture, taille à l'échelle 32 mm, distance au socket, écart entre le high
  et le low (même scène, même position), maillage fermé pour l'impression.
- **Bibliothèque** : enregistrée dans le navigateur (IndexedDB), avec vignette. Menu **Bibliothèque** : export / import
  d'un `.zip` pour la sauvegarder ou la passer du navigateur à l'app Windows (chacun a son propre stockage).

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
- Menu ▾ : pièces séparées (l'ork et chaque brique, un STL chacun dans un ZIP) ou projet `.plasmaork.json`.
- Échelle 32 / 54 / 75 mm : c'est la hauteur des yeux de l'Ork (convention des échelles de figurines), la hauteur totale est affichée dans l'onglet Impression.

Le projet est sauvegardé dans le navigateur (localStorage), avec annuler / rétablir (Ctrl+Z / Ctrl+Y).

## Code

- `js/main.js` : le builder (état, historique, interface, briques accrochées aux sockets, export)
- `js/sockets.js` : sockets de l'ork, noms des locators
- `js/bricks.js` : format des briques, stockage IndexedDB, construction des objets 3D, export / import de la bibliothèque
- `import.html`, `js/importer.js`, `css/importer.css` : l'importeur
- `js/viewport.js` : viewport (passes HDR, masque de sélection, aides, caméra, gizmo) et moteur de vignettes hors écran
- `js/render.js` : portage du renderer Viewport3D (shading raster, passe Tonemap, environnements, studio, HDRI)
- `js/pathtrace.js` : mode path tracing (chargé à la demande)
- `js/hold.js` : pièce en main, visor, sockets de réception et rebonds (BrickIt)
- `js/testmodel.js` : modèle test du rendu (Grot Bomber)
- `js/io.js` : export STL / ZIP / JSON
- `rig.html`, `js/riglab/model.js` (ork riggé : pose, IK, miroir, doigts, picking, géométrie posée), `js/riglab/poses.js` (poses de départ partagées avec le builder), `js/riglab/app.js` (interface du Rig Lab), `css/riglab.css`
- `desktop/` : app Windows (Electron, hors ligne, path tracing), voir son README
- `tools/maya/plasma_export_brick.py` (racine du dépôt) : export d'une brique depuis Maya (OBJ low / high + sockets)
