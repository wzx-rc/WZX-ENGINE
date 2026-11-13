// Langue de l'interface : anglais par défaut, français en option (Réglages > Langue, enregistré sur l'appareil).
// Chargé sans module et bloquant dans le <head> de chaque page, avant le corps.
//
// Le texte source de l'app reste en français. En anglais, ce script traduit l'affichage :
// - les textes et attributs (title, aria-label, placeholder, alt, label) dont le texte exact est dans DICT,
//   au chargement puis à chaque ajout ou changement dans la page (MutationObserver), donc aussi ce que le JS écrit ;
// - les textes qui commencent par un préfixe connu (PREFIX, ex. « Export impossible : <erreur> ») ;
// - les messages construits avec des valeurs passent par window.tr(fr, en) au moment où le JS les écrit.
// Un texte déjà anglais n'est pas dans DICT : il n'est pas retouché (pas de boucle avec l'observateur).
// Un élément (et son contenu) marqué data-noi18n n'est jamais traduit (noms saisis par l'utilisateur, etc.).
(function () {
  var KEY = 'plasmaork.lang';
  var lang = 'en';
  try { var v = localStorage.getItem(KEY); if (v === 'fr' || v === 'en') lang = v; } catch (e) { /* stockage indisponible */ }
  var html = document.documentElement;
  html.lang = lang;

  var DICT = {
    // ---------------------------------------------------------------- commun, barre du haut, réglages
    'Réglages': 'Settings', 'Navigation': 'Navigation', 'Fermer': 'Close', 'Réduire': 'Minimize', 'Agrandir': 'Maximize',
    'Restaurer': 'Restore', 'Mode': 'Mode', 'Interface': 'Interface', 'Complet': 'Full', 'Simple': 'Simple',
    'Simple : seulement le Builder et l\'Importer. Le Hub, la Library et le Rig Lab sont cachés.': 'Simple: only the Builder and the Importer. The Hub, the Library and the Rig Lab are hidden.',
    'Interface du Builder': 'Builder interface', 'Classique': 'Classic', 'Atelier': 'Workshop',
    'Classique : l\'interface d\'origine. Atelier : la vue d\'abord, l\'équipement à gauche, la bibliothèque en tiroir.': 'Classic: the original interface. Workshop: viewport first, gear on the left, library in a drawer.',
    'Choix enregistrés sur cet appareil.': 'Choices are saved on this device.',
    'Langue': 'Language', 'La couleur de la faction devient l\'accent de toute l\'app.': 'The faction color becomes the accent of the whole app.', 'La page se recharge pour changer de langue.': 'The page reloads to switch language.',
    'Hub · Plasma Orks Builder': 'Hub · Plasma Orks Builder', 'Importer · Plasma Orks Builder': 'Importer · Plasma Orks Builder',
    'Builder · Plasma Orks Builder': 'Builder · Plasma Orks Builder', 'Plasma Library · Plasma Orks Builder': 'Plasma Library · Plasma Orks Builder',
    'Rig Lab · Plasma Orks Builder': 'Rig Lab · Plasma Orks Builder',
    'Annuler': 'Cancel', 'Supprimer': 'Delete', 'Renommer': 'Rename', 'Nouveau nom': 'New name', 'Ouvrir': 'Open', 'Ignorer': 'Ignore',
    'Modifier': 'Edit', 'Exporter': 'Export', 'Rechercher': 'Search', 'Rechercher…': 'Search…', 'Outils': 'Tools', 'Affichage': 'Display',
    'Chargement de l\'ork': 'Loading the ork', 'Chargement de l\'ork…': 'Loading the ork…', 'Prêt !': 'Ready!', 'Terminé': 'Done',
    'erreur inconnue': 'unknown error',

    // ---------------------------------------------------------------- catégories, emplacements, factions
    'Têtes': 'Heads', 'Armes': 'Weapons', 'Dos': 'Back', 'Épaulières': 'Shoulder pads', 'Torse': 'Chest', 'Ceinture': 'Belt', 'Socles': 'Bases',
    'Tête': 'Head', 'Arme': 'Weapon', 'Épaulière': 'Shoulder pad', 'Socle': 'Base', 'Épaule': 'Shoulder', 'Main': 'Hand', 'Libre': 'Free',
    'Main gauche': 'Left hand', 'Main droite': 'Right hand', 'Épaule gauche': 'Left shoulder', 'Épaule droite': 'Right shoulder',
    'Ceinture gauche': 'Left belt', 'Ceinture droite': 'Right belt',
    'Faction': 'Faction', 'Klan rose': 'Pink Klan', 'Klan bleu': 'Blue Klan', 'Klan jaune': 'Yellow Klan', 'Klan vert': 'Green Klan',
    'Couleurs de faction': 'Faction colors', 'couleurs de faction': 'faction colors', 'masque faction': 'faction mask',

    // ---------------------------------------------------------------- Hub
    'Dents : la monnaie du hub': 'Teeth: the hub currency', 'Mon ork et packs à ouvrir': 'My ork and packs to open',
    'Ton ork, pose Waaagh !': 'Your ork, Waaagh! pose', 'Dernier loot': 'Latest loot', 'Boutique': 'Shop', 'Objectifs': 'Goals',
    'Couper le paquet : glisser de gauche à droite, ou Entrée': 'Tear the pack: drag left to right, or press Enter',
    'Frapper le coffre': 'Smash the chest', 'MON ORK': 'MY ORK', 'Klan rose · 32 mm': 'Pink Klan · 32 mm', 'Ouvrir le builder': 'Open the Builder',
    '3 pièces': '3 pieces', 'À ouvrir': 'To open', '0 pack': '0 packs',
    'Coupe le paquet du doigt : une commune, une rare et peut-être une extrême, rangées dans ta Plasma Library.': 'Tear the pack with your finger: one common, one rare and maybe an extreme, stored in your Plasma Library.',
    'Ouvrir un pack': 'Open a pack', 'Ouvrir un coffre (': 'Open a chest (', 'Ouvrir un coffre': 'Open a chest',
    'Commune': 'Common', 'Rare': 'Rare', 'Extrême': 'Extreme', 'Tout voir': 'See all', 'pièces': 'pieces', '0 doublon': '0 duplicates',
    'Recycler en dents': 'Recycle into teeth', 'aucun pour l\'instant': 'none yet', 'Essayer sur mon ork': 'Try on my ork',
    'paiement sur le site (à brancher)': 'payment on the website (to be connected)', '3 pièces, 1 rare minimum': '3 pieces, at least 1 rare',
    'Coffre de Nob': 'Nob Chest', 'COFFRE DE NOB': 'NOB CHEST', '3 pièces, 1 extrême garantie': '3 pieces, 1 extreme guaranteed',
    'Pack contre des dents': 'Pack for teeth', 'échange tes doublons recyclés': 'trade your recycled duplicates',
    '1 pack par semaine, soutient le projet': '1 pack a week, supports the project', '[PRIX] / mois': '[PRICE] / month',
    'Kit en cours': 'Current kit', 'Drop du jour': 'Daily drop', 'Récupérer': 'Claim', 'série 1': 'series 1',
    'Coupe le paquet du doigt': 'Tear the pack with your finger', 'Glisse le long des pointillés, de gauche à droite.': 'Swipe along the dotted line, from left to right.',
    'Ou ouvrir d\'un clic': 'Or open with one click', 'Ranger dans la Plasma Library': 'Store in the Plasma Library',
    'Frappe le coffre pour l\'ouvrir': 'Smash the chest to open it', 'Plus de pack à ouvrir': 'No more packs to open', 'Aucun doublon': 'No duplicates',
    'Ouvre un pack : tes 3 dernières pièces apparaîtront ici.': 'Open a pack: your last 3 pieces will show up here.',
    'Nouvelle pièce extrême': 'New extreme piece', 'Nouvelle pièce': 'New piece', 'Touche pour retourner': 'Tap to flip',
    'Étape 1 sur 2 · couper': 'Step 1 of 2 · tear', 'Étape 2 sur 2 · retourner les cartes': 'Step 2 of 2 · flip the cards',
    'Encore !': 'Again!', 'Une dernière fois !': 'One last time!', 'Coffre ouvert': 'Chest opened',
    'Démo : pack ajouté. Le paiement sera branché sur le site.': 'Demo: pack added. Payment will be connected on the website.',
    'Démo : coffre ajouté. Le paiement sera branché sur le site.': 'Demo: chest added. Payment will be connected on the website.',
    'Abonnement : à brancher sur le site.': 'Subscription: to be connected on the website.',
    'Kit de départ': 'Starter kit', 'skin plasma offert': 'free plasma skin',

    // ---------------------------------------------------------------- Plasma Library
    'État': 'Status', 'Rareté': 'Rarity', 'Chercher une pièce': 'Search a piece', 'Pièce sélectionnée': 'Selected piece',
    '0 pack à ouvrir': '0 packs to open', 'Toutes les pièces': 'All pieces', 'Kit': 'Kit', 'Voir les pièces du kit': 'See the kit pieces',
    'Toutes': 'All', 'Débloquées': 'Unlocked', 'Manquantes': 'Missing', 'Doublons': 'Duplicates', 'aperçu 3D à brancher': '3D preview to be connected',
    'Poser sur mon ork': 'Put on my ork', 'Recycler': 'Recycle',
    'Une commune tombe dans chaque pack, chaque coffre et le drop du jour.': 'One common drops in every pack, every chest and the daily drop.',
    'Une rare tombe dans chaque pack et chaque coffre.': 'One rare drops in every pack and every chest.',
    'Plus rien à ouvrir': 'Nothing left to open', 'Aucune pièce ne correspond à ces filtres.': 'No piece matches these filters.',
    'Pièce à débloquer': 'Piece to unlock', 'Aller à la boutique': 'Go to the shop',
    'Export STL de la pièce : à brancher sur l\'exporteur du builder.': 'Piece STL export: to be connected to the Builder exporter.',

    // ---------------------------------------------------------------- pièces du loot (noms orks)
    'Pack Boyz': 'Boyz Pack', 'PACK BOYZ': 'BOYZ PACK', 'Kit Rokkit': 'Rokkit Kit',
    'Kasque cabossé': 'Dented Kasque', 'Mâchoire basique': 'Basic Jaw', 'Kasque à cornes': 'Horned Kasque', 'Bandana du klan': 'Klan Bandana',
    'Tête balafrée': 'Scarred Head', 'Kasque riveté': 'Riveted Kasque', 'Kasque de Nob': 'Nob Kasque', 'Lunettes de Mek': 'Mek Goggles',
    'Krâne bioniK': 'BioniK Skull', 'Kasque à pointes': 'Spiked Kasque', 'Mâchoire d\'acier': 'Steel Jaw', 'Kouronne de Warboss': 'Warboss Krown',
    'Choppa rouillée': 'Rusty Choppa', 'Kikoup\'': 'Kikoup\'', 'Slugga double': 'Twin Slugga', 'Matraque': 'Club',
    'Pince énergétique': 'Power Klaw', 'Big Shoota plasma': 'Plasma Big Shoota', 'Kustom Méga-Blasta': 'Kustom Mega-Blasta',
    'Sac à butin': 'Loot Sack', 'Bidon de fuel': 'Fuel Can', 'Fanion': 'Pennant', 'Tuyaux': 'Pipes', 'Réservoir': 'Tank',
    'Pot d\'échappement': 'Exhaust', 'Générateur': 'Generator', 'Bannière du klan': 'Klan Banner', 'Antenne': 'Antenna',
    'Bannière de Warboss': 'Warboss Banner', 'Rokkit Pack plasma': 'Plasma Rokkit Pack',
    'Épaulière simple': 'Plain Shoulder Pad', 'Épaulière à pointes': 'Spiked Shoulder Pad', 'Plaque rivetée': 'Riveted Plate',
    'Épaulière en pneu': 'Tyre Shoulder Pad', 'Épaulière à crâne': 'Skull Shoulder Pad', 'Épaulière dentée': 'Toothy Shoulder Pad',
    'Épaulière gauche Rokkit': 'Rokkit Left Shoulder Pad', 'Épaulière de Nob': 'Nob Shoulder Pad', 'Épaulière à chaîne': 'Chain Shoulder Pad',
    'Épaulière à cornes': 'Horned Shoulder Pad', 'Épaulière Warboss': 'Warboss Shoulder Pad', 'Épaulière plasma': 'Plasma Shoulder Pad',
    'Plastron riveté': 'Riveted Breastplate', 'Gilet en cuir': 'Leather Vest', 'Plaques de fer': 'Iron Plates', 'Bandoulière': 'Bandolier',
    'Harnais': 'Harness', 'Torse nu': 'Bare Chest', 'Torse de Mek': 'Mek Chest', 'Cuirasse de Nob': 'Nob Cuirass',
    'Plastron à crânes': 'Skull Breastplate', 'Torse rokkit': 'Rokkit Chest', 'Méga-armure': 'Mega Armour', 'Cœur de plasma': 'Plasma Core',
    'Bourse à dents': 'Teef Pouch', 'Grenades stikk': 'Stikkbombs', 'Ceinture simple': 'Plain Belt', 'Cartouchière': 'Ammo Belt',
    'Pochette': 'Pouch', 'Chaîne': 'Chain', 'Ceinture à crânes': 'Skull Belt', 'Trophées': 'Trophies', 'Boucle du klan': 'Klan Buckle',
    'Ceinture de Mek': 'Mek Belt', 'Ceinture de Warboss': 'Warboss Belt', 'Bombe plasma': 'Plasma Bomb',
    'Socle rond': 'Round Base', 'Socle en terre': 'Dirt Base', 'Socle rocheux': 'Rocky Base', 'Socle de ferraille': 'Scrap Base',
    'Socle en ruines': 'Ruins Base', 'Socle de rouille': 'Rust Base', 'Socle de tranchée': 'Trench Base', 'Socle d\'épave': 'Wreck Base',
    'Socle de crânes': 'Skull Base', 'Socle de lave': 'Lava Base', 'Socle plasma': 'Plasma Base', 'Trône de Warboss': 'Warboss Throne',

    // ---------------------------------------------------------------- Importer
    'Briques': 'Bricks', 'Brique': 'Brick', 'Nouvelle brique': 'New brick', 'Catégorie': 'Category', 'Aperçu': 'Preview',
    'Naviguer': 'Navigate', 'Naviguer (Q)': 'Navigate (Q)', 'Ajouter un socket au clic': 'Add a socket on click',
    'Ajouter un socket au clic sur la brique': 'Add a socket by clicking the brick', 'Placer le plug au clic': 'Place the plug on click',
    'Placer le plug au clic sur la brique': 'Place the plug by clicking the brick', 'Déplacer le socket (W)': 'Move socket (W)',
    'Déplacer le socket sélectionné (W)': 'Move the selected socket (W)', 'Tourner le socket (E)': 'Rotate socket (E)',
    'Tourner le socket sélectionné (E)': 'Rotate the selected socket (E)',
    'Essayer la brique sur l\'ork, accrochée par son plug': 'Try the brick on the ork, attached by its plug',
    'Afficher la texture (6) · résine grise (5) · filaire (4)': 'Show texture (6) · grey resin (5) · wireframe (4)',
    'Afficher les textures (6) · résine grise (5) · filaire (4)': 'Show textures (6) · grey resin (5) · wireframe (4)',
    'Résine grise': 'Grey resin', 'Texture': 'Texture', 'Filaire': 'Wireframe', 'Recadrer (F)': 'Frame (F)', 'Recadrer sur la brique (F)': 'Frame the brick (F)',
    'Supprimer la brique': 'Delete the brick', 'faction 1, lourd': 'faction 1, heavy', 'Bibliothèque': 'Library',
    'Exporter toute la bibliothèque (.zip)': 'Export the whole library (.zip)', 'Importer une bibliothèque (.zip)…': 'Import a library (.zip)…',
    'Sur l\'ork': 'On the ork', 'Dépose les fichiers d\'une brique': 'Drop a brick\'s files', 'Choisir des fichiers…': 'Choose files…',
    'Modèle .fbx, .obj, .glb ou .gltf (low et high dans le même fichier ou en deux fichiers), texture .jpg / .png, sockets .json du script Maya.': 'Model .fbx, .obj, .glb or .gltf (low and high in one file or in two files), .jpg / .png texture, sockets .json from the Maya script.',
    'Modèle .fbx, .obj, .glb ou .gltf, ou directement la scène Maya .ma / .mb (convertie par Maya en arrière-plan), texture .jpg / .png. Depuis Maya : étagère PlasmaOrks, bouton Envoyer.': 'Model .fbx, .obj, .glb or .gltf, or the Maya scene .ma / .mb directly (converted by Maya in the background), .jpg / .png texture. From Maya: PlasmaOrks shelf, Send button.',
    'Lâche les fichiers pour les ajouter à la brique': 'Drop the files to add them to the brick',
    'Nom': 'Name', 'Tags': 'Tags', 'Fichiers': 'Files', 'Aucun fichier.': 'No file.', 'Unités de la scène': 'Scene units', 'cm (Maya)': 'cm (Maya)',
    'Z vers le haut': 'Z up', 'Plug': 'Plug', 'Type de plug': 'Plug type',
    'La brique reste à l\'origine, comme dans Maya. Dans le Builder, elle s\'accroche par son plug (locator': 'The brick stays at the origin, as in Maya. In the Builder it attaches by its plug (locator',
    ') au socket où on la pose. « Sur l\'ork » l\'essaie sur le premier socket de son type. Arme : manche selon Y, PLUG sans rotation au milieu de la prise ; le manche suit l\'axe du poing et, en pose de garde, l\'arme est orientée comme dans Maya.': ') to the socket it is placed on. "On the ork" tries it on the first socket of its type. Weapon: handle along Y, unrotated PLUG at the middle of the grip; the handle follows the fist axis and, in the guard pose, the weapon is oriented as in Maya.',
    'Sockets de la brique': 'Brick sockets', 'Locators': 'Locators',
    'lus dans le fichier, ou ajoutés au clic (outil cible). Types : head, grip, back, shoulder, chest, belt, base.': 'read from the file, or added on click (target tool). Types: head, grip, back, shoulder, chest, belt, base.',
    'Aucune texture.': 'No texture.', 'Contrôles': 'Checks', 'Dépose une brique pour lancer les contrôles.': 'Drop a brick to run the checks.',
    'Enregistrer dans la bibliothèque': 'Save to library', 'Enregistrer les modifications': 'Save changes',
    'UV du high reprises du low…': 'Copying high UVs from the low…',
    'Les scènes Maya (.ma / .mb) s\'importent seulement dans l\'édition développeur de l\'app. Exporte la brique en .fbx.': 'Maya scenes (.ma / .mb) can only be imported in the developer edition of the app. Export the brick as .fbx.',
    'Aucun objet en low': 'No low object', 'Pas de high : le low servira aussi en high et à l\'impression': 'No high: the low will also be used as high and for printing',
    'Objets sans UV : la texture ne s\'appliquera pas dessus': 'Objects without UVs: the texture won\'t apply to them', 'UV présentes': 'UVs present',
    'Pas de texture (mode Peint en gris)': 'No texture (grey in Painted mode)',
    'Pas de PLUG_ : la brique s\'accrochera par son origine (0, 0, 0). Ajoute un locator PLUG_ dans Maya ou place-le au clic (outil prise)': 'No PLUG_: the brick will attach by its origin (0, 0, 0). Add a PLUG_ locator in Maya or place it on click (grip tool)',
    'Nom du socket': 'Socket name', 'Type du socket': 'Socket type', 'Aucun socket : la brique ne porte rien.': 'No socket: the brick carries nothing.',
    'Retirer la texture': 'Remove the texture', 'Retirer le masque': 'Remove the mask',
    'Aucune texture. Dépose un .jpg ou .png (un nom avec « mask » est rangé en masque de faction).': 'No texture. Drop a .jpg or .png (a name containing "mask" is stored as the faction mask).',
    'Clique sur la brique': 'Click the brick', 'Brique seule, à l\'origine comme dans Maya': 'Brick alone, at the origin as in Maya',
    'Stockage du navigateur indisponible : la bibliothèque ne peut pas être enregistrée ici': 'Browser storage unavailable: the library can\'t be saved here',
    'Clique encore pour supprimer la brique': 'Click again to delete the brick', 'La bibliothèque est vide': 'The library is empty',
    'Préparation du ZIP…': 'Preparing the ZIP…', 'Brique introuvable dans la bibliothèque': 'Brick not found in the library',
    'Elle disparaît de la bibliothèque et des figurines où elle est posée.': 'It is removed from the library and from the figures it is on.',
    'Aucune brique dans cette catégorie.': 'No brick in this category.',
    'La bibliothèque est vide. Dépose les fichiers d\'une première brique dans l\'aperçu.': 'The library is empty. Drop the files of a first brick in the preview.',
    'Socket libre : choisis une brique dans la bibliothèque.': 'Free socket: pick a brick in the library.',
    'Sélectionne une brique posée (clic dans le viewport ou dans le bandeau Assemblage) pour la régler. Position en cm, relative au socket.': 'Select a placed brick (click in the viewport or in the Assembly strip) to adjust it. Position in cm, relative to the socket.',
    'Pièce du kit': 'Kit piece', '[PRIX]': '[PRICE]', 'pouces': 'inches', 'SOCKET_type_nom': 'SOCKET_type_name', 'Trois-quarts': 'Three-quarter', 'dents': 'teeth', 'Lance-rokkit': 'Rokkit Launcha', 'Version de': 'Version of', 'Avant-bras': 'Forearm',
    'Aucun socket': 'No socket', 'Maillage fermé (imprimable)': 'Closed mesh (printable)',
    'Dépose une scène Maya .ma / .mb, des modèles .fbx / .obj / .glb ou une texture .jpg / .png': 'Drop a Maya scene .ma / .mb, .fbx / .obj / .glb models or a .jpg / .png texture',

    // ---------------------------------------------------------------- Builder
    'Annuler (Ctrl+Z)': 'Undo (Ctrl+Z)', 'Rétablir (Ctrl+Y)': 'Redo (Ctrl+Y)', 'Exporter en STL': 'Export as STL', 'Options d\'export': 'Export options',
    'Équipement de l\'ork': 'Ork gear', 'Catégories': 'Categories', 'Fermer la bibliothèque': 'Close the library', 'Importer des briques': 'Import bricks',
    'Viewport': 'Viewport', 'Sélection (Q)': 'Select (Q)', 'Déplacer (G)': 'Move (G)', 'Tourner (R)': 'Rotate (R)', 'Échelle (S)': 'Scale (S)',
    'Étapes': 'Steps', 'Sockets': 'Sockets', 'Squelette': 'Skeleton', 'Outils de la vue': 'View tools', 'Vues caméra': 'Camera views',
    'Peint': 'Painted', 'Tourner la lumière': 'Rotate the light', 'Plein écran': 'Full screen', 'Personnaliser': 'Customize',
    'Retirer la pièce': 'Remove the piece', 'Onglets': 'Tabs', 'Transformation': 'Transform', 'Ork': 'Ork', 'Définition': 'Detail level',
    'Basemesh low et version low des briques': 'Low basemesh and low version of the bricks', 'High-poly de l\'ork et des briques': 'High-poly of the ork and bricks',
    'Fermeture de la main gauche': 'Left hand grip', 'Fermeture de la main droite': 'Right hand grip', 'Assemblage': 'Assembly', 'Poses': 'Poses',
    'Nom du projet': 'Project name', 'Un seul STL (figurine fusionnée)': 'Single STL (merged figure)',
    'Pièces séparées (un STL par emplacement)': 'Separate pieces (one STL per slot)', 'Projet (.json)': 'Project (.json)', 'Équipement': 'Gear',
    'Clic : choisir le socket et ouvrir la bibliothèque. Les numéros sont ceux des pastilles sur l\'ork.': 'Click: pick the socket and open the library. Numbers match the pins on the ork.',
    'Clic : la brique va sur son socket. Glisser vers le viewport : choisir le socket.': 'Click: the brick goes on its socket. Drag to the viewport: pick the socket.',
    'Clic : poser · Glisser : choisir le socket': 'Click: place · Drag: pick the socket',
    'Pose': 'Pose', 'Rendu': 'Render', 'Qualité de rendu': 'Render quality', 'Temps réel': 'Real time', 'Haute qualité': 'High quality',
    'Grille': 'Grid', 'Échelle': 'Scale', 'Transfo': 'Transform', 'Impression': 'Print', 'Miroir': 'Mirror', 'Réinitialiser': 'Reset',
    'Hauteur': 'Height', 'Triangles': 'Triangles', 'Pièces': 'Pieces', 'Exporter la figurine fusionnée': 'Export the merged figure',
    'Exporter les pièces séparées': 'Export the separate pieces', 'Scène': 'Scene', 'Figurine du builder': 'Builder figure',
    'Modèle test : Grot Bomber': 'Test model: Grot Bomber', 'Charger une texture basecolor (.jpg, .png)': 'Load a basecolor texture (.jpg, .png)',
    'Transformée': 'Tone mapping', 'Exposition': 'Exposure', 'Vignettage': 'Vignette', 'Environnement': 'Environment', 'Image': 'Image',
    'Studio': 'Studio', 'Coucher de soleil': 'Sunset', 'Entrepôt': 'Warehouse', 'Ciel procédural': 'Procedural sky',
    'Charger un HDRI (.hdr, .exr)': 'Load an HDRI (.hdr, .exr)', 'Intensité': 'Intensity', 'Rotation': 'Rotation', 'Visible en fond': 'Visible as background',
    'Lumières studio (key, fill, rim)': 'Studio lights (key, fill, rim)', 'Intensité GI': 'GI intensity',
    'Figer la GI pendant le déplacement': 'Freeze GI while moving', 'Occlusion (SSAO)': 'Occlusion (SSAO)', 'Rayon SSAO': 'SSAO radius',
    'Rebonds': 'Bounces', 'Filtre glossy': 'Glossy filter', 'Débruitage': 'Denoise', 'Profondeur de champ (macro)': 'Depth of field (macro)',
    'Ouverture': 'Aperture', 'Poser dans le Rig Lab': 'Pose in the Rig Lab', 'Face': 'Front', 'Profil': 'Side', 'Position': 'Position',
    'Tête d\'origine': 'Original head', 'Vide': 'Empty', '· libre': '· free', '· chargement…': '· loading…', '· préparation…': '· preparing…',
    'HDRI (à charger)': 'HDRI (to load)', 'au repos': 'at rest', 'figée': 'frozen',
    'Aucune brique posée : l\'ork est nu. Clique une brique de la bibliothèque.': 'No brick placed: the ork is bare. Click a brick in the library.',
    'Aucune brique ne correspond à la recherche.': 'No brick matches the search.',
    'Export en cours…': 'Exporting…', 'Projet exporté': 'Project exported', 'Chargement du modèle test…': 'Loading the test model…',
    'Modèle test introuvable': 'Test model not found', 'Bibliothèque indisponible (stockage du navigateur bloqué)': 'Library unavailable (browser storage blocked)',

    // ---------------------------------------------------------------- Rig Lab
    'Rotation des os (R)': 'Rotate bones (R)', 'Déplacer en IK : mains, pieds, bassin (W)': 'Move in IK: hands, feet, pelvis (W)',
    'Maillage affiché': 'Displayed mesh', 'Afficher les os (B)': 'Show bones (B)', 'Poids de l\'os (H)': 'Bone weights (H)',
    'Os sélectionné': 'Selected bone', 'Remettre l\'os au repos': 'Reset the bone to rest', 'Copie la rotation de cet os sur l\'os opposé': 'Copy this bone\'s rotation to the opposite bone',
    'Tout remettre au repos': 'Reset everything to rest', 'Échange gauche et droite': 'Swap left and right',
    'Copie le côté gauche sur le droit': 'Copy the left side to the right', 'Copie le côté droit sur le gauche': 'Copy the right side to the left',
    'Nom de la pose': 'Pose name', 'Figurine posée (STL)': 'Posed figure (STL)', 'Pose (.json)': 'Pose (.json)', 'Importer une pose (.json)…': 'Import a pose (.json)…',
    'Clique sur la figurine ou sur un os de la liste. Caméra : clic gauche glissé pour tourner, clic milieu pour déplacer, clic droit pour zoomer, F pour cadrer.': 'Click the figure or a bone in the list. Camera: left-drag to orbit, middle-click to pan, right-click to zoom, F to frame.',
    'Low (rig Maya)': 'Low (Maya rig)', 'Symétrie auto': 'Auto symmetry', 'Pieds ancrés': 'Feet planted', 'Os': 'Bones', 'Aucun os': 'No bone',
    'Symétrie': 'Symmetry', 'Os → opposé': 'Bone → opposite', 'Poses de départ': 'Starter poses', 'Mes poses': 'My poses', 'Enregistrer la pose': 'Save the pose',
    'Clique sur une partie de l\'ork pour la sélectionner.': 'Click a part of the ork to select it.', 'Sélectionne un os pour voir ses poids': 'Select a bone to see its weights',
    'Aucune pose enregistrée.': 'No saved pose.', 'Ce fichier n\'est pas une pose du Rig Lab': 'This file is not a Rig Lab pose', 'pas une pose': 'not a pose',
    'Corps': 'Body', 'Bras gauche': 'Left arm', 'Bras droit': 'Right arm', 'Jambe gauche': 'Left leg', 'Jambe droite': 'Right leg',
    'Bassin': 'Pelvis', 'Bas du dos': 'Lower back', 'Cou': 'Neck', 'Clavicule': 'Clavicle', 'Bras': 'Arm', 'Avant-bras': 'Forearm', 'Poignet': 'Wrist',
    'Cuisse': 'Thigh', 'Genou': 'Knee', 'Cheville': 'Ankle', 'Orteils': 'Toes', 'Pouce': 'Thumb', 'Index': 'Index', 'Majeur': 'Middle',
    // ---------------------------------------------------------------- Builder : mode Pose ; Rig Lab : sockets, meshes
    'Mode du Builder': 'Builder mode', 'Build : poser les briques sur l\'ork': 'Build: put bricks on the ork', 'Pose : poser l\'ork avec son squelette, les briques suivent': 'Pose: pose the ork with its skeleton, bricks follow',
    'Tourner l\'os (E)': 'Rotate the bone (E)', 'IK : déplacer main, pied, bassin (W)': 'IK: move hand, foot, hips (W)', 'Enregistrer la pose actuelle': 'Save the current pose',
    'Clic : appliquer la pose. Clic droit sur une de tes poses : renommer, supprimer.': 'Click: apply the pose. Right click one of your poses: rename, delete.',
    'Pose A': 'A-pose', 'Pose T': 'T-pose', 'Neutre': 'Neutral', 'Réinitialiser l\'os': 'Reset the bone', 'Copier sur l\'autre côté (M)': 'Copy to the other side (M)',
    'Pieds ancrés au sol': 'Feet pinned to the ground', 'Mains': 'Hands', 'Jambe': 'Leg', 'Miroir de la pose': 'Pose mirror',
    'Le côté gauche est copié sur le droit': 'The left side is copied to the right', 'Le côté droit est copié sur le gauche': 'The right side is copied to the left',
    'G → D': 'L → R', 'D → G': 'R → L', 'Inverser': 'Swap',
    'Main ouverte': 'Open hand', 'Main détendue': 'Relaxed hand', 'Prise d\'arme': 'Weapon grip', 'Poing fermé': 'Closed fist', 'Poignet droit': 'Straight wrist',
    'Bras le long du corps': 'Arm down', 'Bras en A': 'A arm', 'Bras en T': 'T arm', 'Bras devant': 'Arm forward', 'Bras levé': 'Arm raised', 'Coude plié': 'Bent elbow',
    'Jambe tendue': 'Straight leg', 'Pas en avant': 'Step forward', 'Pas en arrière': 'Step back', 'Genou levé': 'Knee up',
    'Tête droite': 'Head straight', 'Regarder à gauche': 'Look left', 'Regarder à droite': 'Look right', 'Regarder en haut': 'Look up', 'Regarder en bas': 'Look down',
    'Dos droit': 'Straight back', 'Penché en avant': 'Leaning forward', 'Torse bombé': 'Chest out', 'Torsion à gauche': 'Twist left', 'Torsion à droite': 'Twist right',
    'Accroupi': 'Crouched', 'Debout': 'Standing', 'Mains ouvertes': 'Open hands', 'Poings fermés': 'Closed fists',
    'Meshes': 'Meshes', 'Afficher les sockets': 'Show sockets', 'Tout réinitialiser': 'Reset all', 'Position (cm)': 'Position (cm)', 'Rotation (°)': 'Rotation (°)',
    'Position X': 'Position X', 'Position Y': 'Position Y', 'Position Z': 'Position Z', 'Rotation X': 'Rotation X', 'Rotation Y': 'Rotation Y', 'Rotation Z': 'Rotation Z',
    'Redresser': 'Straighten', 'Flèche vers le haut, avant de la figurine': 'Arrow up, facing the figure\'s front',
    'Corps low': 'Body low', 'Corps high': 'Body high', 'Tête low': 'Head low', 'Tête high': 'Head high', 'Yeux': 'Eyes', 'Dents': 'Teeth',
    'Clique la boule d\'un socket (ou la liste). W : déplacer, E : tourner. La flèche est l\'axe Y : le haut de la brique, le manche d\'une arme modélisée debout dans Maya. Le réglage est relatif au joint et sert au Builder.': 'Click a socket ball (or the list). W: move, E: rotate. The arrow is the Y axis: the top of the brick, the handle of a weapon modeled upright in Maya. The setting is relative to the joint and is used by the Builder.',
    'Glisse un OBJ exporté de Maya sur un slot (cm, Y en haut, l\'ork à sa place). Il remplace ce maillage et suit le même rig : poids repris du maillage d\'origine. Les sockets et les poses restent. « Exporter OBJ » donne la référence à reprendre dans Maya.': 'Drop an OBJ exported from Maya on a slot (cm, Y up, the ork in place). It replaces that mesh and follows the same rig: weights come from the original mesh. Sockets and poses stay. "Export OBJ" gives the reference to rework in Maya.',
    'Annulaire': 'Ring', 'Auriculaire': 'Pinky', 'Repos': 'Rest', 'Garde': 'Guard', 'Charge': 'Charge', 'Waaagh !': 'Waaagh!', 'Tir': 'Shooting',
  };

  // Textes qui commencent par une partie fixe suivie d'une valeur (erreur, nom…) : la partie fixe est traduite.
  var PREFIX = [
    ['Impossible de renommer :', 'Couldn\'t rename:'], ['Impossible de supprimer :', 'Couldn\'t delete:'],
    ['Conversion Maya impossible :', 'Maya conversion failed:'], ['Enregistrement impossible :', 'Couldn\'t save:'],
    ['Export impossible ici :', 'Export not possible here:'], ['Export impossible :', 'Export failed:'],
    ['Ce fichier n\'est pas une bibliothèque de briques :', 'This file is not a brick library:'], ['Envoi Maya illisible :', 'Unreadable Maya send:'],
    ['Path tracing indisponible :', 'Path tracing unavailable:'], ['ZIP compressé non pris en charge :', 'Compressed ZIP not supported:'],
    ['GLB introuvable :', 'GLB not found:'], ['Impossible de charger assets/ork_boy.glb.', 'Couldn\'t load assets/ork_boy.glb. Serve the web/plasma-builder folder with a small server (python -m http.server) and check that the file is there.', true],
    ['Scène Maya introuvable sur le disque.', 'Maya scene not found on disk.'], ['Maya (mayapy.exe) introuvable', 'Maya (mayapy.exe) not found: install Maya or set PLASMA_MAYAPY.', true],
    ['Outil PlasmaBridge introuvable', 'PlasmaBridge tool not found in Maya\'s scripts (PlasmaBridge\\cli.py).', true],
    ['Maya n\'a pas répondu en 5 minutes.', 'Maya did not answer within 5 minutes.'], ['mayapy n\'a pas pu démarrer :', 'mayapy could not start:'],
    ['La conversion Maya a échoué', 'The Maya conversion failed'], ['envoi trop lourd (plus de 1,5 Go)', 'send too heavy (over 1.5 GB)'],
    ['aucun fichier lisible dans', 'no readable file in'],
    ['Vide ·', 'Empty ·'], ['HDRI ·', 'HDRI ·'], ['ZIP illisible', 'Unreadable ZIP'], ['pas une bibliothèque de briques', 'not a brick library'],
  ];

  var norm = function (s) { return s.replace(/\s+/g, ' ').replace(/[’]/g, '\'').trim(); };
  var D = {};
  for (var k in DICT) D[norm(k)] = DICT[k];

  // Traduction d'un texte isolé ; null s'il n'y a rien à traduire.
  function lookup(s) {
    var k = norm(s);
    if (!k) return null;
    if (Object.prototype.hasOwnProperty.call(D, k)) return D[k] === k ? null : D[k];
    for (var i = 0; i < PREFIX.length; i++) {
      var p = PREFIX[i];
      if (k.indexOf(p[0]) === 0) return p[2] ? p[1] : p[1] + k.slice(p[0].length);
    }
    return null;
  }
  function t(s) { if (lang === 'fr' || s == null) return s; var r = lookup(String(s)); return r == null ? s : r; }

  window.plasmaLang = lang;
  window.plasmaSetLang = function (l) { try { localStorage.setItem(KEY, l); } catch (e) { /* idem */ } location.reload(); };
  // Messages construits avec des valeurs : le JS donne les deux langues.
  window.tr = function (fr, en) { return lang === 'en' ? en : fr; };
  window.plasmaT = t;
  if (lang === 'fr') return;

  var ATTRS = ['title', 'aria-label', 'placeholder', 'alt', 'label'];
  var skip = function (el) { return el && el.closest && el.closest('[data-noi18n], script, style, textarea'); };
  function textNode(n) {
    var r = lookup(n.data);
    if (r == null || skip(n.parentNode)) return;
    var m = /^(\s*)[\s\S]*?(\s*)$/.exec(n.data);
    n.data = m[1] + r + m[2];
  }
  function attrs(el) {
    for (var i = 0; i < ATTRS.length; i++) {
      var a = ATTRS[i], v = el.getAttribute(a);
      if (v) { var r = lookup(v); if (r != null) el.setAttribute(a, r); }
    }
  }
  function walk(root) {
    if (root.nodeType === 3) { textNode(root); return; }
    if (root.nodeType !== 1 || skip(root)) return;
    attrs(root);
    var w = document.createTreeWalker(root, 5 /* éléments et textes */);
    var n;
    while ((n = w.nextNode())) {
      if (n.nodeType === 3) textNode(n);
      else if (n.closest('[data-noi18n]')) continue;
      else attrs(n);
    }
  }
  var titleFix = function () { var r = lookup(document.title); if (r != null) document.title = r; };
  new MutationObserver(function (list) {
    for (var i = 0; i < list.length; i++) {
      var m = list[i];
      if (m.type === 'childList') { for (var j = 0; j < m.addedNodes.length; j++) walk(m.addedNodes[j]); if (m.target.nodeName === 'TITLE') titleFix(); }
      else if (m.type === 'characterData') { textNode(m.target); if (m.target.parentNode && m.target.parentNode.nodeName === 'TITLE') titleFix(); }
      else if (m.type === 'attributes') { var v = m.target.getAttribute(m.attributeName); if (v) { var r = lookup(v); if (r != null && !skip(m.target)) m.target.setAttribute(m.attributeName, r); } }
    }
  }).observe(html, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ATTRS });
  walk(html);
  document.addEventListener('DOMContentLoaded', function () { walk(document.body); titleFix(); });
})();
