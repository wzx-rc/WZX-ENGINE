# BrickIt natif : plan de travail

Cahier des charges : `PORTAGE_PC.md`. Suivi (captures, temps) : `suivi/JOURNAL.md`.
Machine de test : GeForce RTX 5060, Windows 11, écran 4K.

## Base de départ (M0)

- Branche `brickit-native` = `main` + fusion de `basic-function` (le viewer le plus complet : MSAA, glow de sélection,
  multi-vues, snapping). Capture 00 : l'éditeur WZX Engine tel quel.
- Le module `src/vp3d` (Renderer DX12 + DXR inline) est gardé et étendu ; l'éditeur `src/app` n'est pas touché.
- **Nouvelle application `BrickIt.exe`** (`src/brickit/`), compilée par `build.bat` à côté de `Viewport3D.exe`.
  Elle réutilise `vp3d.lib`, Dear ImGui, `ImGuiLayer` et `Theme` de l'éditeur (même thème, même rendu).
  Raison : l'éditeur est un outil généraliste (outliner, graph, gizmos) ; le builder a sa propre boucle (brique en main,
  snap typé, bibliothèque en bas) et doit rester léger avec des dizaines de milliers d'objets.

## Ce que le Renderer fait déjà, et ce qui manque

- DXR : un BLAS par mesh, une instance TLAS par objet. Bon modèle pour les briques (un BLAS par type de pièce).
- Raster : un `DrawIndexedInstanced` par objet et par passe (ombre, principale, ids), avec 16 constantes racine.
  À 10 000 briques : 30 000 appels par image. **À remplacer** par un dessin instancié par mesh
  (transforms dans un StructuredBuffer, `SV_InstanceID`), puis culling en compute + `ExecuteIndirect`.
- `buildTLAS` reconstruit tout (matériaux, instances, TLAS) à chaque image : à ne refaire que si la scène change.
- Path tracer : bevel shader, vernis (coat), SSS, verre, HDRI avec MIS, débruiteur à-trous, ACES / AgX. À ajouter :
  traces de doigts (roughness), couleur par instance (une palette de matériaux suffit au début).

## Données

- Base (50 pièces) : extraite une fois du HTML web par `tools/brickit/extract_base.py`
  vers `assets/brickit/base.json` + `base.bin` (même format que `__VP3D_LIB` / `__VP3D_BIN`).
- Technic (730 pièces) : les packs `docs/brickit/web/technic/g{n}.wasm` (zlib) sont lus tels quels en C++
  (inflate déjà présent dans `EnvImage.cpp`), copiés par `build.bat` dans `build/bin/assets/brickit/technic`.
- Miniatures : rendues par l'application elle-même avec la scène de rendu des miniatures (même look partout), en cache disque.

## Traces de doigts (dossier `FingerPrint`)

Aucun dossier nommé `FingerPrint` dans un dossier `Ressources` sur les disques C à G (recherche du 01/11/2026).
Maps trouvées, par ordre de préférence :
1. `C:\Users\wzx\Documents\maya\2027\scripts\BrickIt\TOOLS\M_Roughness_Fingers_B.jpg` (map de roughness BrickIt Maya) : **choix par défaut**.
2. `C:\Users\wzx\Documents\maya\2027\scripts\ShadeIt\ShadeIt_Library\SHADERS\Plastic\FingerPrints_Diffuse.jpg`
   (aussi dans LightIt et dans `Documents\Adobe\Adobe Substance 3D Painter\assets\textures\wzx_`).
3. `D:\ProgramData\Marmoset Toolbag 5\library\Textures\Grunge Maps\Fingerprint Light|Medium|Heavy`.
La map choisie est copiée dans `assets/brickit/fingerprints.jpg` ; à remplacer si Wzx indique le vrai dossier.

## Jalons

- M0 : ce plan, capture 00.
- M1 : chargement base + Technic, grille de toutes les pièces dans BrickIt.exe. Capture 01.
- M2 : raster instancié, TLAS seulement si la scène change, banc d'essai 1k / 5k / 10k / 50k (raster et path tracing). Captures 02.
- M3 : look : plastique ABS, traces de doigts, bevel + rainures, HDRI studio lumineux, AgX/ACES. Captures 03.
- M4 : construction : brique en main, snap typé (hachage spatial des connecteurs), bibliothèque 2 rangées,
  couleurs à droite, sélection fine, focus, dupliquer en pile, annuler, `.v3d.json`. Captures 04.
- M5 : mode Mécanique. Captures 05.
