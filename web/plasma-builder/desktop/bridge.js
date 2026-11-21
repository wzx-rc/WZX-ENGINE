// Passerelle Maya -> Importeur, édition développeur seulement (voir README).
//
// L'étagère PlasmaOrks de Maya (outil PlasmaBridge, dans le dossier scripts de Maya) écrit chaque envoi dans un dossier
// de la boîte de réception : <Documents>\PlasmaOrks\Bridge\inbox\<nom>_<date>\ avec le .fbx (maillages + locators
// PLUG_ / SOCKET_), la ou les textures et manifest.json, écrit en dernier (le dossier est renommé une fois complet).
// Puis Maya lance « Plasma Orks Builder.exe --plasma-import <dossier> » : l'app déjà ouverte reçoit la ligne de commande
// (second-instance), sinon elle démarre avec. La boîte de réception est aussi surveillée tant que l'app tourne.
// Dans les deux cas : fenêtre au premier plan, page Importeur, fichiers donnés à la page comme s'ils étaient déposés.
//
// Une scène .ma / .mb déposée dans l'Importeur passe par mayapy (Maya sans interface) et le cli.py de PlasmaBridge, qui
// écrit le même dossier ; la dernière ligne « PLASMA_RESULT {...} » de sa sortie donne le dossier ou l'erreur.
const { app, ipcMain } = require('electron');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const FILE_EXT = /\.(fbx|obj|mtl|glb|gltf|bin|json|jpe?g|png|webp|tga)$/i;
const MAX_BYTES = 1.5 * 1024 ** 3;
const KEEP_DAYS = 7;

const inboxDir = () => path.join(app.getPath('documents'), 'PlasmaOrks', 'Bridge', 'inbox');
const argFolder = argv => { const i = argv.indexOf('--plasma-import'); return i >= 0 && argv[i + 1] ? path.resolve(argv[i + 1]) : null; };

function setupBridge({ getWindow, importerUrl, version, edition }) {
  const handled = new Set();     // dossiers déjà donnés à l'Importeur (ligne de commande et surveillance se recoupent)
  const pending = [];
  let ready = null;              // webContents de l'Importeur prêt à recevoir

  const log = (...a) => console.log('[bridge]', ...a);

  function show() {
    const win = getWindow();
    if (!win) return null;
    if (win.isMinimized()) win.restore();
    win.show();
    win.focus();
    return win;
  }
  function flush() {
    if (!ready || ready.isDestroyed()) return;
    while (pending.length) ready.send('bridge:files', pending.shift());
  }
  function deliver(payload) {
    pending.push(payload);
    const win = show();
    if (!win) return;
    let onImporter = false;
    try { onImporter = /\/import\.html$/.test(new URL(win.webContents.getURL()).pathname); } catch { /* page pas encore chargée */ }
    if (ready === win.webContents) flush();
    else if (!onImporter || !win.webContents.isLoading()) win.loadURL(importerUrl);
    // sinon l'Importeur est en train de charger : il réclamera les fichiers une fois l'ork chargé (bridge:ready)
  }
  // La page change : l'Importeur précédent ne reçoit plus rien jusqu'à son prochain bridge:ready.
  function attach(win) { win.webContents.on('did-navigate', () => { ready = null; }); }

  function readFolder(folder) {
    let manifest = null;
    try { manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json'), 'utf8')); } catch { /* dossier sans manifest : on prend ses fichiers */ }
    const names = manifest?.files?.length ? manifest.files : fs.readdirSync(folder).filter(n => n !== 'manifest.json');
    const files = [];
    let total = 0;
    for (const n of names) {
      const base = path.basename(n);
      if (!FILE_EXT.test(base)) continue;
      const p = path.join(folder, base);
      if (!fs.existsSync(p)) continue;
      total += fs.statSync(p).size;
      if (total > MAX_BYTES) throw new Error('envoi trop lourd (plus de 1,5 Go)');
      files.push({ name: base, data: fs.readFileSync(p) });
    }
    if (!files.length) throw new Error('aucun fichier lisible dans ' + folder);
    return { name: manifest?.name || path.basename(folder).replace(/_\d{8}-\d{6}$/, ''), manifest, folder, files };
  }
  function importFolder(folder, why) {
    const key = path.resolve(folder).toLowerCase();
    if (handled.has(key)) return;
    handled.add(key);
    try {
      const payload = readFolder(folder);
      log(`import (${why})`, folder, payload.files.map(f => f.name).join(', '));
      deliver(payload);
    } catch (err) {
      log('échec', folder, err.message);
      deliver({ error: `Envoi Maya illisible : ${err.message}`, folder, files: [] });
    }
  }

  // ------------------------------------------------ ligne de commande, instance unique
  const first = argFolder(process.argv);
  if (first) app.whenReady().then(() => importFolder(first, 'lancement'));
  app.on('second-instance', (_e, argv) => {
    const f = argFolder(argv);
    if (f) importFolder(f, 'ligne de commande'); else show();
  });

  // ------------------------------------------------ boîte de réception
  app.whenReady().then(() => {
    const inbox = inboxDir();
    try { fs.mkdirSync(inbox, { recursive: true }); } catch (err) { log('boîte de réception impossible', err.message); return; }
    // ménage : les envois de plus de 7 jours ; ceux déjà présents ne sont pas réimportés au démarrage
    const old = Date.now() - KEEP_DAYS * 864e5;
    for (const n of fs.readdirSync(inbox)) {
      const p = path.join(inbox, n);
      try {
        const st = fs.statSync(p);
        if (st.isDirectory() && st.mtimeMs < old) fs.rmSync(p, { recursive: true, force: true });
        else handled.add(p.toLowerCase());
      } catch { /* dossier en cours d'écriture */ }
    }
    const timers = new Map();
    try {
      fs.watch(inbox, (_type, name) => {
        if (!name || name.startsWith('.')) return;
        clearTimeout(timers.get(name));
        timers.set(name, setTimeout(() => {
          timers.delete(name);
          const p = path.join(inbox, name);
          if (fs.existsSync(path.join(p, 'manifest.json'))) importFolder(p, 'boîte de réception');
        }, 400));
      });
    } catch (err) { log('surveillance impossible', err.message); }
    // Où trouver l'app et la boîte de réception, pour l'étagère Maya.
    const info = path.join(app.getPath('appData'), 'PlasmaOrksBuilder', 'bridge.json');
    try {
      fs.mkdirSync(path.dirname(info), { recursive: true });
      fs.writeFileSync(info, JSON.stringify({ exe: process.execPath, inbox, edition, version, pid: process.pid, started: new Date().toISOString() }, null, 2));
    } catch (err) { log('bridge.json impossible', err.message); }
  });

  ipcMain.on('bridge:ready', e => { ready = e.sender; flush(); });

  // ------------------------------------------------ scènes Maya déposées dans l'Importeur
  ipcMain.handle('maya:convert', async (_e, scene) => {
    if (!scene || !/\.(ma|mb)$/i.test(scene) || !fs.existsSync(scene)) return { error: 'Scène Maya introuvable sur le disque.' };
    const tools = findMaya();
    if (tools.error) return tools;
    log('conversion', scene, 'avec', tools.mayapy, tools.cli);
    const res = await runCli(tools, scene);
    if (res.folder) importFolder(res.folder, 'scène Maya');
    return res;
  });

  return { attach };
}

// mayapy.exe et cli.py de PlasmaBridge : la version de Maya la plus récente qui a les deux, sauf variables
// PLASMA_MAYAPY / PLASMA_BRIDGE_CLI.
function findMaya() {
  const versions = dir => { try { return fs.readdirSync(dir).map(n => /^(?:Maya)?(\d{4})$/i.exec(n)?.[1]).filter(Boolean).sort().reverse(); } catch { return []; } };
  const pyRoots = [process.env.ProgramFiles || 'C:\\Program Files', 'C:\\Program Files'].map(p => path.join(p, 'Autodesk'));
  const userRoots = [process.env.MAYA_APP_DIR, path.join(app.getPath('documents'), 'maya'), path.join(app.getPath('home'), 'Documents', 'maya')].filter(Boolean);
  const mayapyFor = v => pyRoots.map(r => path.join(r, 'Maya' + v, 'bin', 'mayapy.exe')).find(p => fs.existsSync(p));
  const cliFor = v => userRoots.map(r => path.join(r, v, 'scripts', 'PlasmaBridge', 'cli.py')).find(p => fs.existsSync(p))
    || userRoots.map(r => path.join(r, 'scripts', 'PlasmaBridge', 'cli.py')).find(p => fs.existsSync(p));
  let mayapy = process.env.PLASMA_MAYAPY, cli = process.env.PLASMA_BRIDGE_CLI;
  const all = [...new Set(pyRoots.flatMap(versions))].sort().reverse();
  for (const v of all) {
    if (mayapy && cli) break;
    const p = mayapyFor(v);
    if (!p) continue;
    const c = cli || cliFor(v);
    if (c) { mayapy ||= p; cli = c; }
  }
  if (!mayapy || !fs.existsSync(mayapy)) return { error: 'Maya (mayapy.exe) introuvable : installe Maya ou renseigne PLASMA_MAYAPY.' };
  if (!cli || !fs.existsSync(cli)) return { error: 'Outil PlasmaBridge introuvable dans les scripts de Maya (PlasmaBridge\\cli.py).' };
  return { mayapy, cli };
}

function runCli({ mayapy, cli }, scene) {
  return new Promise(resolve => {
    let out = '', err = '';
    const p = spawn(mayapy, [cli, scene], { windowsHide: true });
    const timer = setTimeout(() => { p.kill(); resolve({ error: 'Maya n\'a pas répondu en 5 minutes.' }); }, 5 * 60e3);
    p.stdout.on('data', d => { out += d; });
    p.stderr.on('data', d => { err += d; });
    p.on('error', e => { clearTimeout(timer); resolve({ error: 'mayapy n\'a pas pu démarrer : ' + e.message }); });
    p.on('close', code => {
      clearTimeout(timer);
      const line = out.split(/\r?\n/).reverse().find(l => l.startsWith('PLASMA_RESULT '));
      let res = null;
      try { res = line ? JSON.parse(line.slice('PLASMA_RESULT '.length)) : null; } catch { /* sortie illisible */ }
      if (res && (res.folder || res.error)) return resolve(res);
      const tail = (err || out).trim().split(/\r?\n/).slice(-3).join(' ');
      resolve({ error: `La conversion Maya a échoué (code ${code})${tail ? ' : ' + tail : ''}` });
    });
  });
}

module.exports = { setupBridge, argFolder, findMaya, runCli };
