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

## Éditions : développeur et public

`plasmaEdition` dans `package.json` (ou la variable `PLASMA_EDITION` au lancement) choisit l'édition :

- **dev** : la version de Wzx, sur PC. Elle ajoute la passerelle Maya ci-dessous.
- **public** (valeur par défaut si le champ manque) : la version pour les utilisateurs, sans les outils Maya.

## Passerelle Maya (édition dev)

L'outil Maya **PlasmaBridge** (étagère PlasmaOrks, dans `Documents\maya\<version>\scripts\PlasmaBridge`) envoie la
sélection de la scène ouverte à l'Importeur :

1. Il écrit un dossier `Documents\PlasmaOrks\Bridge\inbox\<nom>_<date>\` : le `.fbx` (maillages low / high et
   locators `PLUG_` / `SOCKET_`), la texture couleur et `manifest.json`, écrit en dernier.
2. Il lance `Plasma Orks Builder.exe --plasma-import "<dossier>"`. L'app déjà ouverte passe au premier plan, sinon
   elle démarre directement sur l'Importeur. La boîte de réception est aussi surveillée tant que l'app tourne.
3. L'Importeur reçoit les fichiers comme s'ils avaient été déposés, dans une nouvelle brique.

Au démarrage, l'app écrit `%APPDATA%\PlasmaOrksBuilder\bridge.json`, qui donne à Maya le chemin de l'exe et de la
boîte de réception. Les envois de plus de 7 jours sont supprimés.

Une scène **.ma / .mb** déposée dans l'Importeur est convertie en arrière-plan par `mayapy` et `PlasmaBridge\cli.py`,
sans ouvrir Maya. L'app prend la version de Maya la plus récente qui a les deux, sauf si `PLASMA_MAYAPY` et
`PLASMA_BRIDGE_CLI` sont renseignées. La dernière ligne `PLASMA_RESULT {"folder": …}` de la sortie donne le dossier
à importer ; en cas d'échec, c'est `{"error": …}`.
