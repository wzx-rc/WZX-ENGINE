// Plasma Orks Builder pour Windows : fenêtre Electron qui sert le builder depuis app:// (modules ES, hors ligne),
// GPU dédié demandé pour le path tracing.
const { app, BrowserWindow, protocol, net, shell, Menu, ipcMain } = require('electron');
const path = require('path');
const { pathToFileURL } = require('url');

app.commandLine.appendSwitch('force_high_performance_gpu');
app.commandLine.appendSwitch('ignore-gpu-blocklist');
protocol.registerSchemesAsPrivileged([{ scheme: 'app', privileges: { standard: true, secure: true, supportFetchAPI: true, corsEnabled: true } }]);

const ROOT = path.join(__dirname, 'app');

function createWindow() {
  const win = new BrowserWindow({
    width: 1600, height: 1000, minWidth: 1100, minHeight: 700,
    backgroundColor: '#0e0f11', title: 'Plasma Orks Builder', autoHideMenuBar: true,
    icon: path.join(__dirname, 'app', 'assets', 'icon', 'plasma-orks.ico'),
    // Barre de titre maison (dessinée par la page, voir js/chrome.js), boutons réduire / agrandir / fermer
    // compris (icônes Recraft) : pas de barre ni de boutons natifs, la fenêtre reste redimensionnable.
    titleBarStyle: 'hidden',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true },
  });
  win.loadURL('app://builder/index.html');
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
