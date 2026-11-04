# Bibliothèque complète LDraw (BrickIt natif)

BrickIt lit, en plus de la base (29 pièces) et des packs Technic (741 pièces, dans git), toute la bibliothèque officielle
LDraw : 22 545 pièces (pièces imprimées, minifigs, obsolètes compris), converties avec les connecteurs de la LDCad shadow
library. Ces fichiers sont générés et **ne sont pas dans git** (≈ 470 Mo) ; ils vivent hors des dossiers synchronisés :

```
%LOCALAPPDATA%\BrickIt\
  raw\ldraw_complete\ldraw\      bibliothèque officielle (complete.zip de library.ldraw.org)
  raw\ldcad_shadow\...\          LDCad shadow library (github.com/RolandMelkert/LDCadShadowLibrary)
  raw\rebrickable\*.csv.gz       données Rebrickable (couleurs réelles, tools/brickit/part_colors.py)
  full\index.json, f<n>.bin      packs de la bibliothèque complète (524 packs, ≈ 390 Mo, index 13 Mo)
  full\thumbs.pack               miniatures (PNG 128 px, 22 545, ≈ 72 Mo)
  full\aliases.txt               alias et pièces déplacées -> pièce cible (1 662)
  full\LDConfig.ldr              couleurs LDraw (import des modèles : couleur hors palette -> la plus proche)
  omr\*.mpd                      modèles officiels téléchargés (OMR)
```

`Library::load` cherche `assets\brickit\full` à côté de l'exe, sinon `%LOCALAPPDATA%\BrickIt\full`. Sans ces fichiers,
BrickIt fonctionne comme avant (base + Technic).

## Régénérer

```
:: 1. télécharger (une fois)
::    https://library.ldraw.org/library/updates/complete.zip  -> raw\ldraw_complete (dézippé)
::    https://github.com/RolandMelkert/LDCadShadowLibrary/archive/refs/heads/master.zip -> raw\ldcad_shadow (dézippé)
:: 2. convertir (≈ 6 min, 16 processus)
node tools\brickit\build_full.mjs "%LOCALAPPDATA%\BrickIt\raw\ldraw_complete\ldraw" "%LOCALAPPDATA%\BrickIt\raw\ldcad_shadow\LDCadShadowLibrary-main" "%LOCALAPPDATA%\BrickIt\full" 16
py tools\brickit\ldraw_aliases.py "%LOCALAPPDATA%\BrickIt\raw\ldraw_complete\ldraw" "%LOCALAPPDATA%\BrickIt\full\aliases.txt"
copy "%LOCALAPPDATA%\BrickIt\raw\ldraw_complete\ldraw\LDConfig.ldr" "%LOCALAPPDATA%\BrickIt\full\"
:: 3. miniatures (≈ 10 min, fenêtre cachée)
build\bin\BrickIt.exe --build-full-thumbs "%LOCALAPPDATA%\BrickIt\full\thumbs.pack"
```

## Limites connues

- Pièces imprimées : la géométrie est là, l'impression (triangles d'une autre couleur que 16) n'est pas encore rendue.
- 4 pièces sans géométrie après conversion (6637a–d).

Crédits : géométrie LDraw (ldraw.org, CC BY 4.0 / CC BY 2.0), connecteurs LDCad shadow library (Roland Melkert),
couleurs réelles Rebrickable (rebrickable.com), modèles officiels LDraw OMR (CC BY / CCAL selon le modèle).
