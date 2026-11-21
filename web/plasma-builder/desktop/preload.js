// Signale au builder qu'il tourne dans l'app Windows (mode Path tracing proposé, barre de titre maison) et donne
// à la page les boutons de fenêtre : réduire, agrandir / restaurer, fermer.
// Édition développeur : passerelle Maya (fichiers envoyés par l'étagère PlasmaOrks, scènes .ma / .mb converties).
const { contextBridge, ipcRenderer, webUtils } = require('electron');
const arg = k => (process.argv.find(a => a.startsWith(`--plasma-${k}=`)) || '').split('=').slice(1).join('=');
const edition = arg('edition') === 'dev' ? 'dev' : 'public';
const api = {
  platform: process.platform, version: arg('version') || '0.3.0', edition,
  win: {
    min: () => ipcRenderer.send('win:min'),
    max: () => ipcRenderer.send('win:max'),
    close: () => ipcRenderer.send('win:close'),
    onState: cb => ipcRenderer.on('win:state', (_e, maximized) => cb(!!maximized)),
  },
};
if (edition === 'dev') {
  api.maya = {
    // fichiers d'une brique envoyée depuis Maya : { name, manifest, files: [{ name, data: Uint8Array }] } ou { error }
    onFiles: cb => ipcRenderer.on('bridge:files', (_e, payload) => cb(payload)),
    // l'Importeur est prêt (ork chargé) : l'app lui donne les envois en attente
    ready: () => ipcRenderer.send('bridge:ready'),
    // scène .ma / .mb déposée : convertie par Maya en arrière-plan, le résultat arrive par onFiles
    convert: file => ipcRenderer.invoke('maya:convert', webUtils.getPathForFile(file)),
  };
}
contextBridge.exposeInMainWorld('plasmaDesktop', api);
