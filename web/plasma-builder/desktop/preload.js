// Signale au builder qu'il tourne dans l'app Windows (mode Path tracing proposé, barre de titre maison) et donne
// à la page les boutons de fenêtre : réduire, agrandir / restaurer, fermer.
const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('plasmaDesktop', {
  platform: process.platform, version: '0.3.0',
  win: {
    min: () => ipcRenderer.send('win:min'),
    max: () => ipcRenderer.send('win:max'),
    close: () => ipcRenderer.send('win:close'),
    onState: cb => ipcRenderer.on('win:state', (_e, maximized) => cb(!!maximized)),
  },
});
