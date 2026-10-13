# Atelier — éditeur de graphes Dear ImGui

L’éditeur reprend les gris sombres, les séparateurs fins, les icônes vectorielles et l’accent cyan du concept Ancient Coast. Les en-têtes des nœuds indiquent leur famille par une couleur et une icône ; les broches et les connexions utilisent leur propre couleur de type.

## Lancer et naviguer

Lancer `launch.cmd`, puis cliquer **Graph**, ou démarrer `dist/Atelier-ImGui/Atelier.exe --graph`. Le studio de matériaux est désormais l'espace par défaut. Les boutons **Scene**, **Graph** et **Materials** changent d'espace ; l'option `--scene` démarre directement sur la scène.

Le panneau **My Blueprint** propose deux documents indépendants : **Event Graph** et **Update Graph**. Chacun conserve ses nœuds, ses connexions et sa vue pendant la session. Les séparateurs ajustent la bibliothèque, l’inspecteur et le panneau de sortie.

## Construire un graphe

- Rechercher un nœud dans **Node Library**, filtrer sa catégorie, puis cliquer son entrée ou la glisser sur le canvas. Le clic droit sur le canvas ouvre aussi une palette avec recherche.
- Cliquer une carte pour la sélectionner et la glisser pour la déplacer. `Ctrl+clic` ajoute ou retire un nœud de la sélection. Glisser sur une zone vide dessine un rectangle de sélection.
- Glisser d’une broche de sortie à une entrée de même type pour créer une connexion. Une entrée reçoit une seule connexion ; une sortie d’exécution mène à une destination. **Sequence** permet de distribuer l’exécution. Les sorties de données peuvent alimenter plusieurs nœuds.
- `Alt+clic` sur une broche détache ses connexions. `Alt+clic` sur un fil détache ce fil. Cliquer un fil, puis `Suppr`, ou utiliser son menu contextuel, le retire également.
- Glisser une broche vers une zone vide ouvre la palette ; le nœud choisi se connecte automatiquement lorsqu’une broche compatible existe.
- Modifier le nom, l’activation, la position et les valeurs du nœud dans **Inspector**. Les valeurs locales servent de défaut lorsqu’une entrée n’est pas reliée.
- Ajouter un commentaire avec `C`. Sélectionner son bandeau pour modifier son titre, sa taille et sa couleur dans l’inspecteur ; glisser le bandeau pour le déplacer.

La molette zoome autour du curseur. Glisser avec le bouton central ou droit déplace la vue. La mini-carte permet de recentrer le canvas. L’aimant active un pas de 20 unités pour déplacer les nœuds ; **Auto arrange** réorganise les cartes et retire les commentaires existants.

## Couleurs

| Famille de nœud | Couleur | Exemples |
|---|---|---|
| Events | Rose | Begin Play, Event Tick |
| Input | Cyan | Keyboard Input |
| Flow Control | Orange | Branch, Delay, Sequence, Print String |
| Math | Vert | Float, Boolean, Vector 3, Add, Multiply, Greater Than, Lerp |
| Scene | Bleu | Get Actor, Set Position, Rotate Actor |
| Rendering | Violet | Set Visibility, Set Material, Spawn Effect, Material |
| Audio | Jaune | Play Sound |
| Variables | Mauve | Get Variable, Set Variable, String |

| Broche | Couleur |
|---|---|
| Execution | Blanc, triangle |
| Float | Vert |
| Boolean | Rose |
| Vector 3 | Jaune |
| Entity | Bleu |
| Material | Violet |
| String | Mauve |

## Raccourcis

Les raccourcis du graphe s’appliquent hors des champs de texte et des menus ouverts.

| Commande | Action |
|---|---|
| `Ctrl+S` | Enregistrer le document actif |
| `Ctrl+O` | Recharger le fichier du document actif |
| `Ctrl+N` | Ouvrir le dialogue de nouveau graphe ou de restauration de la démo |
| `Ctrl+Z` / `Ctrl+Y` | Annuler / rétablir les nœuds, leurs propriétés et leurs connexions |
| `Ctrl+A` | Sélectionner tous les nœuds |
| `Ctrl+C` / `Ctrl+V` | Copier / coller les nœuds et leurs connexions internes |
| `Ctrl+D` | Dupliquer la sélection et ses connexions internes |
| `Suppr` / `Retour arrière` | Supprimer la sélection |
| `Home` | Cadrer tout le graphe |
| `F` | Cadrer la sélection, ou tout le graphe si elle est vide |
| `C` | Ajouter un commentaire autour de la sélection |
| `Échap` | Annuler la création d’un lien et vider la sélection |
| `Ctrl+B` | Valider le graphe avec **Compile** |
| `Ctrl+Entrée` | Exécuter l’aperçu avec **Run** |
| `F1` | Afficher l’aide intégrée |
| `F12` | Enregistrer une capture PNG du lanceur natif |

## Enregistrer

L’enregistrement est manuel et porte sur le document actif. Le menu **Project** affiche son chemin. Dans la distribution :

- `BP_AncientCoast.agraph` conserve **Event Graph**.
- `BP_AncientCoast_Update.agraph` conserve **Update Graph**.
- Le fichier `.agraph.layout` associé conserve le cadrage et les commentaires.

Le format `.agraph` versionné conserve les IDs, types, titres, paramètres, positions, activation et liens des nœuds. L’écriture du modèle utilise un fichier temporaire avant remplacement du fichier précédent ; un chargement invalide conserve le graphe courant. Les connexions incompatibles et les références de broches invalides sont rejetées.

Au lancement, les graphes de démonstration sont initialisés. `Ctrl+O` recharge ensuite la sauvegarde du document actif. Pour transporter aussi les commentaires et le cadrage, copier le `.agraph` avec son `.agraph.layout`. Les commentaires et la disposition de la vue ne font pas partie de l’historique des nœuds.

## Validation et aperçu

**Compile** vérifie les connexions, les types et les cycles. Les diagnostics apparaissent dans **Output** et **Compiler Results** ; cliquer un diagnostic lié à un nœud cadre ce nœud.

**Run** évalue les chemins d’exécution connectés, les opérations mathématiques, les conditions et les variables numériques. La démo Event Graph initialise la position d’un acteur de démonstration unique, puis choisit une branche selon `RotationSpeed`. La voie vraie produit une rotation de 33,75° et une requête audio ; la voie fausse produit une requête VFX. Les résultats et l’ordre des nœuds parcourus sont visibles dans **Output** et **Execution Trace**.

Le bouton **Step** parcourt la trace de la dernière exécution. L’aperçu part des événements **Begin Play** activés ; lorsqu’il n’en existe aucun, il simule une fois les événements **Event Tick** et **Keyboard Input** activés. **Delay** consigne une durée simulée sans attendre. Audio et VFX produisent des messages et des requêtes dans le résultat. Ils nécessitent une connexion aux systèmes du moteur pour jouer un son ou afficher un effet. L’aperçu manipule un acteur de démonstration et des variables temporaires ; il ne simule pas la scène 3D existante.

## Réutiliser le module

Le canvas et les contrôles utilisent Dear ImGui et ses draw lists. Les fichiers du module sont `src/graph_ui.h`, `src/graph_ui.cpp`, `src/graph_model.h`, `src/graph_model.cpp` et `src/ui_icons.h`. Le modèle de graphe ne dépend pas d’ImGui.

```cpp
#include "graph_ui.h"

// Après création du contexte ImGui et initialisation du thème/police :
atelier::graph::Initialize("BP_MonProjet.agraph");

// Dans une frame ImGui :
const bool sceneRequested = atelier::graph::Draw();
const atelier::graph::Graph& graph = atelier::graph::GetGraph();
const atelier::graph::RunResult& preview = atelier::graph::GetLastRun();

// Consommer les nœuds et connexions dans votre compilateur/runtime.
// preview contient notamment actorPosition, actorYaw, actorVisible,
// actorMaterial, effects, logs et executedNodes.

// Avant destruction du contexte ImGui :
atelier::graph::Shutdown();
```

`Draw()` occupe la zone principale ; il retourne `true` lorsque **Scene** est demandé. `GetGraph()` et `GetLastRun()` exposent des références en lecture. Pour intégrer vos propres nœuds, étendre `NodeType`, les `NodeSpec`, leurs icônes, leurs propriétés et le compilateur/runtime de l’hôte. `Graph::BeginEdit()` / `EndEdit()` groupent les mutations en une opération d’historique ; ne pas conserver de pointeur `Node*` à travers une insertion, une suppression, un chargement ou une annulation.

## Vérifier le modèle

```powershell
.\dist\Atelier-ImGui\Atelier.exe --graph-test --frames 20
```

Le rapport `outputs/graph-model-test.json`, dans le dossier de travail, vérifie les données et l’exécution simulée : types et cardinalités, historique, duplication, suppression des liens, IDs, sauvegarde/chargement, cycles, branches, variables et budgets d’exécution. Il ne constitue pas un test automatique de toutes les interactions à la souris.
