# Parité BrickIt natif / web v26 — traduction 1:1

Consigne de Wzx (01/11/2026) : **tout comme le web**, chaque détail du HTML v26 (`docs/brickit/web/BrickIt_Builder.html`)
réécrit en C++. Seules différences permises : la densité pour la 4K (échelle réglable) et le rendu (path tracing natif en plus).
Cases : `[x]` porté et vérifié · `[~]` partiel (ce qui manque est dit) · `[ ]` à faire.

## 1. Interface (structure du DOM)

### En-tête `header.bar`
- [x] Logo « 3D » jaune + « BrickIt Builder »
- [x] Annuler / Rétablir (grisés quand la pile est vide)
- [x] Modes Sélection / Viseur / Déplacer / Tourner (boutons, état jaune)
- [x] Mode Sélection : clic = sélection ; [~] rectangle de sélection (`#marquee`, codé, à tester à la souris)
- [x] Mode Déplacer : gizmo flèches rouge / jaune / bleu (crans tenon / plaque, sol respecté, multi-sélection), sphère blanche = déplacement libre en main ; [x] Mode Tourner : anneaux 90° autour du pivot (+ rebond 0,92) ; taille 0,07 × distance, blanc au survol — captures 13 / 13b
- [x] Modes Déplacer / Tourner : un clic sélectionne (Auto Pivot déplace le pivot), glisser fait orbiter ; Viseur : glisser le vide fait orbiter (comme le web)
- [x] Grille, [x] Auto Pivot, [x] Sécurité 50 %, [x] Focus, [x] Animations
- [x] Voxels (points de la brique sélectionnée / en main seulement, la brique à 30 % pour voir ses points ; cycle à 2 états du web v26 : masqués / affichés — le filtre par famille n'existe plus dans `VOX_STEPS`)
- [x] Supprimer (grisé sans sélection)
- [~] Qualité Rapide / HQ (MSAA ; pas de SSAO) + Path tracing (natif en plus) ; [x] Tonemap ACES / AgX / Std
- [x] Mécanique ; [x] DEV (bouton cyan, toasts du web, panneau « Mode développeur » : Connecteurs / Contour LDraw / Remplacements, légende, liste avec pastilles à jour / LDraw modifié / absente, Exporter / Retirer (confirmation), résumé, Importer un maillage… ; connecteurs par type sur toutes les briques, contour LDraw à 30° sous un remplacement, badge « M » sur les cartes) — capture 16 ; [x] Fenêtres ; [x] Scène (ouvre la fiche « Scène partagée »)
- [x] Infobulles (titres du web)

### Vue 3D `main.stage`
- [x] La vue occupe la zone entre l'en-tête et la bibliothèque (même cadrage que le web), couleur courante Yellow 14 au départ
- [x] Scène du web en temps réel : fond et brouillard 0x111214, sol 0x0e0f11, grille des tenons, axes X rouge / Z bleu, key chaude 3,4 + ombres, environnement 0,55, ACES 1, caméra fov 32 / yaw 38 / pitch 32
- [x] Rim froide 0xc0d4ff 1,3 depuis (26, 22, −30) (lumière de surface lointaine, éclairement 1,3, sans ombre) et hémisphère 0x9aa3b8 / 0x1a1a1c 0,25 (nouveau terme du raster du moteur) — capture 20
- [x] Toast (haut de la vue, bord jaune, fondu)
- [x] HUD « Infos (pièces, fps) » (masqué par défaut, via Fenêtres)
- [x] Écran « Chargement de la bibliothèque BrickIt… » (fond 0x131417, texte 14 px gris, pendant le chargement de la bibliothèque et des miniatures)

### Menu radial `#opts` (main gauche)
- [x] Grip, titre (nom de la pièce ou « AUCUNE BRIQUE »), 4 coins + Dupliquer au centre, état inactif à 45 %
- [x] Tourner G/D, Basculer haut/bas, Dupliquer (empilé)
- [x] Pilules Poser / Annuler pendant la prise en main ; [x] Pivot (✎) et Prendre (✋) : masquées dans le web v26 (`display: none !important`), donc masquées aussi
- [x] Panneau déplaçable (glisser la poignée = fenêtre flottante) et ancrable à gauche / à droite (`optsRight`, mémorisé), zone d'ancrage pointillée — capture 19

### Couleurs `#colpanel`
- [x] Grip « Couleurs » repliable, couleur courante, groupes Unies / Transparentes / Métal, reflets trans / métal, contour jaune
- [x] Masqué en mode Mécanique

### Bibliothèque `footer.tray`
- [x] Grip « Bibliothèque » repliable
- [x] Rangée 1 : Récentes (horloge), Favoris (étoile), Toutes, 9 familles LDraw avec compteurs, loupe, taille
- [x] Rangée 2 : « Tout » + catégories en pilules (vides en pointillé), sous-familles Technic, notes (Récentes / Favoris / Toutes / Recherche)
- [x] Cartes sur 2 rangées, 64 / 84 / 112 px, image zoomée ×1,4, cadre, ★ favori, défilement horizontal
- [x] Recherche (nom, référence, catégorie FR / EN), Échap vide la recherche
- [x] Clic = brique en main ; [x] glisser hors du plateau = glisser-déposer ; [x] appui long 480 ms = fiche pièce
- [x] Atténuée à 45 % et inactive en mode Mécanique
- [x] Détacher / ancrer le plateau (flottant min(780 px, 94 vw), bord accent, zone d'ancrage en bas ; clic sur la poignée = replier) ; onglets et catégories défilent de côté (`.fscroll`, `.cats`) — capture 19

### Fiche pièce `#info` (appui long)
- [x] Fiche simple du web (`showPartSimple`) : visionneuse 3D (studio des miniatures, ACES 0,92, caméra 24° (1, 0,8, 1,3), glisser = lacet ×0,012 / tangage ×0,01 borné ±1,4, rotation en espace caméra), « Glisse pour tourner la pièce », nom, famille › catégorie, ☆ / ★ Favori, Fermer, Ajouter à la scène, Échap / clic dehors — capture 15
- Natif : pendant la fiche, la vue principale garde sa dernière image (le moteur ne dessine que la visionneuse)
- [x] Fiche complète en mode DEV (image, titre LDraw, pastilles de connecteurs, Famille … Origine LDraw, crédit, rangée DEV : Exporter pour Maya, Importer le maillage modifié, Revenir à LDraw, note du remplacement) — capture 16b

### Panneau Mécanique `#mechPanel`
- [x] Titre, Quitter (Échap), Pose de montage, Liaisons, Couleurs par corps
- [x] Stats (corps, mobiles, pivots, vérins, engrenages, boucles, DDL, commandes), avertissements
- [x] Commandes : nom, rapports, valeur en jaune, curseur −720..720°, ◀ touche / touche ▶ (maintien)
- [x] Message de butée (fond rouge)

### Fenêtres `#winmenu`
- [x] Bibliothèque, Manuel d'instructions, Infos (pièces, fps), Couleurs, Actions de la brique, Réinitialiser la disposition (toast « Disposition réinitialisée »)

### Fiche « Scène partagée » `#sheet`
- [x] Texte JSON, Copier, Charger ce texte, Ouvrir un fichier…, Exporter .ldr, Nouvelle scène, Fermer ; [x] Exporter vers Maya

### Manuel `#manual`, `#mbig`, `#pdfbig`
- [x] Livret d'étapes lu verticalement (étape n = n premières briques, la n-ième en surbrillance 0xffc21a × 0,3, papier 0xf4f2ea, caméra 28° (1, 0,75, 1), 960 × 600, pièce à ajouter + « 1× », « p. n »), en-tête Manuel / « Étapes a–b / N » / Scène·PDF / PDF / ‹ › / – / ⤢ — capture 18
- [x] Grand format `#mbig` (livre vertical 760 px, ✕, ‹ ›) et `#pdfbig` (1000 px, ✕)
- [x] Import PDF lu verticalement (pages blanches l'une sous l'autre, rendues à l'approche ±600 px, libérées au-delà de 6 pages, « n / N », « PDF p. n / N », ‹ › défilent d'une page, toast « PDF importé : N pages », réouvert au lancement) — capture 18b avec le PDF LEGO 60498 (84 pages)
- [x] Redimensionnable par le coin bas gauche en largeur (260..900) et en hauteur (160..), mémorisé

## 2. Clavier (web)
- [x] Ctrl+Z / Ctrl+Y
- [x] R ou E = tourner +90°, Q = −90°
- [x] C = cycle du pivot d'accroche (toast « Pivot d'accroche : voxel n / N »), V = voxels, A = animations, F = focus (toasts du web) ; [x] P = édition du pivot (sphères 0,2 par voxel aux couleurs tenon / pin / axle, pivot courant blanc ×1,5, le curseur vise le voxel le plus proche, la brique en main ne flotte plus, le clic ne pose pas) — capture 14
- [x] Échap = annuler la prise / désélectionner ; [x] Suppr / Retour arrière
- [x] Espace = prendre en main la brique sélectionnée
- [x] Mécanique : A/Q, Z/S, E/D, R/F… ; Échap quitte
- Natif en plus (à garder hors des touches du web) : Ctrl+D, Ctrl+S / Ctrl+O, M (Mécanique), path tracing

## 3. Souris / doigt
- [x] Viseur : toucher une brique la prend en main, relâcher la pose
- [x] Glisser depuis la bibliothèque
- [~] Rectangle de sélection (mode Sélection, Maj/Ctrl ajoute, toast « n briques sélectionnées ») : codé, à tester
- [x] Orbite / pan / zoom
- [x] Gizmo flèches et anneaux (à essayer à la souris)

## 4. Animations et timings
- [x] Brique qui tombe dans la main (écrasement 0,55, ressort w22 z.35), flottement 0,9 + oscillation, ressort w14, bascule selon la vitesse, pose 0,15 s (t²), rebond à la pose (0,86, w30 z.28, 0,6 s), halo du voxel visé (0,18 s, pulsation) — capture 12
- [x] Aperçu de pose : fantôme jaune / rouge 32 %, carrés du viseur au sol, ligne pointillée, anneau pulsant au pivot ; Animations coupées = pas d'aperçu (comme le web)
- [x] Toast 3,2 s avec fondu

## 5. Préférences et sauvegarde (localStorage du web → fichiers à côté de l'exe)
- [x] `vp3d-builder-scene` : sauvegarde automatique (`brickit_scene.v3d.json`) et reprise au lancement, sinon la scène d'exemple du web
- [x] `vp3d-recent-parts`, `vp3d-fav-parts`, `vp3d-thumb-size` (`brickit_prefs.json`)
- [x] Préférences : sécurité, grille, auto pivot, animations, focus, mode, qualité, tonemap, navigation, couleur, panneaux (visible, replié, flottant, x, y) ; [x] position du radial (`optsRight`)
- [x] `vp3d-manual-w/h`, `vp3d-manual-pdf-page` (clé `manual` de `brickit_prefs.json`, PDF copié dans `manual_pdf\current.pdf`)

## 6. Fichiers et exports
- [x] `.v3d.json` (format vp3d-scene v1, compatible web)
- [x] Export `.ldr` (même transformation que le web), [x] Export Maya (port de `maya_export.js`, même `maya_template.py` que le web : maillages fusionnés, matrices, shaders, rig corps / liaisons, set driven keys échantillonnées au solveur, contrôle du couplage, zip .py + LISEZMOI.txt) — vérifié dans mayapy 2027 : 14 briques, 5 corps, 4 liaisons, moteur1 = 90° fait tourner J0..J3 (rapport 1:3 compris), [x] mode DEV : export zip (<id>.obj + LISEZMOI.txt, origine LDraw, cm, Y haut, repères ref_connecteurs_*), réimport OBJ (groupes ref_ ignorés, devine l'unité, décalage du centre, axes, revue Annuler / Recaler / Garder), aller-retour vérifié à l'identique (--dev-roundtrip 3001)

## 7. Mécanique (détails)
- [x] Connecteurs, corps, pivots, engrènements, arbre, boucles, solveur LM, butées, rapports, phase des dents, couleurs par corps, liaisons
- [~] Glisser une pièce mobile (codé, à tester)
- [x] Plateaux tournants : pivot entre une moitié « Bottom / Base » et sa moitié « Top » de la même famille (5 familles : 5 x 5, 28 dents, 60 dents, Type 1, Type 2), vérifié par `--turntable-test` (2 corps, 1 pivot pour chacune) ; sans engrenage il n'a pas de moteur et se manipule au glisser, comme la règle du web
- [ ] Vérins : inactifs comme dans le web v26 (aucune pièce ne reçoit `cyl`) ; la conversion LDraw → BrickIt ne porte pas les glissières LDCad (axe, course), il faut les ajouter au convertisseur (`ldraw2brickit.mjs`) avant de pouvoir les détecter

## Écarts voulus

### Corrections (bugs du web non reproduits)
- Taille des miniatures : le web démarre à 64 px parce que `+localStorage.getItem("vp3d-thumb-size")` vaut 0 quand la clé est absente
  (l'intention est 84 px). Le natif reprend 64 px pour l'identique visuel ; à corriger dans le web si 84 était voulu.

### Corrections mécanique
- Plateaux tournants : le web v26 contient la détection (`metas[i].pair`) mais aucune pièce ne reçoit cette donnée, donc
  ils ne tournent jamais. Le natif la dérive du titre LDraw (Top / Bottom / Base, même famille, axe vertical par le centre
  de la pièce). Le test du web (centres à 1 mm près le long de l'axe) échoue toujours pour deux moitiés de hauteurs
  différentes : le natif demande que leurs étendues le long de l'axe se touchent ou se recouvrent (0,1 cm de jeu), et ne
  soude pas les deux moitiés par leurs tenons (le dessus tourne sur la base).

### Stockage / formats (mode DEV)
- Remplacements gardés dans `dev_overrides\<id>.obj` + `<id>.json` à côté de l'exe (le web : IndexedDB « stocké sur cet appareil » ou base de l'artifact). Préférences DEV dans `brickit_prefs.json` (clé `dev`).
- FBX : pas de lecteur FBX natif, le réimport demande un OBJ (toast « Lecture impossible : le FBX n'est pas lu par la version native… »).
- Export : boîte « Enregistrer sous » (le web : téléchargement, sinon la fiche texte de secours).

- Export Maya : compression zlib maison (LZ77 + Huffman fixe, `Deflate.cpp`) au lieu de CompressionStream ; tirages du contrôle de couplage à graine fixe (le web : Math.random).

- PDF : rendu par le lecteur PDF de Windows (Windows.Data.Pdf, WinRT) au lieu de pdf.js ; stocké à côté de l'exe au lieu d'IndexedDB.

### Rendu
- Polices : Segoe UI + Segoe UI Symbol fusionnées pour les glyphes du web (› … ★ ☆ ✎ ✋ ◀ ▶), absents de la police latine seule.
- Path tracing natif (studio clair validé sur 03b) en plus du temps réel identique au web.
