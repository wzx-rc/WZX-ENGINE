# Intégration de l’inspecteur autonome

Cette livraison reprend la variante **Inspecteur** approuvée : un panneau de 318 px, avec trois onglets **Properties**, **Stage** et **Texture**. Elle contient ses propres données, widgets et glyphes. Le module ne dépend pas des anciens éditeurs Scene, Graph ou Material Studio.

Le panneau édite un document local. Stage conserve les réglages de lumière et de caméra ; Texture conserve un chemin et ses réglages. Ces onglets n’exécutent aucun moteur 3D. Le symbole de matériau et la vignette de texture sont décoratifs ; aucune image de texture n’est décodée.

## API C++ réellement fournie

L’API se trouve dans [`../src/inspector.h`](../src/inspector.h), dans `atelier::inspector`.

| Élément | Usage |
|---|---|
| `Document` | Valeurs du matériau, du stage et du chemin de texture. |
| `InspectorState` | Document courant, valeurs initiales, onglet, recherche, sections, historique et brouillon couleur. |
| `ApplyTheme(float scale = 1.f)` | Configure le style ImGui ; appeler après la création du contexte. |
| `DrawInspector(InspectorState&, float clientWidth, float clientHeight)` | Dessine la surface ImGui et son panneau centré ; appeler une fois par frame. |
| `ScalarSpecs()` | Identifiants, bornes, précision et unités des 23 propriétés scalaires. |
| `Clamp(Document&)` | Normalise les valeurs, les enums et les tableaux. |
| `Equal(const Document&, const Document&)` | Compare toutes les valeurs éditées. |
| `ToJson` / `FromJson` | Sérialisation du document ; les erreurs de parsing sont retournées sans modifier la valeur de sortie. |
| `ParseHex` / `Hex` | Conversion RGB `#RRGGBB` ; `ParseHex` conserve l’alpha existant. |

Le module utilise Dear ImGui et C++17. Le choix de fichier fourni dans `inspector.cpp` utilise Windows `GetOpenFileNameW` ; l’hôte Windows doit lier `comdlg32`. Le backend de fenêtre et de rendu ImGui appartient à l’hôte.

```cpp
#include "inspector.h"
#include "imgui.h"

atelier::inspector::InspectorState inspector;

// Après ImGui::CreateContext(), et le chargement de la police par l’hôte :
atelier::inspector::ApplyTheme();

// Dans la boucle, entre ImGui::NewFrame() et ImGui::Render() :
const auto before = inspector.value;
const ImVec2 client = ImGui::GetIO().DisplaySize;
atelier::inspector::DrawInspector(inspector, client.x, client.y);
if (!atelier::inspector::Equal(before, inspector.value)) {
    // Appeler ici le callback de votre application, si nécessaire.
    // Les valeurs sont disponibles dans inspector.value.
}
```

Le header ne fournit pas de registre de callbacks ni de connexion moteur. L’exemple montre l’endroit où l’hôte peut appeler **son propre** callback. Un changement pendant le glissement peut servir d’aperçu local ; `InspectorState` regroupe le geste dans son historique à la fin de l’édition. Une intégration dans une fenêtre dockée devra adapter le placement de la surface racine : `DrawInspector` utilise actuellement les coordonnées de la zone cliente principale.

Les dimensions du contrat sont en pixels logiques à l’échelle 1. `ApplyTheme(scale)` ajuste le style, les primitives, la typographie et les traits. `DrawInspector` reçoit les dimensions physiques de la zone cliente et les convertit en coordonnées logiques. L’hôte charge une police adaptée à son échelle ; les `ControlRect` exposés restent en pixels physiques.

## Valeurs et transactions

`Document.baseColor` et `emissionColor` contiennent des floats RGB normalisés de 0 à 1, issus des valeurs d’affichage sRGB. Le moteur hôte applique sa propre conversion vers son espace linéaire. L’alpha de Base Color est la quatrième composante, également de 0 à 1 ; il reste distinct de `Document.opacity`. L’alpha d’Emission Color est normalisé à 1.

Les enums sont stockés sous forme d’entiers : shader `0..3` = Opaque, Cutout, Transparent, Unlit ; pattern `0..5` = Smooth, Brushed Metal, Ancient Stone, Wood Grain, Checker, Woven Fabric ; environnement `0..2` = Studio, Sunset, Warehouse ; canal de texture `0..4` = RGB, Red, Green, Blue, Alpha. Ce sont des données, pas des programmes de shader compilés.

Pour une modification depuis l’hôte, utiliser les mêmes transactions :

```cpp
const auto before = inspector.value;
inspector.value.roughness = 0.64f;
inspector.Commit(before); // Clamp, historique et suppression de Redo.

inspector.Reset("roughness"); // Une propriété depuis inspector.defaults.
inspector.Reset("all");       // Le document complet depuis inspector.defaults.
inspector.Undo();
inspector.Redo();
```

`BeginEdit(before)` puis `EndEdit()` regroupent les changements continus. `Commit(before)` crée une entrée uniquement si le document a changé. `defaults` définit les valeurs de référence de Reset et des indicateurs de modification ; charger un document ne remplace pas automatiquement cette référence.

Les champs numériques et les sliders partagent la même valeur et les bornes de `ScalarSpecs()`. Les axes U/V sont indépendants. Opacity est désactivé pour Opaque et Unlit ; sa valeur reste conservée. Texture strength devient actif lorsque le chemin est renseigné et la texture activée. Les resets ne sont actifs que pour les valeurs modifiées.

## Sélecteur de couleur et texture

`OpenPicker(0)` ouvre Base Color, `OpenPicker(1)` ouvre Emission Color. Le sélecteur édite `pickerDraft` ; la couleur du document reste intacte jusqu’à `ApplyPicker()`. `CancelPicker()` ferme et annule le brouillon. RGB/HSV change la représentation, la palette conserve l’alpha, et Original restaure le brouillon initial. L’application d’une couleur crée une seule entrée Undo.

Les valeurs internes `pickerHSV` sont normalisées : H, S et V sont dans `0..1`. L’interface affiche H en degrés et S/V en pourcentage. RGB s’affiche dans `0..255` et l’alpha dans `0..100 %`. Le popup est contraint à la zone cliente ; son contenu peut défiler lorsque la hauteur est réduite, avec Apply et Cancel fixes.

Choose Texture ouvre un vrai sélecteur Windows, convertit le chemin en UTF-8, renseigne `texturePath` et active `textureEnabled`. Clear vide le chemin et désactive la texture. Le filtre du dialogue accepte notamment PNG, JPG, BMP, TGA, TIFF, EXR, HDR et DDS : cette sélection ne garantit aucun décodage de ces formats, car le module ne charge pas l’image.

## Persistance et identifiants

`Save(path)` et `Load(path)` utilisent un chemin UTF-8. Le format de document est `{ "format": "atelier-inspector", "version": 1, "values": { ... } }`, avec nombres, booléens, chaînes et tableaux. Les tableaux couleur comportent quatre composantes, les UV deux. Les erreurs apparaissent dans `InspectorState.status`. L’écriture passe par un fichier temporaire avant remplacement du fichier existant.

L’hôte fourni expose Ctrl+S pour enregistrer, Ctrl+Shift+S pour choisir une destination, Ctrl+O pour ouvrir, Ctrl+Z/Ctrl+Y pour l’historique. Les shortcuts sont gérés par l’hôte ; une application qui reprend uniquement le composant doit les raccorder elle-même.

`state.controls` contient les rectangles des contrôles dessinés durant la frame, en coordonnées cliente. `FindControl(id)` retourne un pointeur utilisable jusqu’à la prochaine modification de cette liste. Les identifiants incluent `tab.0`, `tab.1`, `tab.2`, `roughness.value`, `roughness.slider`, `roughness.reset`, `baseColor.swatch`, `picker.apply`, `picker.cancel`, `texture.choose`, `undo` et `redo`. Les contrôles cachés ou non dessinés peuvent être absents de cette liste.

## Contrat de design livré

- [`../design/theme.json`](../design/theme.json) : couleurs, typographie, dimensions et états.
- [`../design/layouts/inspector.json`](../design/layouts/inspector.json) : rectangle de chaque composant, parent, binding, icône, action et état actif ; référence de 342 × 920 px avec marge 12 px.
- [`../design/actions.json`](../design/actions.json) : IDs sémantiques fixes et opérations locales correspondantes.
- [`../design/icons/catalog.json`](../design/icons/catalog.json) et les SVG nommés : les douze glyphes de `inspector_icons.h`, avec leur géométrie normalisée.

**Le code natif ne lit ni ne charge ces JSON de design.** Ils servent de contrat d’intégration pour reproduire la disposition et mapper les actions. Un adaptateur qui souhaite les interpréter devra être implémenté par l’hôte. Le JSON du document produit par `ToJson` est un format distinct.

Les coordonnées des composants sont relatives à leur parent. Les sections sont dans l’espace de contenu du scroll, puis repositionnées lors de l’expansion ou de la recherche. La recherche native filtre les sections grâce à leur titre et leur liste de mots-clés ; elle affiche tous les contrôles d’une section correspondante. L’onglet actif et les sections constituent un état d’affichage indépendant du document.

La police native fournie est Segoe UI Regular, à 400 ; la proposition demandait 500 pour certains titres. Les tailles et espacements sont conservés, mais cette graisse reste une approximation native. Les SVG et ImGui partagent les primitives des glyphes ; leur antialiasing et les cercles polygonaux natifs peuvent différer légèrement. Le damier utilise des carrés de 4 px, soit un cycle de 8 px.
