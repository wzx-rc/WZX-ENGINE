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
