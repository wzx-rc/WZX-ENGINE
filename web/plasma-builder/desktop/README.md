# Plasma Orks Builder pour Windows

Le builder web (`web/plasma-builder`) dans une fenêtre Electron, avec le **path tracing** activé (mode retiré de la
version web). Tout est embarqué : three.js, three-mesh-bvh et three-gpu-pathtracer sont copiés dans l'app, aucun
accès réseau n'est nécessaire (hors polices Google, qui ont un repli).

## Lancer depuis les sources

```
cd web\plasma-builder\desktop
npm install
npm start
```

## Construire l'exécutable portable

```
npm run pack:win
```

Sortie : `dist\Plasma Orks Builder-win32-x64\Plasma Orks Builder.exe` (dossier à zipper ou copier tel quel).

## Modèle test HD

La version web embarque l'Ork Boy allégé (~400 k triangles, `assets/test/boy_32mm.glb`). Pour l'app, une version
HD (~1,7 M triangles) est prise si `desktop/assets/boy_32mm_hd.glb` existe au moment du build :

```
python tools/mb_mesh/mb_mesh.py BOY_32mm.mb out
python tools/mb_mesh/prep.py && node tools/mb_mesh/simplify.mjs 0.3 web/plasma-builder/desktop/assets/boy_32mm_hd.glb
```

La texture `basecolor_4k.jpg` (UDIM 1001, corps) et `basecolor_1002_4k.jpg` (tête) sont chargées depuis
`web/plasma-builder/assets/test/` si présentes, sinon via le bouton « Charger une texture basecolor » de l'onglet Rendu.

Raccourcis : F11 plein écran, F12 outils de développement.
