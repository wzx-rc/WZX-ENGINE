# Mémoire du projet : prototype RTS « village » (Unity puis Unreal)

> Ce document décrit le prototype fait dans Unity et Unreal **avant** WZX Engine ; il sert de cahier des charges au jeu RTS. L'état du moteur lui-même est dans le `README.md` et les rapports `RAPPORT_DDGI.md` et `RAPPORT_RTS.md` (jalon `v0.1` du 14/10/2026 : DDGI, éditeur nodal et RTS fusionnés dans `main`).

Fichier généré à la fin de la session du 12/10/2026. Il résume tout ce qui a été fait, les choix techniques, les pièges rencontrés et comment reprendre le travail.

---

## 1. Objectif

Prototype de gestion type RTS : une grille au sol, des maisons que l'on sélectionne et déplace, un bouton pour en ajouter, une lumière réglable, une caméra RTS. Réalisé d'abord dans **Unity**, puis refait à l'identique dans **Unreal Engine 5.8** (cible : build Windows).

---

## 2. Environnements

| | Unity | Unreal |
|---|---|---|
| Projet | `C:\Users\wzx\Dropbox\BRASSART 2027\02 - WORKSHOPS\HADES\UNITY\My project` | `C:\Users\wzx\Desktop\MyProject` |
| Version | Unity 6000.6.3f1, URP | Unreal Engine 5.8 |
| Pilotage par Claude | Plugin Unity + CLI `unity` (package `com.unity.pipeline` ajouté au projet) | Plugin « Unreal Engine Skills » + serveur MCP (`.mcp.json` à la racine du projet, `http://127.0.0.1:8000/mcp`) |
| Démarrage de la connexion | Éditeur ouvert, `unity status` doit afficher `ready` | Dans Unreal : console `ModelContextProtocol.StartServer` (ou option « Auto Start Server »), puis relancer Claude Code |

Notes d'environnement :
- Unity est resté en cible de build **WebGL** (je l'avais basculé pour le test iPad). Pour revenir à Windows : `unity command switch_build_target --target StandaloneWindows64 --confirm true` (réimport des assets, quelques minutes).
- Le module iOS n'est pas installé et un build iOS est impossible depuis Windows (Mac + Xcode requis).
- Les « Enter Play Mode Options » d'Unity désactivent le rechargement du domaine : les caches statiques doivent être purgés au lancement (déjà géré dans le code).

---

## 3. Unity : ce qui a été réalisé

### Scène (`Assets/Scenes/SampleScene.unity`)
- Sol (Plane 20×20 m, matériau vert), grille en surcouche, cube rouge avec Rigidbody.
- 3 maisons « cozy cottage » rendues cliquables.
- `CameraRig` (la Main Camera est son enfant), `Grid` (GridManager), `UICanvas` (bouton d'ajout + cadran de lumière), `EventSystem`.
- Le plan `r` (grand plan préexistant) a été descendu de 2 cm pour éviter le z-fighting avec le sol.

### Scripts (`Assets/Scripts/`)
| Script | Rôle |
|---|---|
| `RTSCameraController` | ZQSD/flèches, Maj = rapide, A/E ou clic molette = rotation, molette = zoom, clic droit = glisser la vue, bords d'écran. Tactile : 1 doigt glisse, pincer = zoom, torsion = rotation. Bloquée pendant l'usage du cadran. |
| `ClickSelector` | Souris et doigt (`Pointer.current`) : survol, sélection, glisser sur la grille. Maison invalide = retour à l'origine. |
| `Clickable` | Outline (coque sur calque 31), animation : flottement, inclinaison, rebond et écrasement à la dépose. |
| `GridManager` | Occupation de la grille (cases de 1 m), emprise déduite de la bounding box. |
| `HouseSpawner` + `SpawnButton` | Clic = la maison tombe au centre ; glisser depuis le bouton = on la tient et on la dépose. Bouton animé (flottement, rebond). |
| `LightDial` | Cadran 360° pour la lumière directionnelle, relatif à l'orientation de la caméra. |
| `OutlinePass` | Outline en espace écran (voir ci-dessous). |

### Outline (définitive)
Première version (coque gonflée) abandonnée : elle faisait des « boules ». Version finale : une caméra de masque rend la silhouette des objets surlignés dans une RenderTexture ; un shader plein écran dessine un trait net + un halo autour (échantillonnage en anneaux). Shaders : `Assets/Shaders/OutlineMask.shader`, `OutlineComposite.shader`, `GridOverlay.shader`. Couleurs : cyan = survol, vert = sélection, rouge = placement invalide.

### Prefab
`Assets/Prefabs/House.prefab` (utilisé par le bouton d'ajout). Les OBJ des maisons ont « Read/Write » activé (hérité de l'ancienne outline, plus nécessaire : à désactiver pour alléger).

### Build WebGL / test iPad
- Build réalisé (≈ 150 Mo, ~6,5 min la première fois ; `data` dominé par 2 meshes OBJ de 66 Mo chacun).
- Serveur de test local : script PowerShell `serve.ps1` (port 8080). Ouvrir `http://<IP du PC>:8080` dans Safari, même Wi-Fi.
- Risque : mémoire de Safari iPad avec ces meshes. Améliorations proposées : Read/Write désactivé, compression des meshes, colliders simples, retrait du package `com.unity.ai.inference` si inutile.
- Multi-plateformes : un seul projet, on change la cible au build. iOS exige un Mac/Xcode (ou service cloud) et un compte Apple Developer.

---

## 4. Unreal : ce qui a été réalisé

### Contenu créé (`/Game/…`)
| Asset | Rôle |
|---|---|
| `Blueprints/BP_House` | Maison : composants `Visual` (animation), `Mesh` (House01, échelle 3,5, roll 90°), `Hit` (boîte de collision). Variables d'animation, grille (`GridX/GridY`, emprise 2×2), rotation (`RotTarget/RotCur`). Fonctions : `SetTarget`, `DropIn`, `ApplyHovered/Selected/Invalid/Lifted`, `UpdateHighlight`, `RotateStep`. |
| `Blueprints/BP_RTSPawn` | Caméra RTS (SpringArm + Camera, inclinaison -55°). ZQSD/flèches, A/E rotation, molette zoom, clic droit glisser, clic molette tourner, bords d'écran. Variable `Blocked` pilotée par le cadran. |
| `Blueprints/BP_RTSController` | Cerveau : survol, sélection, glisser sur la grille, mode « maison attachée à la souris », touche R, création du HUD. Fonctions : `GroundPoint`, `SnapGrid`, `Center`, `CanPlace`, `FindFree`, `SelectHouse`, `BeginDrag/UpdateDrag/EndDrag`, `BeginSpawnDrag`, `RotateSelected`, etc. |
| `Blueprints/BP_RTSGameMode` | Pawn et contrôleur par défaut. Défini dans les World Settings du niveau. |
| `UI/WBP_HUD` | Bouton « + Ajouter une maison » (animé), bouton « Tourner 45° (R) », cadran de lumière. |
| `Materials/M_Outline` | Post-process : outline depuis le Custom Stencil (trait + halo). |
| `Materials/M_Grid`, `M_Grass`, `M_Red` | Grille (surcouche), sol, cube. |
| `Main` (niveau) | Sol 8000×8000, grille 30×30 cases de 100 cm, `PostProcessVolume_0` (unbound, `M_Outline`), 2 `BP_House`. |

Le cube rouge physique avait été créé puis a disparu de la scène (supprimé par l'utilisateur ou perdu) : à recréer si besoin.

### Réglages de projet modifiés
- Rendu : **Custom Depth-Stencil = Enabled with Stencil** (nécessaire à l'outline).
- Input : `bAlwaysShowTouchInterface = false` (avant, joysticks virtuels style iPhone + clics souris cassés), `DefaultViewportMouseCaptureMode = NoCapture`, `DefaultViewportMouseLockMode = DoNotLock`, `bCaptureMouseOnLaunch = false`.
- Mode d'entrée forcé « Jeu + UI » au démarrage du HUD.

### Commandes de jeu (Unreal)
- Survol = outline cyan ; clic = sélection (vert) + glisser sur la grille ; rouge si invalide ; rebond à la dépose.
- Bouton « + Ajouter une maison » : clic = la maison suit la souris, clic sur la grille pour la poser, clic droit/Échap/clic UI pour annuler. Glisser-déposer direct toujours possible.
- **R** ou bouton = rotation de 45° de la maison sélectionnée.
- Cadran : fait tourner la lumière à 360° et bloque la caméra pendant l'usage.

---

## 5. Pièges rencontrés (à ne pas reproduire)

### Unity
- Une coque gonflée donne des boules sur des meshes irréguliers → préférer un masque + shader plein écran.
- Domain reload désactivé : les statiques gardent des objets détruits entre deux Play.
- Instabilité d'un ressort intégré à pas variable : borner le pas de temps.
- L'éditeur en tâche de fond n'avance pas son Play : tester la logique en appelant `Update` à la main.

### Unreal
- **Évaluation paresseuse des nœuds « purs »** : une valeur calculée par un nœud pur est recalculée à chaque utilisation, donc après les modifications de variables. Exemple fatal : `clic = lmb ET NON précédent` recalculé après la mise à jour de `précédent` donnait toujours faux. Toujours stocker le résultat dans une variable avant d'écraser l'état dont il dépend (variables `PressedNow`, `RotNow`, `Tmp*`).
- Un bouton UI qui traite un clic ne le transmet pas au jeu (`IsInputKeyDown` reste faux) : utiliser `OnPressed/OnReleased` du bouton.
- Un widget plein écran « Visible » intercepte les clics : mettre la racine en `SelfHitTestInvisible`.
- Changer `CollisionProfileName` via les propriétés ne recalcule pas les réponses : appeler `SetCollisionProfileName` au `BeginPlay`.
- `CreateWidget` dans le `BeginPlay` du PlayerController échoue (« no Player attached ») : créer le HUD dans le Tick, quand le contrôleur local est prêt.
- Noms de fonctions identiques à ceux de variables ou d'autres classes (`SetHovered`, `ClearSelection`…) : ambiguïté, d'où `ApplyHovered`, etc. La relecture DSL affiche parfois un autre nom de classe, sans que le nœud soit faux.
- Post-process translucide : le fond devient noir. Mélanger soi-même avec `PostProcessInput0` en mode Opaque.
- Post-process avec `SceneTextureLookup` : il faut au moins un nœud `SceneTexture` relié pour que le compilateur inclue les fonctions.
- `write_graph_dsl` remplace les événements présents dans le script mais ne supprime pas les anciens nœuds d'un événement vidé : les supprimer avec `delete_node`.
- Les captures d'écran de l'outil d'Unreal doivent être faites avec une caméra explicite (`captureTransform`) ; attendre la fin de la compilation des shaders (≈ 20 à 30 s) avant de capturer.

---

## 6. Comment reprendre

**Unity** : ouvrir le projet, vérifier `unity status` (état `ready`), `unity command` liste les commandes. Les scripts sont dans `Assets/Scripts/`.

**Unreal** : ouvrir `MyProject.uproject`, lancer `ModelContextProtocol.StartServer` dans la console, relancer Claude Code sur le dossier du projet. Utiliser les outils `unreal-mcp` (`list_toolsets`, `describe_toolset`, `call_tool`) ; les graphes Blueprint s'écrivent en DSL via `BlueprintTools.write_graph_dsl`.

### Prochaines étapes possibles
1. **Build Windows** d'Unreal (non fait : validation du comportement en Play demandée d'abord).
2. Recréer le cube rouge physique dans Unreal si souhaité.
3. Remettre Unity en cible Windows ; alléger les meshes OBJ (décimation, Read/Write off, colliders simples) pour un test iPad fiable.
4. Test sur iPad d'Unreal via Pixel Streaming (plugin déjà activé dans le projet) : build Web impossible avec Unreal, iOS impossible sans Mac.
5. Éventuellement : annuler la dernière dépose, sauvegarde de la disposition, plus de types de bâtiments.
