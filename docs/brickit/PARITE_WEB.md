# Parité BrickIt natif / web v26

Référence : `docs/brickit/web/BrickIt_Builder.html`, fonction par fonction. État au 01/11/2026 (branche `brickit-native`).
Légende : **porté** = fait et vérifié, **partiel** = l'essentiel est là mais il manque des morceaux, **absent** = pas commencé.

## Rendu

| Fonction web | État | Détail natif |
|---|---|---|
| Rendu temps réel | **porté** | Raster DX12 instancié, LOD, culling ; 50k briques. |
| Qualité HQ / rapide (`#quality`, SSAO) | **partiel** | Raster MSAA et path tracing (P) ; pas d'AO dans le raster, pas de sélecteur HQ/rapide. |
| Transformation de vue (`#tonemap` ACES / AgX / Std) | **partiel** | ACES par défaut, AgX, AgX Punchy, Standard en option de ligne de commande `--tonemap` ; pas de menu dans l'UI. |
| Bevel écran + rainures | **porté** | Bevel du path tracer (0,035 cm) ; pas de bevel en raster. |
| Traces de doigts, SSS | **porté** | En plus du web (path tracing et raster). |

## Construction

| Fonction web | État | Détail natif |
|---|---|---|
| Brique en main, snap typé (`computeTarget`, `SNAP_OK`) | **porté** | Tenon/anti-tenon, pin/trou, axe/trou ; tenons recouverts exclus. |
| Grille des tenons (`#gridSnap`) | **porté** | Touche G, bouton. |
| Sécurité 50 % + pivot voisin (`#safety`) | **porté** | Toujours active, sans bouton pour la couper. |
| Pivot auto (`#autoPivot`) | **partiel** | Le pivot = voxel le plus proche du point saisi quand on déplace une brique posée ; pas d'option on/off, pas de cycle de pivot ni d'édition du pivot (`oPivot`, `cycleGrab`). |
| Affichage des voxels (`#vxBtn`) | **absent** | |
| Animations (`#anim` : rebond, écrasement, bascule) | **absent** | Pose instantanée. |
| Menu radial (`#radial` : tourner G/D, basculer haut/bas, dupliquer) | **absent** | Clavier seulement : R / Maj+R tourner, T / Maj+T basculer, Ctrl+D. Pas de « monter / descendre ». |
| Gizmo déplacer / tourner (`#modeMove`, `#modeRotate`) | **absent** | |
| Dupliquer empilé (`stackedCopy`) | **porté** | Ctrl+D, la copie est sélectionnée. |
| Multi-sélection Maj | **porté** | |
| Sélection au rectangle (`#marquee`) | **absent** | |
| Mode focus 50 % (`#focusDim`) | **porté** | Touche H, bouton. |
| Couleurs (`#swatches`, panneau à droite) | **porté** | |
| Annuler / rétablir, supprimer | **porté** | |
| Glisser-déposer depuis la bibliothèque (`startLibDrag`) | **absent** | Un clic sur une miniature met la pièce en main. |

## Bibliothèque

| Fonction web | État | Détail natif |
|---|---|---|
| Familles / catégories sur 2 rangées, sous-familles Technic | **porté** | Seules les familles qui ont des pièces sont affichées (pas l'arbre LDraw complet vide). |
| Récents, favoris (clic droit), recherche (loupe), taille des miniatures | **porté** | |
| Miniatures | **porté** | Rendues par le moteur, gris neutre, studio du web, cache disque. |
| Fiche pièce à l'appui long (visionneuse 3D tournante, favori) | **absent** | Info-bulle avec le nom et la référence seulement. |
| Fiche DEV complète (métadonnées) | **absent** | |

## Fichiers et exports

| Fonction web | État | Détail natif |
|---|---|---|
| Ouvrir / enregistrer `.v3d.json` | **porté** | Compatible web (aller-retour vérifié). |
| Coller / copier le texte de scène (`#sceneText`, `#copyScene`) | **absent** | |
| Nouvelle scène (`#newScene`) | **absent** | |
| Export `.ldr` (`sceneLdr`) | **absent** | |
| Export Maya de scène (script Python + rig, `mayaExportScript`) | **absent** | |

## Mode DEV

| Fonction web | État |
|---|---|
| Bouton DEV, voxels / connecteurs LDraw, export et réimport Maya OBJ des pièces, overrides | **absent** |

## Manuel

| Fonction web | État |
|---|---|
| Module Manuel (étapes, doubles pages, grand format) | **absent** |
| Import PDF et lecture verticale | **absent** |

## Fenêtres et HUD

| Fonction web | État | Détail natif |
|---|---|---|
| Menu Fenêtres (`#winBtn`), panneaux flottants / ancrés, HUD | **partiel** | Panneaux fixes (barre d'outils, stats, bibliothèque, couleurs, mécanique) ; pas de menu Fenêtres, pas de panneaux déplaçables. |

## Mécanique

| Fonction web | État | Détail natif |
|---|---|---|
| Détection corps / pivots / engrènements, arbre, boucles, solveur, moteurs | **porté** | |
| Phase des dents | **porté** | |
| Glisser une pièce mobile | **partiel** | Codé, pas encore testé à la souris. |
| Pose de montage (`#mechReset`) | **porté** | « Pose de départ ». |
| Liaisons affichées (`#mechJoints`) | **porté** | Toujours affichées (pas de bouton). |
| Couleurs par corps (`#mechTint`) | **absent** | |
| Diagnostics (engrenages pas en prise, axes non portés, trains bloqués) | **porté** | Pas la liste des pièces sans connecteur. |
| Vérins (glissières) et plateaux tournants | **absent** | Les métadonnées `cyl` / `pair` du web ne sont pas dans les packs ; à dériver des pièces LDraw. |

## Absents, liste courte

Menu radial · gizmo déplacer/tourner · sélection au rectangle · animations · affichage des voxels et édition du pivot ·
glisser-déposer depuis la bibliothèque · fiche pièce (visionneuse 3D) et fiche DEV · mode DEV (connecteurs, export/import Maya OBJ,
overrides) · export Maya de scène · export .ldr · copier/coller de scène, nouvelle scène · module Manuel et PDF ·
menu Fenêtres et panneaux déplaçables · sélecteurs qualité/tonemap dans l'UI · couleurs par corps, vérins et plateaux tournants.
