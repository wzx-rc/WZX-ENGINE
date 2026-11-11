// Captures of the HUD and the controls by device (the device simulator of Play): the same moment of the mission on
// PC, iPad and iPhone (workers selected, harvesting), then on iPhone the touch rectangle, the placement of the
// factory with Valider / Annuler, and the folded / opened objectives. Window captures; prints SHOT <label> <path>.
// Usage: node tools/mecha/captures_devices.mjs   (the editor of this project running with game/scenes/mecha_level1.wzx)
import { wzx } from './wzx.mjs';
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
// SHOT <label> <path> <x> <y> <w> <h>: the window capture and the rectangle of the view (its bezel around), to crop
const shot = async (label) => {
  await sleep(1.1);
  const r = await wzx('screenshot', { window: true });
  const [x, y, w, h] = (await wzx('state')).viewport;
  const m = Math.round(h * 0.05);
  console.log(`SHOT ${label} ${r.path} ${Math.round(x - m)} ${Math.round(y - m)} ${Math.round(w + 2 * m)} ${Math.round(h + 2 * m)}`);
};

// the same moment on each device: Play, the intro closed, the workers selected and sent to the nearest station
async function moment(device) {
  await wzx('rts.device', { device });
  await wzx('play');
  await sleep(1.5);
  await wzx('input.ok');
  const st = await wzx('rts.state');
  const workers = st.units.filter((u) => u.prefab === 'MechaWorker');
  const tanks = st.units.filter((u) => u.prefab === 'VitaeTanks').sort((a, b) => Math.hypot(...a.pos) - Math.hypot(...b.pos));
  if (device === 'pc') {
    await wzx('input.select', { targets: workers.map((w) => w.index) });
    await wzx('input.rightclick', { target: tanks[0].index });
  } else {
    await wzx('input.tap', { target: workers[0].index, double: true });
    await wzx('input.tap', { target: tanks[0].index });
  }
  await sleep(4.0);
  return { workers, tanks };
}

for (const device of ['pc', 'ipad', 'iphone']) {
  await moment(device);
  await shot(`moment_${device}`);
  if (device !== 'iphone') await wzx('stop');
}
// iPhone: the touch rectangle (shown while the finger drags) around the workers at the start, the factory placed
await wzx('stop');
await wzx('play');
await sleep(1.5);
await wzx('input.ok');
const st = await wzx('rts.state');
const ws = st.units.filter((u) => u.prefab === 'MechaWorker');
const xs = ws.map((u) => u.pos[0]), zs = ws.map((u) => u.pos[1]);
await wzx('input.touchrect', { from: [Math.min(...xs) - 2, 0, Math.min(...zs) - 2], to: [Math.max(...xs) + 2, 0, Math.max(...zs) + 2], preview: true });
await shot('iphone_rectangle');
await wzx('input.touchrect', { from: [Math.min(...xs) - 2, 0, Math.min(...zs) - 2], to: [Math.max(...xs) + 2, 0, Math.max(...zs) + 2] });
await shot('iphone_rectangle_selected');
// the factory: enough Vitae for the capture, the ghost at the centre of the view, red then green
await wzx('stop');
await wzx('play');
await sleep(1.5);
await wzx('input.ok');
await wzx('rts.var', { name: 'vitae', value: 400 });
await wzx('input.hud', { id: 'factory' });
await sleep(0.3);
await shot('iphone_placement_centre');
await wzx('input.tap', { pos: [0, 0, 0] });   // on the Bastion: red, Valider disabled
await shot('iphone_placement_rouge');
await wzx('input.tap', { pos: [5, 0, -10] });
await shot('iphone_placement_vert');
await wzx('input.command', { name: 'ok' });
await shot('iphone_usine_placee');
await wzx('stop');
await wzx('rts.device', { device: 'pc' });
