# Ancient Coast — interface Dear ImGui

Reproduction native C++ de l'interface du concept fourni : palette sombre, barre d'outils, arborescence de scène, viewport, navigateur de contenu et inspecteur. Les libellés anglais et les proportions du concept servent de référence.

L'interface utilise de vrais contrôles Dear ImGui. Le viewport utilise une variante du concept dont les contrôles intégrés dans l'image ont été retirés ; les miniatures proviennent de la photographie fournie. La scène représentée reste une prévisualisation par image. L'état des acteurs est accessible par une API C++ pour le raccorder à un moteur 3D.

## Lancer

Double-cliquer sur `launch.cmd` ou sur `dist/Atelier-ImGui/Launch-Atelier.cmd`. La version distribuée se lance aussi directement avec `dist/Atelier-ImGui/Atelier.exe`.

Pour déplacer l'application, copier le dossier `dist/Atelier-ImGui` complet, avec son dossier `assets`. Le runtime C++ est intégré à la version Release ; aucune installation de Dear ImGui ou d'un runtime Python/Node n'est nécessaire.

La fenêtre s'ouvre avec une zone de dessin de 1280 × 720. Elle est redimensionnable. `F12` enregistre une capture dans `dist/Atelier-ImGui/outputs/capture.png`. Le journal du lanceur est dans `dist/Atelier-ImGui/outputs/host.log`.

## Utiliser l'interface

- **Scene** : rechercher et sélectionner un acteur, ouvrir les dossiers, afficher/masquer et verrouiller les entrées. Glisser un acteur dans un dossier pour le déplacer dans la hiérarchie ; les cycles sont refusés. Le clic droit propose renommer, dupliquer et supprimer.
- **Viewport** : molette pour zoomer dans l'image, glisser avec le bouton droit pour la déplacer. Le gizmo modifie les valeurs de transformation de l'acteur sélectionné. `W`, `E`, `R` choisissent translation, rotation et échelle ; le bouton aimant active un pas de 0,5 unité ou 15°.
- **Content Browser** : sélectionner un asset, rechercher, filtrer par dossier, choisir grille/liste et régler la taille des miniatures. Double-cliquer pour ouvrir l'aperçu ; glisser un asset dans le viewport ou un dossier de scène pour ajouter un acteur.
- **Inspector** : glisser les champs de transformation ou faire `Ctrl+clic` pour saisir un nombre. Choisir un mesh ou un matériau, modifier les options de rendu, collision et physique, ajouter des composants et des tags.
- **Lighting / World Settings** : régler les paramètres de démonstration, le nom de scène et ouvrir/enregistrer les données. L'exposition et l'heure teintent l'image de référence.
- **Materials / Animations / Visual Scripting** : attribuer un matériau, contrôler le temps de la séquence, déplacer/ajouter des nœuds et exécuter leurs actions sur les données de l'acteur sélectionné.
- **Disposition** : glisser les séparateurs des panneaux. `F11` agrandit le viewport ; `F` réinitialise son cadrage.

Les menus et les infobulles donnent accès aux autres actions. `Help > Controls & shortcuts` rappelle les commandes.

| Raccourci | Action |
|---|---|
| `Ctrl+S` / `Ctrl+O` | Enregistrer / ouvrir `Ancient_Coast.atelier` |
| `Ctrl+N` | Restaurer la scène du concept après confirmation |
| `Ctrl+Z` / `Ctrl+Y` | Annuler / rétablir les modifications d'acteurs |
| `Ctrl+D` / `Suppr` | Dupliquer / supprimer l'acteur sélectionné |
| `F2` | Renommer la sélection |
| `Espace` | Lecture / pause du temps de prévisualisation |

Le fichier `.atelier` conserve les acteurs, leurs transformations, composants, tags et propriétés, ainsi que les dimensions des panneaux et les paramètres d'éclairage/monde. Le graphe et la séquence sont des démonstrations en mémoire. L'enregistrement est manuel.

Le fichier est écrit à côté de l'exécutable : `dist/Atelier-ImGui/Ancient_Coast.atelier`. Le menu `Project` affiche le chemin exact. Le module réutilisable accepte un autre emplacement via `SetSceneFile()`.

## Recompiler

Installer Visual Studio ou les Build Tools avec la charge **Développement Desktop en C++**, MSVC x64 et le SDK Windows. Dear ImGui est inclus dans le projet.

Depuis le dossier du projet :

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Le script produit `dist/Atelier-ImGui/Atelier.exe` et copie ses assets. Pour une compilation Debug, ajouter `-DebugBuild`. Le lanceur `launch.cmd` déclenche aussi la compilation si l'exécutable est absent.

`CMakeLists.txt` fournit une autre voie de compilation pour un environnement CMake et MSVC.

## Capture automatisée

```powershell
.\dist\Atelier-ImGui\Atelier.exe --width 1280 --height 720 --screenshot .\outputs\interface.png --frames 20
```

La capture est prise à la dixième frame, puis l'application se ferme après 20 frames. `--width` et `--height` règlent la taille initiale de la zone de dessin.

## Vérifications

Le rapport [outputs/ui-self-test.json](outputs/ui-self-test.json) confirme **11 contrôles réussis** sur les données de scène : sélection initiale, insertion, duplication, suppression, historique, protection contre la suppression d'un acteur verrouillé, sauvegarde/chargement et unicité des IDs après restauration.

Pour relancer ces contrôles :

```powershell
.\dist\Atelier-ImGui\Atelier.exe --smoke-test --frames 20
```

Le rapport est écrit dans `outputs/ui-self-test.json` du dossier de travail. Ces contrôles vérifient le modèle et ses commandes ; la comparaison visuelle et les interactions à la souris se vérifient dans l'application.

## Sources

- `src/editor_ui.cpp` : interface et état de démonstration.
- `src/editor_ui.h` : API `Initialize()`, `Draw()`, `GetState()`, `SetSelection()`, `SetSceneFile()` et `Shutdown()`.
- `src/ui_icons.h` : icônes vectorielles.
- `src/main.cpp` : lanceur Windows, Direct3D 11 et chargement JPEG WIC.
- `assets/reference.jpg` : image du concept.
- `assets/viewport-clean.png` : variante pour dessiner séparément les contrôles du viewport.
- `vendor/imgui/` : Dear ImGui et sa licence.

Pour intégrer l'interface dans une application existante, consulter [docs/INTEGRATION.md](docs/INTEGRATION.md).

## Limites

Cette livraison reproduit l'éditeur et ses interactions de démonstration. Le rendu 3D, l'import de meshes, la compilation de scripts et la simulation physique nécessitent une connexion au moteur et au modèle de données de l'application hôte.

Les paramètres de caméra et de shading restent des modes de prévisualisation de l'image. Les compteurs FPS et Frame mesurent l'application ; les statistiques géométriques reproduisent les chiffres du concept. Les modifications d'acteurs sont annulables ; l'éclairage, le nom de scène et la disposition des panneaux ne font pas partie de cet historique.
