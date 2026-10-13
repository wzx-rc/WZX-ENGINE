# Atelier — Material Creation / Preview

La fenêtre **Material Studio** reprend la palette sombre, les bordures fines, les icônes vectorielles et l'accent cyan des éditeurs Scene et Graph. Elle associe une bibliothèque de matériaux, les paramètres du matériau actif et un aperçu calculé en Direct3D 11.

## Lancer

Double-cliquer sur `launch.cmd` ou sur `dist/Atelier-ImGui/Launch-Atelier.cmd`. L'application ouvre Material Studio dans une fenêtre redimensionnable de 1600 × 900. Les commandes `--materials`, `--scene` et `--graph` permettent de choisir l'espace de travail au lancement.

```powershell
.\dist\Atelier-ImGui\Atelier.exe --materials
```

`F12` enregistre une capture PNG de la fenêtre dans `dist/Atelier-ImGui/outputs/capture.png`. Le journal `dist/Atelier-ImGui/outputs/host.log` indique l'état d'initialisation du renderer de matériaux. Une erreur d'initialisation est affichée dans l'interface.

## Utiliser la fenêtre

- **Material Library** : sélectionner un preset, rechercher par nom ou description, filtrer les métaux, diélectriques et surfaces émissives. Le bouton `+` crée un matériau ; le menu contextuel propose duplication, export et suppression. Le dernier matériau de la bibliothèque ne peut pas être supprimé.
- **Preview** : glisser avec le bouton gauche pour tourner la caméra, utiliser la molette pour zoomer, puis double-cliquer pour réinitialiser. **Turntable** fait tourner l'aperçu ; **Live Preview** suspend ou reprend son actualisation. Les séparateurs ajustent la largeur des panneaux et la hauteur de la zone inférieure.
- **Properties** : renommer, choisir le mode de surface et modifier les paramètres regroupés dans des sections repliables. La recherche filtre les sections. Les changements de matériau sont annulables.
- **Stage** : choisir l'environnement et régler l'éclairage, l'exposition, la caméra, la grille et les ombres de contact. Ces réglages de prévisualisation sont conservés pendant la session.
- **Texture Maps / Texture** : choisir un slot puis assigner une image, régler son activation, sa force et son canal, ou retirer l'assignation. Le sélecteur ouvre les fichiers locaux ; les cinq cartes affichent les images assignées.
- **Channel Overview** : cliquer un canal pour afficher sa vue de diagnostic dans l'aperçu. Ce panneau représente les entrées du matériau et leur surface de sortie.
- **Output / Shader Source** : lire les diagnostics ou le shader HLSL de l'aperçu. Le bouton de copie place le texte dans le presse-papiers ; le bouton de validation contrôle les matériaux et leurs références d'images.

Les statistiques affichent le FPS de l'application, la durée CPU de soumission de l'aperçu et la résolution de sa texture. La mesure CPU n'est pas une mesure du temps GPU.

| Commande | Action |
|---|---|
| `Ctrl+N` | Créer un matériau |
| `Ctrl+D` | Dupliquer le matériau actif |
| `Ctrl+Z` / `Ctrl+Y` | Annuler / rétablir les changements de matériau |
| `Ctrl+S` / `Ctrl+O` | Enregistrer / recharger la bibliothèque |
| `Ctrl+Entrée` | Valider et appliquer |
| `F` | Réinitialiser la caméra |
| `Espace` | Activer / arrêter la rotation automatique |
| `F1` | Ouvrir l'aide intégrée |
| `F12` | Capturer la fenêtre complète |

Les raccourcis s'appliquent hors des champs de texte et des menus ouverts.

## Enregistrer, exporter et appliquer

**Save** enregistre la bibliothèque complète dans `dist/Atelier-ImGui/MaterialLibrary.amat`. Cette sauvegarde est rechargée automatiquement au prochain lancement. `Ctrl+O` permet aussi de la recharger pendant la session. L'enregistrement des modifications reste manuel.

Le format textuel `ATELIER_MATERIAL_LIBRARY` version 1 conserve les IDs, la sélection, les noms UTF-8, les descriptions, les tags, tous les paramètres et les chemins des cinq images. Les paramètres de caméra, d'éclairage et de disposition ne sont pas enregistrés. La sauvegarde écrit un fichier temporaire avant de remplacer la version précédente ; un fichier invalide ou incomplet ne remplace pas la bibliothèque active.

**Export selected material** écrit un fichier contenant le seul matériau actif dans `dist/Atelier-ImGui/exports/<nom>.amat`. Les images restent référencées par leur chemin et ne sont pas copiées dans le fichier `.amat` ; conserver leurs emplacements ou adapter ces chemins dans le moteur hôte.

Le bouton caméra et **Export preview PNG** enregistrent la texture effectivement rendue dans `dist/Atelier-ImGui/outputs/<nom>-preview.png`. En mode suspendu, cette image correspond à l'aperçu affiché. `F12` capture toute la fenêtre de l'éditeur.

**Apply** valide le matériau actif, conserve une copie et appelle le callback du moteur s'il a été configuré. Le changement visible dans l'aperçu est immédiat ; l'application aux objets de Scene ou au moteur nécessite ce raccordement.

## Matériau et aperçu

Le modèle propose quatre modes de surface : **Opaque**, **Cutout**, **Transparent** et **Unlit**. Les paramètres comprennent la couleur de base, la rugosité, le métal, la normale, l'occlusion, l'émission, l'opacité, l'IOR, le clearcoat, l'anisotropie et le sheen. Les coordonnées UV disposent d'une échelle, d'un décalage, d'une rotation et d'un choix de répétition.

Les cinq slots d'images sont **Base Color**, **Roughness**, **Metallic**, **Normal** et **Ambient Occlusion**. Le chargement WIC accepte PNG, JPEG, TIFF et BMP, jusqu'à 8192 × 8192 pixels par image. Les textures restent des fichiers référencés par leur chemin. Leur activation et leur force sont distinctes ; les cartes de données permettent de choisir un canal. La carte de normale utilise son vecteur RGB.

Les motifs procéduraux **Smooth**, **Brushed Metal**, **Ancient Stone**, **Wood Grain**, **Checker** et **Woven Fabric** permettent d'essayer un matériau sans images externes. Leurs couleurs et normales sont calculées par le shader de prévisualisation.

L'aperçu propose **Sphere**, **Cube**, **Torus**, **Plane** et **Shader Ball**, ainsi que les éclairages **Studio**, **Sunset** et **Warehouse**. L'exposition, la rotation de l'environnement et les intensités des trois sources sont réglables. Les vues de diagnostic montrent la couleur de base, la rugosité, le métal, les normales et l'occlusion.

Le renderer dessine des formes analytiques dans une texture Direct3D 11. Il calcule l'éclairage direct avec GGX, Smith et Schlick ; les reflets d'environnement sont une approximation procédurale. Les valeurs sont appliquées en temps réel à l'aperçu.

## Portée du rendu

Les environnements sont générés par le shader ; cette version ne charge pas de fichier HDRI. La transparence compose la surface avec le fond, sans réfraction ni tri de plusieurs objets. L'IOR modifie la réponse de Fresnel. Le sol utilise une ombre de contact approximative.

Les formes de test sont analytiques ; l'import de mesh n'est pas fourni. **Receive Shadows** active une approximation d'occlusion locale sur les formes creuses et rainurées. **Two Sided** est conservé dans les données pour le moteur hôte, sans effet visible sur les formes fermées de l'aperçu. Le module ne compile pas un graphe de matériau en shader du moteur.

## Réutiliser les sources

Le module se compose de `src/material_ui.h`, `src/material_ui.cpp`, `src/material_model.h`, `src/material_model.cpp`, `src/material_renderer.h` et `src/material_renderer.cpp`. Le modèle ne dépend pas d'ImGui. Le renderer de prévisualisation utilise l'appareil et le contexte Direct3D 11 de l'application hôte.

Les liens Windows requis comprennent `d3d11`, `d3dcompiler`, `windowscodecs`, `ole32` et `comdlg32` ; le projet fourni les déclare dans `build.ps1` et `CMakeLists.txt`.

Le renderer doit être initialisé après la création de l'appareil et du contexte Direct3D, puis arrêté avant leur destruction. `GetRendererStatus()` expose son diagnostic. En cas d'échec, l'application conserve l'interface et présente le diagnostic à la place de l'aperçu.

```cpp
#include "material_ui.h"
#include "material_renderer.h"

atelier::material::InitializeRenderer(device, context);
atelier::material::Initialize("MaterialLibrary.amat");

// Dans une frame Dear ImGui :
int workspace = atelier::material::Draw();
// 0 = Material Studio, 1 = Scene, 2 = Graph.

// Avant destruction des backends et de l'appareil :
atelier::material::Shutdown();
atelier::material::ShutdownRenderer();
```

`MaterialLibrary::BeginEdit()` / `EndEdit()` regroupent des modifications directes en une opération d'annulation. Ne pas conserver un pointeur `Material*` à travers un ajout, une suppression, un chargement ou une annulation.

`GetLibrary()` expose la bibliothèque en lecture et `GetPreviewSettings()` les réglages de l'aperçu. `GetAppliedMaterial()` donne la dernière copie explicitement appliquée. `SetApplyCallback()` permet de transmettre cette copie à votre système de matériaux :

```cpp
atelier::material::SetApplyCallback(
    [](const atelier::material::Material& material, void* user) {
        // Convertir material vers les paramètres du moteur hôte.
    });
```

## Capture automatisée

```powershell
.\dist\Atelier-ImGui\Atelier.exe --materials --width 1600 --height 900 --screenshot .\outputs\material-studio.png --frames 20
```

La capture est enregistrée à la dixième frame et l'application se ferme après le nombre de frames demandé.

## Vérifier le modèle

```powershell
.\dist\Atelier-ImGui\Atelier.exe --material-test --frames 20
```

Les 31 contrôles du modèle vérifient les presets, les IDs, la sélection, l'historique, la duplication, la suppression, la restauration des presets, la sauvegarde, les valeurs non finies, les fichiers invalides et les limites de prévisualisation. Ils ne constituent pas une validation automatique de toutes les interactions à la souris ou du rendu.

Le rapport du modèle est `outputs/material-model-test.json`, dans le dossier de travail. Le même lancement produit `outputs/material-render-test.json` et cinq exports GPU dans `outputs/material-tests/` : cuivre lisse, cuivre rugueux, céramique bleue, cuivre sur torus et normales sur cube. Le rapport de rendu indique le succès des exports ; comparer les images permet de vérifier la réponse visuelle aux variantes.
