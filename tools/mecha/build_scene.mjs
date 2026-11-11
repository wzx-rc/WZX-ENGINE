// Lays out the mecha RTS level in the running editor with live prefab instances (game/prefabs/mecha), then saves
// it as game/scenes/mecha_level1.wzx. Usage: node tools/mecha/build_scene.mjs   (the editor of this project running)
import { wzx } from './wzx.mjs';

// prefab name -> [x, z, yaw degrees]
const layout = [
  ['BastionHQ', [[0, 0, 0]]],
  ['VitaeTanks', [[10, 3, 0], [8, -7, 15], [-6, 9, -10]]],
  ['VitaeDepot', [[-5, -4, 30]]],

  ['MechaWorker', [[-1.4, 3.6, 0], [0, 4.0, 0], [1.4, 3.6, 0]]],

  ['PropCrates', [[-2.8, -2.4, 10], [6.4, -4.2, 70], [-3.4, 5.6, -20]]],
  ['PropLamp', [[2.6, -2.6, 0], [-2.6, 2.4, 180], [7.2, 1.0, 90], [-4.0, 7.0, 0]]],
  ['PropAntenna', [[-3.2, -0.6, 0]]],
  ['PropRocks', [[14, -2, 0], [-11, 2, 80], [3, 13, 140], [-9, -10, 200], [13, 10, 260], [-14, -6, 30]]],
  ['PropCrystal', [[11.8, 5.2, 0], [9.6, -9.4, 50], [-8.2, 10.6, 120], [-12.5, 0.5, 200]]],
];

await wzx('workspace.set', { name: 'scene' });
await wzx('scene.new');
const floor = (await wzx('scene.get')).objects.find((o) => o.part === 'GROUND');
if (floor) await wzx('object.set', { target: floor.index, visible: false });
const ground = await wzx('object.add', { primitive: 'plane', name: 'Ground', pos: [0, 0, 0], scale: [90, 1, 90], color: [0.40, 0.355, 0.315] });
await wzx('object.set', { target: ground.index, selectable: false });
let count = 0;
for (const [name, spots] of layout)
  for (const [x, z, yaw] of spots) {
    await wzx('prefab.instance', { prefab: name, pos: [x, 0, z], yaw });
    ++count;
  }
await wzx('sun.set', { azimuth: 135, elevation: 42 });
await wzx('render.set', { mode: 'lit' });
await wzx('camera.set', { target: [1.5, 0, 0.5], yaw: 25, pitch: 55, distance: 36 });
await wzx('scene.save', { path: 'game/scenes/mecha_level1.wzx' });
console.log('scene saved:', count, 'instances,', (await wzx('state')).objects, 'objects');
