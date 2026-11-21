// Plasma Orks Builder pour Windows : fenêtre Electron qui sert le builder depuis app:// (modules ES, hors ligne),
// GPU dédié demandé pour le path tracing.
const { app, BrowserWindow, protocol, net, shell, Menu, ipcMain } = require('electron');
const path = require('path');
const { pathToFileURL } = require('url');
const pkg = require('./package.json');

// Deux éditions : « dev » (pour Wzx, PC : passerelle Maya, import de scènes .ma / .mb) et « public » (sans ces outils).
// Réglée par plasmaEdition dans package.json, ou PLASMA_EDITION au lancement.
const EDITION = (process.env.PLASMA_EDITION || pkg.plasmaEdition || 'public') === 'dev' ? 'dev' : 'public';

// Une seule fenêtre : relancer l'exe (raccourci, ou Maya avec --plasma-import) ramène l'app déjà ouverte.
if (!app.requestSingleInstanceLock()) { app.quit(); return; }

app.commandLine.appendSwitch('force_high_performance_gpu');
app.commandLine.appendSwitch('ignore-gpu-blocklist');
protocol.registerSchemesAsPrivileged([{ scheme: 'app', privileges: { standard: true, secure: true, supportFetchAPI: true, corsEnabled: true } }]);

const ROOT = path.join(__dirname, 'app');
let mainWin = null;
const bridge = EDITION === 'dev'
  ? require('./bridge').setupBridge({ getWindow: () => mainWin, importerUrl: 'app://builder/import.html', version: pkg.version, edition: EDITION })
  : null;
if (!bridge) app.on('second-instance', () => { if (mainWin) { if (mainWin.isMinimized()) mainWin.restore(); mainWin.focus(); } });

function createWindow() {
  const win = new BrowserWindow({
    width: 1600, height: 1000, minWidth: 1100, minHeight: 700,
    backgroundColor: '#0e0f11', title: 'Plasma Orks Builder', autoHideMenuBar: true,
    icon: path.join(__dirname, 'app', 'assets', 'icon', 'plasma-orks.ico'),
    // Barre de titre maison (dessinée par la page, voir js/chrome.js), boutons réduire / agrandir / fermer
    // compris (icônes Recraft) : pas de barre ni de boutons natifs, la fenêtre reste redimensionnable.
    titleBarStyle: 'hidden',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true,
      additionalArguments: ['--plasma-edition=' + EDITION, '--plasma-version=' + pkg.version],
    },
  });
  mainWin = win;
  win.on('closed', () => { if (mainWin === win) mainWin = null; });
  bridge?.attach(win);
  // lancée par Maya (--plasma-import) : directement sur l'Importeur
  const startPage = bridge && require('./bridge').argFolder(process.argv) ? 'import.html' : 'index.html';
  win.loadURL('app://builder/' + startPage);
  const state = () => win.webContents.send('win:state', win.isMaximized());
  win.on('maximize', state); win.on('unmaximize', state);
  win.webContents.on('did-finish-load', state);
  win.webContents.setWindowOpenHandler(({ url }) => { shell.openExternal(url); return { action: 'deny' }; });
  win.webContents.on('before-input-event', (e, input) => {
    if (input.type === 'keyDown' && input.key === 'F11') { win.setFullScreen(!win.isFullScreen()); e.preventDefault(); }
    if (input.type === 'keyDown' && input.key === 'F12') { win.webContents.toggleDevTools(); e.preventDefault(); }
  });
}

// Boutons de la barre de titre maison.
ipcMain.on('win:min', e => BrowserWindow.fromWebContents(e.sender)?.minimize());
ipcMain.on('win:max', e => { const w = BrowserWindow.fromWebContents(e.sender); if (w) w.isMaximized() ? w.unmaximize() : w.maximize(); });
ipcMain.on('win:close', e => BrowserWindow.fromWebContents(e.sender)?.close());

app.whenReady().then(() => {
  Menu.setApplicationMenu(null);
  protocol.handle('app', req => {
    const rel = decodeURIComponent(new URL(req.url).pathname);
    const file = path.normalize(path.join(ROOT, rel));
    if (!file.startsWith(ROOT)) return new Response('interdit', { status: 403 });
    return net.fetch(pathToFileURL(file).toString());
  });
  createWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});
app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
