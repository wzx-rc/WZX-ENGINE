# Intégration dans un projet Dear ImGui

L'interface est séparée du lanceur Windows. Les fichiers à reprendre sont `src/editor_ui.h`, `src/editor_ui.cpp` et `src/ui_icons.h`. Le module dessine l'éditeur dans la fenêtre Dear ImGui principale et contient son état de démonstration.

## Interface publique

```cpp
#include "editor_ui.h"

// Choisir explicitement un fichier accessible en écriture :
atelier::SetSceneFile("mon_projet.atelier");

// Après la création du contexte ImGui et le chargement des textures GPU :
atelier::Texture reference{ reference_texture_handle, 1280, 720 };
atelier::Texture viewport{ clean_viewport_texture_handle, 1672, 941 };
atelier::Initialize(reference, viewport);

// Le second argument est facultatif : Initialize(reference) reste valide.

// Dans chaque frame, après les NewFrame du backend et ImGui::NewFrame() :
atelier::Draw();

// Après un picking réalisé par votre moteur :
// atelier::SetSelection(picked_actor_id);

// Lire les valeurs éditées dans cette frame :
const atelier::SceneState state = atelier::GetState();
for (const atelier::ActorState& actor : state.actors) {
    if (actor.folder)
        continue;
    // Appliquer actor.position, actor.rotation, actor.scale,
    // actor.meshIndex, actor.materials et les propriétés au moteur.
}

// Lors de la fermeture, avant de détruire le contexte ImGui :
atelier::Shutdown();
```

`Texture::id` doit être le handle de texture attendu par le backend utilisé. Dans le lanceur Direct3D 11 livré, il s'agit d'un `ID3D11ShaderResourceView*`. Le code appelant conserve la propriété des textures et doit les garder valides pendant toute l'utilisation de l'interface.

Les dimensions doivent correspondre aux textures chargées et être strictement positives. `assets/reference.jpg` fournit les miniatures. Le second argument fournit une image de viewport facultative ; le lanceur charge `assets/viewport-clean.png`, une variante du concept où la barre, les statistiques et les poignées de transformation intégrées dans l'image ont été retirées. Sans ce second argument, le viewport utilise la photographie d'origine.

Les deux images gardent la disposition du concept complet. `Viewport()` échantillonne la zone de scène par des coordonnées UV normalisées selon `reference.width` et `reference.height`. Le module ne reconstruit pas la scène 3D.

## Boucle de rendu

Conserver la boucle et le backend du projet existant. L'ordre est le suivant :

1. Traiter les événements de la fenêtre.
2. Appeler `NewFrame()` pour les backends plateforme et rendu.
3. Appeler `ImGui::NewFrame()`.
4. Appeler `atelier::Draw()`.
5. Lire `atelier::GetState()` et transmettre l'état à l'application hôte.
6. Appeler `ImGui::Render()` puis transmettre les draw data au backend.

`src/main.cpp` constitue l'exemple complet pour Win32 et Direct3D 11. Le chargement JPEG y est assuré par Windows Imaging Component (WIC).

## Adaptation au moteur

`GetState()` retourne une copie de l'état courant. `SceneState` contient le nom de scène, l'ID sélectionné, les paramètres d'éclairage/monde, l'état de lecture et la liste des `ActorState`. Chaque acteur expose son ID, son parent, sa transformation, ses indices de mesh et de matériaux, ses propriétés, composants et tags. Les entrées de dossier portent `folder == true`.

L'application peut consommer cet état après chaque `Draw()` pour créer, mettre à jour ou retirer ses objets, en comparant leurs IDs. Les valeurs retournées sont une photographie de l'état : modifier cette copie ne modifie pas l'interface. `SetSelection(actorId)` transmet une sélection de l'application à l'éditeur et retourne `false` si l'ID est inconnu.

Pour afficher une render target du moteur, fournir son handle comme texture de viewport et adapter l'appel à `AddImage()` dans `Viewport()` : une texture contenant uniquement la vue 3D doit utiliser des UV allant de `(0, 0)` à `(1, 1)`, au lieu du recadrage du concept. Le zoom et le pan de démonstration deviennent ensuite des commandes de la caméra du moteur.

Pour personnaliser le modèle ou les commandes, les points d'adaptation dans les sources sont :

- `Node`, `nodes` et `Current()` pour la sélection et les données de scène.
- `AddAsset()`, `Duplicate()` et `Delete()` pour les commandes de scène.
- `AxisFields()` et `InspectorActor()` pour personnaliser les propriétés affichées.
- `Viewport()` pour afficher une render target et connecter le gizmo à la caméra et aux objets.
- `Asset`, `assets` et `Thumb()` pour alimenter le navigateur et les miniatures.
- `Save()` et `Load()` pour remplacer le format de démonstration par celui du projet.

`Capture()`, `Remember()`, `Undo()` et `Redo()` gèrent l'historique des acteurs, y compris le prochain ID. Les paramètres d'éclairage et de monde ont leur propre état de démonstration. Les options de collision et de physique sont des valeurs éditables exposées par `GetState()` ; elles doivent être reliées aux systèmes correspondants du moteur.

`Draw()` occupe la zone principale, en utilisant `ImGuiIO::DisplaySize` et une fenêtre racine placée à `(0, 0)`. Les séparateurs redimensionnent ses panneaux. Pour l'héberger dans une fenêtre ou un dock existant, adapter cette origine et la taille disponible dans `Draw()`.

`Initialize()` applique le thème sombre au contexte ImGui. Il utilise Segoe UI depuis Windows, avec la police par défaut d'ImGui en secours. Dans un projet qui possède déjà son thème ou ses polices, adapter cette initialisation. Appeler `Initialize()` une seule fois avant la boucle de frames. La sauvegarde de la scène est manuelle ; `Shutdown()` ne libère pas la texture de l'appelant et n'enregistre pas la scène.

## Dépendances

Le projet inclut Dear ImGui **v1.92.9b-docking** et sa licence MIT dans `vendor/imgui/`. L'interface est écrite en C++17 et utilise les contrôles Dear ImGui ainsi que ses draw lists pour les icônes et l'habillage. Le chargement WIC et Direct3D 11 appartiennent au lanceur Windows ; ils peuvent être remplacés par les fonctions équivalentes de l'application hôte.

Les libellés anglais sont conservés pour correspondre au concept fourni.
