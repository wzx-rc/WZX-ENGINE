# PlasmaOrk Builder (web)

Builder modulaire de figurines Ork dans le navigateur : bibliothèque de pièces, rig, poses, viewport réaliste et export STL pour l'impression.
Three.js 0.170 (chargé depuis jsDelivr par l'importmap de `index.html`), sans étape de build.

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
- **Rendu** : PBR, environnement studio, ombres douces, tonemapping AgX ; mode Haute qualité (résolution et ombres plus fines), affichages Résine / Peint / Filaire, rotation de la lumière, vues caméra Face / Trois-quarts / Dos / Profil.

**Kits & options** : accessoires (rokkits, bombe, mine), plaques d'armure, sacoches, ornements et un curseur d'usure.

## Export

- **Exporter STL** : la figurine fusionnée dans la pose courante, en millimètres, Z vers le haut, posée sur le plateau.
- Menu ▾ : pièces séparées (un STL par emplacement dans un ZIP, plots d'assemblage optionnels) ou projet `.plasmaork.json`.
- Échelle 32 / 54 / 75 mm : c'est la hauteur des yeux de l'Ork (convention des échelles de figurines), la hauteur totale est affichée dans l'onglet Impression.

Le projet est sauvegardé dans le navigateur (localStorage), avec annuler / rétablir (Ctrl+Z / Ctrl+Y). Les STL importés ne sont pas conservés au rechargement.

## Code

- `js/main.js` : état, historique, interface et événements
- `js/viewport.js` : rendu (renderer, lumières, environnement, caméra, gizmo) et moteur de vignettes hors écran
- `js/figure.js` : la figurine assemblée (pièces accrochées aux os, kits, options, groupes d'export)
- `js/rig.js` : os, poses, os réglables par emplacement
- `js/parts.js` : catalogue et pièces de départ **procédurales** (à remplacer par les vrais STL sculptés)
- `js/io.js` : lecture STL, export STL / ZIP / JSON

Pour ajouter une pièce sculptée au catalogue : l'exporter centrée sur son articulation (l'origine = le pivot de l'os) et l'ajouter comme variante dans `parts.js`, ou l'importer depuis l'interface.
