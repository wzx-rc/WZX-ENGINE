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

for (const page of ['index.html', 'rig.html', 'import.html', 'hub.html', 'library.html']) {
  let html = fs.readFileSync(path.join(web, page), 'utf8');
  html = html.replace(/https:\/\/cdn\.jsdelivr\.net\/npm\/(three|three-mesh-bvh|three-gpu-pathtracer)@[^/]+\//g, './vendor/$1/');
  fs.writeFileSync(path.join(app, page), html);
}

// Versions HD réservées à l'app (non versionnées) : sculpts complets et texture 8k du Grot.
const HD = [['grot_hd.glb', 'grot_hd.glb'], ['grot_basecolor_8k.jpg', 'grot_basecolor_8k.jpg']];
let hdCount = 0;
for (const [src, dst] of HD) {
  const f = path.join(here, 'assets', src);
  if (fs.existsSync(f)) { fs.copyFileSync(f, path.join(app, 'assets', 'test', dst)); hdCount++; }
}
const hd = hdCount > 0;
console.log('app/ prêt' + (hd ? ` (${hdCount} fichiers HD inclus)` : ''));
