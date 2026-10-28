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

Le Grot Bomber du rendu (`assets/test/grot.glb`) est pris en version HD si `desktop/assets/grot_hd.glb` existe au moment
du build, avec `grot_basecolor_8k.jpg`.

La bibliothèque de briques de l'app est séparée de celle du navigateur : passer par l'export / import `.zip` du menu
Bibliothèque de l'importeur.

Raccourcis : F11 plein écran, F12 outils de développement.
