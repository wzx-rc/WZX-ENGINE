// Captures of the access to the prefab of an instance: the right-click menu in the view, the Prefab line of the
// Inspector, the Prefab tab opened on MechaWorker, then the instances of the scene updated after a modification saved
// (the « Team Blue » of the worker turned orange). The prefab file is put back by git afterwards (see the end).
// Usage: node tools/mecha/captures_prefab.mjs   (an editor of this project with game/scenes/mecha_level1.wzx; WZX_PORT
// to choose it when two are open)
import { wzx } from './wzx.mjs';
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const shot = async (label) => {
  await sleep(1.1);
  const r = await wzx('screenshot', { window: true });
  console.log(`SHOT ${label} ${r.path}`);
};

await wzx('workspace.set', { name: 'scene' });
const sc = await wzx('scene.get');
const worker = sc.objects.find((o) => o.name === 'Mecha Worker1') ?? sc.objects.find((o) => /Mecha Worker/.test(o.name));
await wzx('camera.set', { target: [0.5, 0, 3.4], yaw: 25, pitch: 40, distance: 11 });
await wzx('object.select', { target: worker.index });
await sleep(0.5);
await shot('inspector_prefab');
await wzx('ui.prefabMenu', { target: worker.index });
await shot('menu_clic_droit');
await wzx('ui.prefabMenu', { close: true });
await sleep(0.3);
console.log(JSON.stringify(await wzx('prefab.openInstance', { target: worker.index })));
await sleep(1.5);
await shot('studio_mechaworker');
await wzx('prefab.recolor', { material: 'Team Blue', color: [1.0, 0.45, 0.08] });
await sleep(0.8);
await shot('studio_modifie');
await wzx('prefab.save');
await wzx('workspace.set', { name: 'scene' });
await sleep(2.5);   // the library looks at the files every second
await wzx('object.select', { target: worker.index });
await shot('scene_instances_a_jour');
// back to the original colour (the file is then restored by git: git checkout game/prefabs/mecha/MechaWorker.prefab)
await wzx('prefab.openInstance', { target: worker.index });
await wzx('prefab.recolor', { material: 'Team Blue', color: [0.1, 0.52, 1.0] });
await wzx('prefab.save');
await wzx('workspace.set', { name: 'scene' });
await sleep(2.5);
await shot('scene_couleur_rendue');
