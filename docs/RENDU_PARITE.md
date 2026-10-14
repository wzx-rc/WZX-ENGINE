# Parité du rendu : éditeur, Windows, iOS

Objectif : un jeu construit dans l'éditeur doit s'afficher de la même façon dans l'éditeur, dans le build Windows et dans le build iOS.

## Ce qui doit être identique

- Modes **raster** (temps réel) et **GI sondes (DDGI)** sur toutes les cibles.
- Mêmes shaders sources (HLSL), compilés pour chaque API (DXIL pour DirectX 12, Metal pour iOS).
- Mêmes données : scène, matériaux, textures, lumières, environnement.
- Même post-traitement : tonemapping (ACES, AgX, Standard) et débruiteur.

## Exclus (listés explicitement)

- **Path tracing** (touche P, mode « Temporal » et « Progressive ») : Windows seulement. Il utilise le ray tracing matériel, non prévu sur iOS.
- **Options réservées à l'éditeur** : panneaux, gizmos, sélection, grille d'édition.
- Tout autre effet qui dépend du ray tracing matériel (à lister ici avant de l'ajouter).

## Critère de « 100 % identique »

Une identité bit à bit est impossible entre deux GPU et deux API (arrondis flottants, filtrage, ordre des opérations). Le critère retenu est mesurable :

- Des scènes de référence sont rendues sur chaque cible à résolution et réglages identiques.
- L'écart maximal par pixel après tonemapping doit rester sous un seuil fixé (à définir : une ou deux valeurs sur 255).
- Les rendus de référence sont des fichiers PNG stockés dans le dépôt, et un test compare chaque build à ces références.

## Points de vigilance

- La DDGI actuelle lance ses rayons par ray tracing matériel (`TraceClosest` dans `DDGIUpdate.hlsl`). Pour iOS sans ray tracing, il faut remplacer la source des rayons par une capture rasterisée (cubemap par sonde). Cette version devient aussi celle de Windows, pour que l'éditeur et les builds restent identiques.
- Les directions de rayons sont tirées au hasard à chaque image : les graines doivent être déterministes pour pouvoir comparer deux rendus.

## Mode Lit multi-API (Windows et iOS)

- Le mode Lit des builds passe par `src/lit` (Diligent Engine) : DirectX 12 sur Windows, Metal via MoltenVK sur iOS.
- Il compile le même `src/vp3d/shaders/Raster.hlsl` que l'éditeur (sémantiques ATTRIBn par la macro `VP3D_ATTRIB_SEMANTICS`) et le même tonemapping (`ToneCurves.hlsli`, partagé avec `Tonemap.hlsl`).
- Toute la préparation côté CPU (matériaux, lumières, lumière clé et cadrage de l'ombre, HDRI : mips, SH et soleil) est dans `src/vp3d/LitPrep.cpp`. Le viewport DirectX 12 de l'éditeur a encore sa propre copie de ce code dans `Renderer.cpp` et `RendererRaster.cpp` : à remplacer par `LitPrep` une fois la branche DDGI raster fusionnée.
- Test : `WZXPlayerHeadless --out image.png` (dossier `player/`) rend la scène sans fenêtre, sur DirectX 12 (Windows) ou Vulkan (Linux, sans GPU avec Mesa lavapipe), pour comparer les images.
- Point d'attention : glslang ignore `row_major` dans les structures de `ConstantBuffer<T>`. Le module Lit compile donc avec l'empaquetage row-major par défaut (`SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR`).
- Point d'attention : Diligent compile les shaders Direct3D avec FXC (shader model 5), sans espaces de registres. Dans le build Lit (`VP3D_ATTRIB_SEMANTICS`), `gTextures` est donc en `t4..t19` au lieu de `t0, space1` (sinon la texture 0 lit la shadow map).
- Jeu : `WZXPlayerHeadless --scene studio` joue le graphe avec des clics et touches scriptés (`--click x,y,image`, `--drag`, `--key`). Écart mesuré DirectX 12 / Vulkan sur ce test : max 28/255, moyenne 0,01/255.
