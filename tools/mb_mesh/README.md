# mb_mesh : sculpt Maya (.mb) vers GLB de test du rendu

Lit directement un Maya binaire (IFF FOR8) sans Maya, extrait les maillages et produit un GLB allégé en gardant les UV.

```
python mb_mesh.py BOY_32mm.mb out             # un .npz par mesh (positions, UV map1, triangles)
python xf.py BOY_32mm.mb                       # matrices + tweaks pt des dents / yeux (utilise tools/mb2glb)
python prep.py                                 # sommets uniques (position, uv), UDIM ramenés dans [0,1]
npm install && node simplify.mjs 0.07 boy_32mm.glb   # ~400 k triangles (web) ; 0.3 = ~1,7 M (app Windows)
```

Format du bloc MESH : nom d'attribut + NUL + drapeau, puis positions (float32 BE), paires d'arêtes (bit 31 = arête
dure, à masquer), liste d'arêtes par face (bit 31 = arête inversée, bit 30 = fin de face), normales (1e20 = non
définie), jeux d'UV (« map1 ») et index d'UV par coin de face. Le sculpt haute définition est dans
polySurfaceShape1 (corps) et polySurfaceShape2 (tête, déjà en place) ; dents et yeux = (positions + pt) × matrice monde.

## OBJ avec UV (ex. Grot Bomber)

`python obj2out.py GROT_UV.obj grot` écrit `out/` (sommets dédoublés par couple position/uv, quads triangulés),
puis `node simplify.mjs 0.25 grot.glb` (web, ~410k triangles) et `node simplify.mjs 1 grot_hd.glb` (app Windows).
