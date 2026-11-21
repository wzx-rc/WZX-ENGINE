# WZX Live : le jeu en cours d'édition sur l'iPhone / l'iPad

L'éditeur envoie le jeu qu'il édite (scène, graphe, prefabs, fichiers utilisés) à l'app installée sur l'appareil, par le
Wi-Fi. L'app le lance aussitôt, et renvoie son journal et des captures d'écran. Plus besoin d'une nouvelle IPA pour
chaque changement de scène ou de graphe : il ne faut recompiler l'app que lorsque l'éditeur l'annonce.

## Une fois : installer l'app avec WZX Live

1. Build (F7) > iOS, profil **ios-live** (option « WZX Live » cochée), Build : `game/export/game.json` contient `"live": true`.
2. Pousser : GitHub Actions (workflow *iOS build*) produit l'artefact `WZXPlayer-unsigned` (.ipa).
3. Installer l'IPA avec Sideloadly (Apple ID gratuit : à re-signer tous les 7 jours, la même IPA).
4. Ouvrir l'app : au premier lancement, iOS demande l'accès au **réseau local** : accepter.
   L'app affiche son écran d'attente : **code d'appairage** à 4 chiffres, nom, adresse IP et port.

## À chaque essai

1. Éditeur : **Tools > Appareils (WZX Live)** (ou Build > « Appareils... »).
2. Choisir l'appareil dans la liste (vu sur le réseau par Bonjour ; sinon taper l'adresse affichée sur l'appareil).
3. Taper le code la première fois (il est retenu ensuite), **Connecter**.
4. **Envoyer sur l'appareil** : seuls les fichiers changés partent ; l'app relance le jeu.
   « Capture » montre l'écran de l'appareil, le journal de l'appareil s'affiche dessous.
5. Sur l'appareil, le badge **LIVE** en haut de l'écran donne le code et l'état, et ramène à l'écran d'attente.

## Quand faut-il une nouvelle IPA ?

Avant l'envoi, l'éditeur compare le jeu à ce que l'app sait lire (elle l'annonce à la connexion) :

- **nœud du graphe inconnu** de l'app (« le nœud X n'existe pas dans sa version ») ;
- **fonction du contenu** plus récente (instances de prefab, clips, courbes, HDRI...) ;
- **format de contenu** plus récent.

Dans ces cas, un avertissement s'affiche avec « Envoyer quand même » (l'app ne plante pas : elle affiche l'erreur et
reste en attente). Si l'app parle **un autre protocole WZX Live**, l'envoi est bloqué. Dans tous ces cas : recompiler
et réinstaller l'IPA (étapes 1 à 3 ci-dessus).

## Sécurité

- Réseau local seulement ; l'app n'accepte rien avant le bon code (connexion coupée après plusieurs codes faux).
- L'app n'écrit que dans son dossier `Documents/live` (chemins relatifs, sans `..`).
- Option de développement : décocher « WZX Live » pour un jeu final.

## Sur le PC, sans iPhone

`WZXPlayer.exe --live` attend l'éditeur sur `127.0.0.1:47800` (fenêtre : code et port dans le titre) ;
`--live-lan` écoute aussi le réseau (Bonjour). Dans l'éditeur, l'entrée « Ce PC (WZXPlayer --live) » de la liste.

Tests : `node tools\live_test.mjs <Viewport3D.exe> <WZXPlayer.exe> <dossier>` (de bout en bout, avec des apps
« trop anciennes » simulées : `--live-simulate-old unit.produce,prefab-clip`, `--live-simulate-protocol 99`), et le
self-test de l'éditeur (`tools\selftest.bat`, groupe *wzx live*, avec `WZX_PLAYER` = chemin de WZXPlayer.exe).

## IA (MCP)

`wzx_device_list`, `wzx_device_connect`, `wzx_device_send` (`force` pour envoyer malgré les avertissements),
`wzx_device_screenshot` (image JPEG de l'écran de l'appareil), `wzx_device_log`. API : `device.*` (voir `wzx_help`).

## Technique

Protocole : `src/game/LiveLink.h` (TCP, messages préfixés par leur taille : HELLO, INFO, MANIFEST, NEED, CHUNK,
RELOAD, STATUS, LOG, SHOT). L'appareil est le serveur (port 47800, Bonjour `_wzx-live._tcp`), l'éditeur se connecte
(aucune demande du pare-feu Windows). Côté appareil : `player/src/LiveHost.cpp` ; côté éditeur :
`src/app/LiveClient.cpp` (découverte DNS-SD de Windows 10+) et `src/app/AppLive.cpp` (fenêtre, API, export).
Ajouter une fonction de contenu : un identifiant dans `kFeatures` et sa détection dans `contentNeeds` (LiveLink.cpp).
