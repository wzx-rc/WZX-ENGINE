# Journal de suivi BrickIt natif

| # | Date | Capture | État | GPU / CPU | Briques |
|---|------|---------|------|-----------|---------|
| 00 | 01/11/2026 | `00_etat_depart.png` | Départ : `main` + `basic-function` fusionnés, éditeur WZX Engine compilé tel quel (scène Studio, raster MSAA). | 0,19 ms GPU, 15,9 ms par image (vsync) | 0 |
| 01 | 01/11/2026 | `01_grille_780_pieces_raster.png`, `01_grille_780_pieces_pathtracing.png` | M1 : `BrickIt.exe` (nouvelle app sur vp3d + thème ImGui de l'éditeur). Base (50 pièces, extraite du HTML par `tools/brickit/extract_base.py`) + les 21 packs Technic lus tels quels (inflate C++) : 780 pièces, 1,44 M triangles, rangées par sous-famille. HDRI studio, AgX. Encore un objet vp3d par brique (pas d'instancing). | Raster : 1,05 ms GPU, 3,3 ms CPU. Path tracing : 145 ms GPU par image (2 éch./px, 1600×900) : à optimiser en M2 | 780 |
