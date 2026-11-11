// Lays out the mecha RTS level in the running editor from the prefabs of game/prefabs/mecha, then saves it as
// game/scenes/mecha_level1.wzx. Usage: node tools/mecha/build_scene.mjs   (the editor running, Scene workspace)
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { wzx } from './wzx.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const pf = (n) => path.join(root, 'game', 'prefabs', 'mecha', n + '.prefab');

// prefab name -> positions on the ground (x, z)
const layout = [
  ['BastionHQ', [[0, 0]]],
  ['VitaeTanks', [[10, 3], [8, -7], [-6, 9]]],
  ['VitaeDepot', [[-5, -4]]],
  ['MechaFactory', [[4, -11]]],
  ['MechaWorker', [[2.6, 3.4], [3.6, 2.2], [-2.4, 3.2]]],
  ['MechaWarden', [[5.5, 6.0]]],
  ['PropCrates', [[-2.8, -2.4], [6.4, -4.2], [-3.4, 5.6]]],
  ['PropLamp', [[2.6, -2.6], [-2.6, 2.4], [7.2, 1.0], [-4.0, 7.0]]],
  ['PropAntenna', [[-3.2, -0.6]]],
  ['PropRocks', [[14, -2], [-11, 2], [3, 13], [-9, -10], [13, 10], [-14, -6]]],
  ['PropCrystal', [[11.8, 5.2], [9.6, -9.4], [-8.2, 10.6], [-12.5, 0.5]]],
];

await wzx('workspace.set', { name: 'scene' });
await wzx('scene.new');
const floor = (await wzx('scene.get')).objects.find((o) => o.part === 'GROUND');
if (floor) await wzx('object.set', { target: floor.index, color: [0.50, 0.42, 0.36], name: 'Plateau' });
for (const [name, spots] of layout) {
  await wzx('prefab.open', { path: pf(name) });
  for (const [x, z] of spots) {
    await wzx('camera.set', { target: [x, 0, z] });
    await wzx('prefab.place');
  }
}
await wzx('workspace.set', { name: 'scene' });
await wzx('sun.set', { azimuth: 135, elevation: 42 });
await wzx('render.set', { mode: 'lit' });
await wzx('camera.set', { target: [1, 0, -1], yaw: 35, pitch: 42, distance: 34 });
await wzx('scene.save', { path: path.join(root, 'game', 'scenes', 'mecha_level1.wzx') });
console.log('scene saved,', (await wzx('state')).objects, 'objects');
