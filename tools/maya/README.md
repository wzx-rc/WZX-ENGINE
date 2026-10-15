# WZX Shader Graph pour Maya (prototype)

Le graph de nœuds du moteur (onglet Graph, style Atelier, thèmes) dans un panneau Maya dockable, pour monter des
matériaux Arnold. Chaque nœud du graph pilote un nœud Maya / Arnold existant (aucun shader réimplémenté) :

| Graph | Maya / Arnold |
|---|---|
| Material Output | shadingEngine (`<nom>SG`) |
| aiStandardSurface, aiMixShader, aiFlat | mêmes nœuds |
| Ramp, Checker | `ramp`, `checker` + place2dTexture |
| aiNoise, aiImage | mêmes nœuds |
| aiColorCorrect, Blend Colors, Multiply, aiBump2d | `aiColorCorrect`, `blendColors`, `multiplyDivide`, `aiBump2d` |
| Color / Float Constant | `colorConstant`, `floatConstant` (lookdevKit) |

## Installer

1. `build.bat` (une fois, moteur et shaders), puis `tools\maya\build_shader_graph.bat` → `build\mayashader\WzxShaderGraph.exe`.
2. Glisser `tools\maya\install_wzx_shader.mel` dans le viewport de Maya : bouton **WZX Shader Graph** sur l'étagère `WZX_Shader`.

## Utiliser

- Le bouton ouvre le panneau : liste des graphs de la scène, **New**, **Restart**, puis l'éditeur.
- Backdrops : Ctrl+G (ou le bouton « + Backdrop ») encadre les nœuds sélectionnés ; glisser le titre déplace le cadre et ses nœuds, double clic renomme, coin bas-droit redimensionne, couleur dans l.Inspector.
- Engrenage (en haut à gauche) : le panneau Settings de WZX Engine (thème, mode, couleurs, tailles, contours du graph) ; le canvas et les nœuds suivent aussi le thème. Au premier lancement les réglages du moteur (`build/bin/wzx_settings.ini`) sont repris, ensuite ils sont gardés dans `build/mayashader/wzx_settings.ini`.
- Chaque modification du graph est répercutée dans Maya (création, valeurs, connexions, suppression) ; Hypershade montre le réseau.
- **Assign** : assigne le matériau aux objets sélectionnés. **Graph** : ouvre Hypershade sur le matériau. **Sync Maya** : resynchronise.
- Le graph est enregistré dans la scène (nœud `network` `<nom>_wzxGraph`), donc sauvé avec le .ma / .mb.
- Aperçu à droite : sphère en path tracing (vp3d), HDRI (`assets/hdri`) + 3 lumières. Glisser = tourner, clic droit glissé = tourner l'éclairage, molette = zoom.
  Les couleurs sont lues dans l'espace de rendu de Maya (ACEScg par défaut) ; l'exposition est calée sur un rendu Arnold avec le même HDRI.

## Fonctionnement

`WzxShaderGraph.exe` est une appli séparée : Maya la lance avec `--embed`, lit le handle de sa fenêtre et l'insère dans le
panneau (`QWindow.fromWinId`). L'appli parle à Maya par son `commandPort` (port 7811, Python) : à chaque modification elle
écrit le graph (`Documents/maya/wzx_shader/<nom>.graph.json`) et appelle `wzx_shader_bridge.sync_file`. Elle se ferme
avec le panneau ou avec Maya.

Fichiers : `src/mayashader/` (appli), `tools/maya/scripts/wzx_shader_panel.py` (panneau, étagère),
`tools/maya/scripts/wzx_shader_bridge.py` (graph → nœuds Maya).
