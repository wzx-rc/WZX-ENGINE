// Signale au builder qu'il tourne dans l'app Windows : le mode Path tracing y est proposé.
const { contextBridge } = require('electron');
contextBridge.exposeInMainWorld('plasmaDesktop', { platform: process.platform, version: '0.2.0' });
