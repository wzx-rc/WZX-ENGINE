# Plasma Bridge (édition Développeur)

Passerelle Maya 2027 → Importer de Plasma Orks Builder. Copie de référence ; la version active est installée dans
`C:\Users\wzx\Documents\maya\2027\scripts\PlasmaBridge` (le dossier Documents n'est pas redirigé).

## Contenu
- `PlasmaBridge/core.py` : analyse de la sélection (low / high, locators PLUG_ / SOCKET_, catégorie, texture couleur) et envoi.
- `PlasmaBridge/ui.py` : fenêtre Plasma Bridge (PySide6) + `quick_send()`.
- `PlasmaBridge/cli.py` : conversion d'une scène .ma / .mb sans ouvrir Maya (mayapy).
- `PlasmaBridge/icons/` : icônes de l'étagère et de la fenêtre.
- `shelf/shelf_PlasmaOrks.mel` : étagère « PlasmaOrks » (Plasma Bridge, Envoi rapide, PLUG grip, Ouvrir l'app).

## Installation
1. Copier `PlasmaBridge/` dans `Documents\maya\2027\scripts\`.
2. Copier `shelf/shelf_PlasmaOrks.mel` dans `Documents\maya\2027\prefs\shelves\` (Maya ouvre l'étagère au démarrage).

## Contrat avec l'app (édition Développeur)
Chaque envoi écrit `%USERPROFILE%\Documents\PlasmaOrks\Bridge\inbox\<nom>_<AAAAMMJJ-HHMMSS>\` :
`<nom>.fbx` (maillages + locators, unités de la scène, Y en haut), `<nom>_basecolor.<ext>`, puis `manifest.json`
(écrit en dernier ; le dossier est créé en `.tmp_*` puis renommé). Ensuite :
`"<exe>" --plasma-import "<dossier>"`, l'exe étant lu dans `%APPDATA%\PlasmaOrksBuilder\bridge.json` (clé `exe`),
sinon `PlasmaOrksBuilder\app-v6\Plasma Orks Builder-win32-x64\Plasma Orks Builder.exe`. Les dossiers envoyés ne sont
jamais supprimés par Maya (l'app purge ceux de plus de 7 jours).

manifest.json : `{"bridge":1,"version","name","category","plugType","units","files":[...],"meshes":[{"name","lod","tris"}],
"locators":[{"name","kind","type","valid"}],"bbox","source":{"scene","maya"},"created"}`.

## Conversion .ma / .mb
    "C:\Program Files\Autodesk\Maya2027\bin\mayapy.exe" "...\PlasmaBridge\cli.py" "<scène>" [--root <noeud>] [--inbox <dossier>]
Dernière ligne de stdout : `PLASMA_RESULT {"folder": "...", "warnings": [...]}` (code 0) ou
`PLASMA_RESULT {"error": "..."}` (code 1). La scène n'est jamais enregistrée ; le processus se termine par
TerminateProcess, car la fermeture normale de maya.standalone plante (rapport d'erreur Autodesk + scène « Recovered »).
