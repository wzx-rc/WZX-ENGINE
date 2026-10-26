// Prépare desktop/app/ : copie du builder web, dépendances embarquées (hors ligne) et importmap réécrit.
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.join(here, '..');
const app = path.join(here, 'app');
fs.rmSync(app, { recursive: true, force: true });
fs.mkdirSync(app, { recursive: true });
for (const d of ['css', 'js', 'assets']) fs.cpSync(path.join(web, d), path.join(app, d), { recursive: true });

const vendor = (pkg, sub) => fs.cpSync(path.join(here, 'node_modules', pkg, sub), path.join(app, 'vendor', pkg, sub), { recursive: true });
vendor('three', 'build');
vendor('three', 'examples/jsm');
vendor('three-mesh-bvh', 'build');
vendor('three-gpu-pathtracer', 'build');

let html = fs.readFileSync(path.join(web, 'index.html'), 'utf8');
html = html.replace(/https:\/\/cdn\.jsdelivr\.net\/npm\/(three|three-mesh-bvh|three-gpu-pathtracer)@[^/]+\//g, './vendor/$1/');
fs.writeFileSync(path.join(app, 'index.html'), html);

// Modèle test HD : posé à côté du builder s'il a été généré (tools/mb_mesh, voir README).
const hd = path.join(here, 'assets', 'boy_32mm_hd.glb');
if (fs.existsSync(hd)) fs.copyFileSync(hd, path.join(app, 'assets', 'test', 'boy_32mm_hd.glb'));
console.log('app/ prêt' + (fs.existsSync(hd) ? ' (modèle test HD inclus)' : ''));
