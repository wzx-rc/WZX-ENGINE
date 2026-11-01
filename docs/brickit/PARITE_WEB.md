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
- [x] Voxels (points de la brique sélectionnée / en main seulement, comme le web) ; [ ] filtre par famille
- [x] Supprimer (grisé sans sélection)
- [~] Qualité Rapide / HQ (MSAA ; pas de SSAO) + Path tracing (natif en plus) ; [x] Tonemap ACES / AgX / Std
- [x] Mécanique ; [x] DEV (bouton cyan, toasts du web, panneau « Mode développeur » : Connecteurs / Contour LDraw / Remplacements, légende, liste avec pastilles à jour / LDraw modifié / absente, Exporter / Retirer (confirmation), résumé, Importer un maillage… ; connecteurs par type sur toutes les briques, contour LDraw à 30° sous un remplacement, badge « M » sur les cartes) — capture 16 ; [x] Fenêtres ; [x] Scène (ouvre la fiche « Scène partagée »)
- [x] Infobulles (titres du web)

### Vue 3D `main.stage`
- [x] La vue occupe la zone entre l'en-tête et la bibliothèque (même cadrage que le web), couleur courante Yellow 14 au départ
- [x] Scène du web en temps réel : fond et brouillard 0x111214, sol 0x0e0f11, grille des tenons, axes X rouge / Z bleu, key chaude 3,4 + ombres, environnement 0,55, ACES 1, caméra fov 32 / yaw 38 / pitch 32
- [ ] Rim froide 1,3 et hémisphère 0,25 (le moteur n'a qu'un soleil en raster)
- [x] Toast (haut de la vue, bord jaune, fondu)
- [x] HUD « Infos (pièces, fps) » (masqué par défaut, via Fenêtres)
- [ ] Écran « Chargement de la bibliothèque BrickIt… »

### Menu radial `#opts` (main gauche)
- [x] Grip, titre (nom de la pièce ou « AUCUNE BRIQUE »), 4 coins + Dupliquer au centre, état inactif à 45 %
- [x] Tourner G/D, Basculer haut/bas, Dupliquer (empilé)
- [x] Pilules Poser / Annuler pendant la prise en main ; [x] Pivot (✎) et Prendre (✋) : masquées dans le web v26 (`display: none !important`), donc masquées aussi
- [ ] Panneau déplaçable / ancrable à droite (`optsRight`)

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
- [ ] Détacher / ancrer le plateau (flottant, zone d'ancrage)

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
- [x] Texte JSON, Copier, Charger ce texte, Ouvrir un fichier…, Exporter .ldr, Nouvelle scène, Fermer ; [ ] Exporter vers Maya

### Manuel `#manual`, `#mbig`, `#pdfbig`
- [ ] Livret d'étapes (pages lues verticalement), grand format, import PDF lu verticalement, redimensionnable

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
- [x] Préférences : sécurité, grille, auto pivot, animations, focus, mode, qualité, tonemap, navigation, couleur, panneaux ; [ ] position du radial (à droite)
- [ ] `vp3d-manual-w/h`, `vp3d-manual-pdf-page`

## 6. Fichiers et exports
- [x] `.v3d.json` (format vp3d-scene v1, compatible web)
- [x] Export `.ldr` (même transformation que le web), [ ] Export Maya (script Python + rig, `maya_export.js` / `maya_template.py`), [x] mode DEV : export zip (<id>.obj + LISEZMOI.txt, origine LDraw, cm, Y haut, repères ref_connecteurs_*), réimport OBJ (groupes ref_ ignorés, devine l'unité, décalage du centre, axes, revue Annuler / Recaler / Garder), aller-retour vérifié à l'identique (--dev-roundtrip 3001)

## 7. Mécanique (détails)
- [x] Connecteurs, corps, pivots, engrènements, arbre, boucles, solveur LM, butées, rapports, phase des dents, couleurs par corps, liaisons
- [~] Glisser une pièce mobile (codé, à tester)
- [ ] Vérins (glissières) et plateaux tournants (métadonnées `cyl` / `pair` à dériver)

## Écarts voulus

### Corrections (bugs du web non reproduits)
- Taille des miniatures : le web démarre à 64 px parce que `+localStorage.getItem("vp3d-thumb-size")` vaut 0 quand la clé est absente
  (l'intention est 84 px). Le natif reprend 64 px pour l'identique visuel ; à corriger dans le web si 84 était voulu.

### Corrections mécanique
- (aucune pour l'instant)

### Stockage / formats (mode DEV)
- Remplacements gardés dans `dev_overrides\<id>.obj` + `<id>.json` à côté de l'exe (le web : IndexedDB « stocké sur cet appareil » ou base de l'artifact). Préférences DEV dans `brickit_prefs.json` (clé `dev`).
- FBX : pas de lecteur FBX natif, le réimport demande un OBJ (toast « Lecture impossible : le FBX n'est pas lu par la version native… »).
- Export : boîte « Enregistrer sous » (le web : téléchargement, sinon la fiche texte de secours).

### Rendu
- Polices : Segoe UI + Segoe UI Symbol fusionnées pour les glyphes du web (› … ★ ☆ ✎ ✋ ◀ ▶), absents de la police latine seule.
- Path tracing natif (studio clair validé sur 03b) en plus du temps réel identique au web.
