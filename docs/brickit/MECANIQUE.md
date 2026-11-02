# Mécanique de BrickIt — algorithme (natif, à reporter à l'identique dans le web)

Code : `src/brickit/Mech.cpp` (construction et solveur), tests : `src/brickit/BrickMechTests.cpp` (`BrickIt.exe --mechtests`,
résultats dans `mech_tests.txt`, copie dans `docs/brickit/tests/mech_tests_resultats.txt`). Diagnostic d'une scène :
`BrickIt.exe --open scene.v3d.json --mech` écrit `mech_diag.txt` (corps, liaisons, engrenages, rapports).

## 1. Connecteurs d'une pièce (`Mech::meta`)

À partir des voxels typés de la pièce (tenon, anti-tenon, pin, trou de pin, axe, trou d'axe) :

- **Tenons / anti-tenons** : tels quels (`top`, `bottom`).
- **Trous** : d'abord, deux points d'un même type distants de 0,3 à 1,7 cm sur un seul axe et posés tous deux sur des
  faces opposées de la boîte de la pièce forment un trou le long de cet axe (centre au milieu, découpé par 0,8 cm) ; les
  points entre les deux lui appartiennent. **Correction** : les poutres (ex. 32524) donnent un point sur chaque face par
  trou, 0,8 cm plus loin que le trou voisin : l'ancienne règle (voisins à moins de 0,5 cm) les laissait isolés, avec
  l'axe Z par défaut au lieu de Y. Ensuite, comme avant, les points alignés (voisins à moins de 0,5 cm sur un seul axe)
  forment une ligne : un connecteur le long de la ligne, les trous découpés par 0,8 cm.
  **Correction** : sur une poutre Technic (titre `Technic Beam` ou `Liftarm`, sans tenon), le convertisseur donne
  certains trous comme des anti-tenons posés sur une face, au centre du trou (poutres coudées 32526 / 32140 / 32271 :
  aucun trou lu ; trou d'extrémité de la poutre 11 32525). Chaque anti-tenon sur une face de moins de 1,7 cm devient
  aussi un trou rond à travers la pièce (sans doublon), puis les trous sont rangés le long de la pièce.
- **Mâles** (pins, axes) : lignes de voxels de pin / d'axe. **Correction** : une ligne « mâle » qui traverse au moins
  deux trous de la même pièce en travers (le milieu des trous d'une poutre, typé pin) est retirée.
- **Engrenages** : titre `Gear N Tooth` (toutes les occurrences : un différentiel a 16 et 24), autour du trou le plus
  proche du centre ; `kind` 1 (conique / couronne) si le titre contient `Bevel` ou `Crown`. **Vis sans fin** : titre
  `Worm Gear` (hors Duplo) : 1 filet, rayon = sommet le plus éloigné de l'axe. **Plateaux dentés** : titre
  `Turntable N Tooth` (moitié du dessus) : engrenage de N dents sur l'axe du plateau, `kind` 1.
- **Plateaux tournants** : moitiés `Bottom / Base` et `Top` de la même famille (titre sans ces mots), axe vertical par
  le centre, étendue en hauteur.

## 2. Corps rigides (union-find sur les pièces)

1. Plateaux tournants : base et dessus de la même famille, coaxiaux, étendues qui se touchent ou se recouvrent (0,1 cm)
   = pivot (et les deux moitiés ne sont pas soudées par leurs tenons).
2. **Tenons** : un tenon et un anti-tenon de deux pièces à moins de 0,06 cm (recherche dans les 27 cellules de 0,1 cm
   autour) = soudure, quelle que soit l'orientation (empilement, tenons de côté, à l'envers). **Correction** :
   l'ancienne clé arrondie au 1/50 de cm ratait deux points voisins tombés dans deux cellules.
3. **Axes et pins** : un mâle et un trou parallèles (à 2e-3 près), le trou sur la ligne du mâle (0,08 cm) et à moins de
   `demi-longueur − 0,15` du centre du mâle. Axe dans un trou en croix = soudure ; axe dans un trou rond = pivot. Pin :
   soudé à la première pièce traversée, pivot avec les autres.
4. Deux pivots non coaxiaux entre les deux mêmes corps les soudent (jusqu'à stabilité).

## 3. Arbre cinématique

Pour chaque groupe connexe de corps : la racine est le corps structurel (tenons) le plus bas, puis le plus gros. Arbre
couvrant des pivots (ceux des corps à engrenages d'abord) ; les pivots restants ferment des boucles (2 points par
boucle, 6 résidus). Chaque pivot de l'arbre a une variable `q` (angle).

## 4. Engrènements (nouveau : contraintes du solveur, quels que soient les supports)

Détection (rayon primitif r = dents × 0,05 cm) :

- **Axes parallèles** : centres dans le même plan (écart axial < 0,3 cm), entraxe = rA + rB à 0,105 cm près.
- **Axes à 90° qui se coupent** (au moins un conique / couronne) : points les plus proches des deux axes à moins de
  0,36 cm (**correction** : 0,3 refusait deux engrenages décalés d'une plaque, 0,32 cm, cas courant quand une des deux
  briques est posée sur une plaque ; les dents des coniques font 0,4 à 0,8 cm de large et restent en prise), distance de chaque centre au point de croisement P = rayon de l'autre (à 0,35 cm près).
- **Vis sans fin** : axes à 90°, distance entre les axes = r_roue + r_vis (0,2 cm), la vis dans le plan de la roue
  (0,35 cm).

Contrainte (une ligne de résidu par engrènement) : pour chaque engrenage, son angle autour de son axe, mesuré **depuis
la direction de l'autre engrenage** (projetée dans son plan), à partir de la direction d'une dent ; phase =
`tA·αA + σ·tB·αB`, constante (valeur au repos) **modulo 2π** (une dent : les dents se répètent). σ = −signe(vA·vB) avec
vA = uA × (K − cA), vB = uB × (K − cB) au point de contact K (vitesse de chaque engrenage au contact pour une rotation
positive) : +1 pour deux engrenages droits extérieurs (sens opposés), −1 quand les vitesses vont dans le même sens. Vis :
σ = +1 (tA = 1 filet). Le résidu est `(phase − c0 ramené dans [−π, π]) × 0,5`.

Mesurer les angles depuis la ligne des centres rend la contrainte juste quand les supports bougent : un satellite sur un
bras qui tourne autour d'un soleil fixe donne bien bras = satellite × tS / (tS + tP)… vu depuis le bras (test 5).
Plus de règle « axes portés par la même pièce » : deux supports séparés, empilés, côte à côte, mobiles, un axe dans
plusieurs supports, tout passe par la même contrainte.

## 4 bis. Glissières (crémaillère, vérins)

Une liaison peut être un **pivot** (angle) ou une **glissière** (avance en cm le long d'un axe, avec une course
lo..hi). Dans l'arbre, la pose d'un enfant de glissière est la translation `u × q` ; la course est une ligne de résidu
de plus (`q − hi` au-delà, `lo − q` en deçà, 0 sinon) : au bout de la course, le solveur refuse la pose (butée).

- **Crémaillère** (titre `Gear Rack`, pas la crémaillère de direction) : rangée de dents le long du plus grand côté de
  la pièce. Engrène avec un pignon dont l'axe est en travers de la rangée, son centre au-dessus de la rangée (dans le
  plan du pignon, à 0,4 cm près), à une distance de la ligne médiane de la crémaillère entre r − 0,1 et r + 0,6 cm. Si
  rien ne tient la crémaillère, elle glisse le long de sa rangée, guidée par la pièce qui porte l'axe du pignon, course
  = tant que le pignon reste sur les dents (demi-longueur − 0,4 cm). Contrainte : `tP·αP + σ·(2π / p)·s` constante modulo
  2π, p = π × 0,1 cm (pas des dents), s = position du pignon le long de la rangée, σ = signe(vP·u). Un tour de pignon fait
  avancer la crémaillère de 2π r (test 12 : 0,8 cm par radian pour un 16).
- **Vérins** : amortisseur `Shock Absorber … Cylinder` + `Piston Rod`, actionneur `Linear Actuator … Axle Actuator` +
  `Piston` : axe = plus grand côté de chaque moitié, **passant par le trou de pin d'extrémité** (trou en travers de
  l'axe le plus éloigné du centre ; correction : le centre de la boîte est décalé par une patte, 0,45 cm sur 2909, et un
  vérin monté par ses trous n'était jamais vu coaxial avec sa tige), les deux coaxiales (0,15 cm), la tige engagée dans
  le corps. Sens de l'axe : du corps vers la tige, une commande positive **allonge** le vérin.
  Glissière entre leurs deux corps ; **course estimée sur le maillage** (le convertisseur LDraw ne donne pas les
  glissières LDCad) : un tiers de la plus courte des deux moitiés de chaque côté, de 0,4 à 2,4 cm. Une commande
  « Vérin n » par vérin (cm, curseur sur la course).
- **Vérin qui ferme une boucle** (cas principal : pelleteuse, tractopelle — vérin entre le châssis et un bras qui
  pivote, ses deux bouts montés par des pins) : les glissières passent **en premier** dans l'arbre couvrant, donc le
  vérin est toujours une liaison de l'arbre (une variable pilotée) et c'est un **pivot** (un pin d'extrémité) qui ferme
  la boucle. La commande impose la longueur ; le bras suit par la contrainte de fermeture de boucle (6 résidus, LM). La
  course est une ligne de résidu : en bout de course la pose est refusée (butée), la commande s'arrête là. Aucun
  avertissement. Plusieurs vérins = plusieurs commandes indépendantes (flèche + balancier) ; un vérin + biellette
  (quadrilatère) = deux boucles pour un seul DDL. Près d'un point mort (vérin aligné avec le bras), la boucle se bloque :
  c'est la limite mécanique réelle, la pose est refusée comme une butée.

## 5. Commandes et rapports

- **Train** = engrenages reliés par des engrènements **ou portés par le même corps** (même axe). Une commande « Moteur »
  par train, sur le pivot de son plus petit engrenage mobile (une vis sans fin d'abord : 1 filet). Si tous les
  engrenages du train sont sur des pièces fixes : avertissement « Engrenages fixes : aucun ne peut tourner ».
- Un axe porteur d'engrenages sans engrènement, libre dans une pièce fixe : commande « Axe ».
- **Rapports** (affichés « 1:k ») : mesurés sur le solveur, en tournant la commande seule de 0,02 rad ; si le solveur ne
  peut pas la suivre : avertissement « Train d'engrenages bloqué (rapports incompatibles) ».

## 6. Solveur (Levenberg-Marquardt)

Variables : pivots libres (et les commandes pendant un glisser). Résidus : boucles (6 par boucle) + engrènements (1
par engrènement) + glisser (3, poids 0,35) + rappel vers la pose précédente (0,01 × écart, une ligne par variable).
Jacobien par différences finies de pas **1e-4** (correction : les poses sont en flottants simple précision, un pas de
1e-6 ne mesurait que leur arrondi et le solveur s'arrêtait avant d'avoir convergé). 30 itérations au plus, pose
refusée (butée) si un résidu de boucle ou d'engrènement dépasse 0,004.

## 7. Limites connues

- Course des vérins : estimée (pas de données de glissière dans la conversion LDraw).
- Actionneur linéaire 92695 : la conversion ne donne aucun trou au corps (seulement un axe) ; il ne se monte pas par
  ses bouts dans une boucle (l'amortisseur 2909 + 2910, oui).
- Modèle tout Technic (sans tenon) : le sol est le corps le plus bas ; une pièce qui pend sous le châssis devient le sol
  (même règle que le web).
- Couronnes intérieures : pas de pièce Technic de ce type dans la bibliothèque ; le signe σ = −1 les couvrirait.
- Engrenages « Large Tooth » (module différent) : non détectés.
- Les satellites internes d'un différentiel ne sont pas testés (le boîtier l'est).

## 8. Tests (`--mechtests`) — 31 / 31

1 deux engrenages sur une poutre (8 / 24) · 2 deux supports séparés (16 / 24) · 3 supports empilés (8 / 16 en
diagonale) · 4 côte à côte, deux briques non reliées · 5 train planétaire (soleil fixe, satellite sur un bras) · 6 train
de 4 engrenages de 16 · 7 rapports 8 / 40 / 24 · 8 coniques à 90° · 9 différentiel 6573 entraîné par un 8 · 10 plateau
tournant 28 entraîné par un conique 12 · 11 vis sans fin et 24 · 12 crémaillère et pignon (16, un quart de tour : 0,8 cm / rad) · 13 axe
traversant trois supports · 14 montage 1 tourné de 90° puis 180° · 15 deux poutres reliées par deux pins (un seul
corps) · 16 scène de Wzx `docs/brickit/tests/meca_16_24.json` · 17 vérin amortisseur 2909 + 2910 · 18 actionneur linéaire 92695 + 92696 · **19** bras levé par un vérin
(boucle châssis – vérin – bras) · **19b** butées (bout de course atteint, 0,3 cm au-delà refusé, pose conservée) · **20**
flèche + balancier, deux vérins : vérin 1 piloté (la flèche bouge, le balancier garde son angle sur elle) · **20b**
vérin 2 piloté (le balancier bouge, la flèche non) · **21** godet sur quadrilatère (biellette + bielle, deux boucles)
piloté par un vérin · **22** construction sur la grille comme dans l'éditeur (rotations de 90° : châssis en L 32526 +
poutre 32525, bras horizontal, vérin vertical ; scène `docs/brickit/tests/meca_pelleteuse.json`) · **22b** sens : un
vérin allongé de 0,8 cm lève le bout du bras (4,8 → 8,8 cm) · **23 à 28** cas 2 de Wzx (deux briques Technic 1 × 2 à trou
3700 côte à côte, non reliées, un axe 3706 dans chaque trou, un engrenage sur chaque axe, engrènement entre les deux
briques ; scène `docs/brickit/tests/meca_cas2.json`, capture 35) : doubles coniques 12 / 20 (32270 / 32269), 16 / 16
(4019), conique simple 20 (32198) / double conique 12, sur une plaque 1 × 4 (un seul corps fixe), montage tourné de 90°,
et les deux briques à 90° (doubles coniques 20 / 12, axes qui se croisent). Tous verts sans changement du code : titres
« Double Bevel » / « Bevel » lus (type 1), entraxe = (N1 + N2) × 0,05 cm (1,6 cm pour 12 + 20 ou 16 + 16 : deux briques
1 × 2 qui se touchent). À savoir : 12 + 12 (1,2 cm) ou 20 + 20 (2,0 cm) ne tombent pas sur deux briques côte à côte
(1,6 cm) ; les dents ne se touchent pas, chaque axe reçoit alors sa commande « Axe » (pas d'engrènement).
Diagnostic 29 (non compté) : tous les placements sur la grille de deux coniques à 90° (centres tous les 0,4 cm depuis
le croisement, deux côtés, sens, décalage d'une plaque) ; les doubles coniques 12 / 12, 12 / 20, 20 / 20 sont
détectées dans toutes les positions plausibles. Le critère de contact par les sommets LDraw n'est pas valable pour les
dents coniques (elles entrent légitimement sous le cercle de pied de l'autre) : il sert seulement à repérer les cas. Les tests 19 à 21 placent le bras pour que le vérin soit
à sa longueur naturelle et choisissent les trous qui donnent l'angle de transmission le plus proche de 90°. Chaque test vérifie le nombre de corps, de commandes, puis
un tour complet de la commande en 36 pas : chaque engrenage tourne de son rapport attendu (2 %) et les engrènements
restent en prise (écart de phase < 0,05 rad).
