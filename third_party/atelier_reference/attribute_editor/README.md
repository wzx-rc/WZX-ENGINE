# Atelier Inspector — Dear ImGui autonome

Ce paquet contient uniquement l'inspecteur validé **Inspecteur**, dans une fenêtre Windows autonome. Son panneau sombre de 318 px présente les attributs, les contrôles numériques, les couleurs et les paramètres UV. Les onglets Properties, Stage et Texture appartiennent à cet inspecteur.

## Démarrer

Double-cliquer sur `Launch-Inspector.cmd` ou `Atelier-Inspector.exe`. Aucun téléchargement ni installation de Python, Node ou Visual Studio n'est nécessaire pour utiliser l'exécutable. Windows x64 et DirectX 11 sont requis ; le host utilise WARP si le GPU est indisponible.

Les sliders et valeurs sont synchronisés. Un point cuivre et le reset signalent une valeur modifiée. Le picker propose RGB/HSV, alpha indépendant de l'opacité, valeurs hexadécimales, palette et comparaison original/courant. **Apply** valide la couleur ; **Cancel** abandonne le brouillon. Les modifications sont regroupées par geste dans l'historique.

Les valeurs sont stockées dans un `Document` local. Le choix d'une texture conserve son chemin ; la vignette est un indicateur de fichier. Les paramètres Stage éditent les données d'environnement et de caméra. Le raccordement au moteur et le rendu des textures ou d'une scène sont à réaliser par l'application hôte ; voir `docs/INTEGRATION.md`.

## Raccourcis

| Raccourci | Action |
|---|---|
| Ctrl+Z | Annuler |
| Ctrl+Y / Ctrl+Shift+Z | Rétablir |
| Ctrl+S | Enregistrer le document JSON |
| Ctrl+O | Ouvrir un document JSON |
| Ctrl+Shift+S | Enregistrer sous |
| F12 | Capture PNG dans `outputs/capture.png` |

La recherche filtre les sections par leurs attributs. Les sections se replient. L'opacité reste conservée mais inactive pour Opaque/Unlit ; l'IOR reste éditable. Les paramètres d'une texture inactive gardent leur valeur.

## Contenu

| Chemin | Usage |
|---|---|
| `Atelier-Inspector.exe` | Exécutable autonome compilé avec runtime statique |
| `src/inspector.h` | Données et API publique |
| `src/inspector.cpp` | Composants et panneau Dear ImGui |
| `src/inspector_model.cpp` | Limites, historique, couleurs et sérialisation |
| `src/main.cpp` | Petit host Win32 / DirectX 11 |
| `design/` | Layout JSON, thème, actions fixes, SVG nommés et captures annotées |
| `vendor/imgui/` | Sous-ensemble Dear ImGui nécessaire pour reconstruire |
| `validation/` | Rapports et captures du rendu natif |
| `manifest.json` | Inventaire et empreintes des fichiers du paquet |

Le design JSON décrit le contrat de rendu ; les paramètres sont implémentés en C++ dans cette version. Aucune ancienne fenêtre Scene, Graph ou Material Preview n'est incluse.

## Reconstruire

Avec les outils C++ x64 de Visual Studio installés : lancer `powershell -ExecutionPolicy Bypass -File .\build.ps1`. Le résultat est écrit dans `dist/Atelier-Inspector.exe`. Une configuration CMake est également fournie. Le build utilise les sources vendoriées, sans téléchargement.

Les contrôles de validation sont reproductibles :

```powershell
.\Atelier-Inspector.exe --self-test validation\model-tests.json
.\Atelier-Inspector.exe --ui-test validation\ui --hidden
.\Atelier-Inspector.exe --screenshot validation\inspector.png --frames 16 --hidden
```

Les tests d'interaction injectent des événements souris dans le véritable contexte Dear ImGui et capturent le backbuffer DirectX. Ils ne remplacent pas une validation du raccordement à votre moteur.

Pour reproduire la référence de design à 100 % : ajouter `--scale 1 --width 342 --height 920`. L'application utilise par défaut l'échelle Windows et ajuste sa fenêtre à la zone de travail du moniteur.
