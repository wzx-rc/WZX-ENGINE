// Screenshot of the running editor's viewport (or of the window with --window); prints the PNG path.
// Optional: --play (starts Play first), --wait <seconds>, --cam x,y,z,yaw,pitch,dist
import { wzx } from './wzx.mjs';
const args = process.argv.slice(2);
const opt = (k) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : undefined; };
if (opt('--cam')) {
  const [x, y, z, yaw, pitch, distance] = opt('--cam').split(',').map(Number);
  await wzx('camera.set', { target: [x, y, z], yaw, pitch, distance });
}
if (args.includes('--play')) await wzx('play');
const wait = Number(opt('--wait') ?? 0);
if (wait) await new Promise((r) => setTimeout(r, wait * 1000));
const r = await wzx('screenshot', { window: args.includes('--window') });
console.log(r.path);
