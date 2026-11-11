// B2 check in the running editor: three workers selected, a right click behind the Bastion: they walk around it.
import { wzx } from './wzx.mjs';
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
await wzx('graph.set', { graph: { nodes: [
  { id: 1, type: 'event.start', x: 0, y: 0 },
  { id: 2, type: 'rts.camera', x: 250, y: 0, in: { Active: true } },
  { id: 3, type: 'action.setVar', x: 500, y: 0, in: { Name: 'vitae', Value: 100 } },
  { id: 4, type: 'hud.counter', x: 750, y: 0, in: { Id: 'vitae', Label: 'Vitae', Variable: 'vitae', Color: [0.3, 1, 0.45] } },
  { id: 5, type: 'hud.objective', x: 1000, y: 0, in: { Id: 'move', Text: 'Move the workers behind the Bastion', Done: false } },
  { id: 10, type: 'event.rightClickGround', x: 0, y: 200 },
  { id: 11, type: 'unit.forEachSelected', x: 250, y: 200 },
  { id: 12, type: 'unit.move', x: 500, y: 200, in: { Speed: 3 } },
  { id: 13, type: 'action.print', x: 750, y: 260, in: { Text: 'arrived' } } ],
  links: [ { from: [1, 0], to: [2, 0] }, { from: [2, 0], to: [3, 0] }, { from: [3, 0], to: [4, 0] }, { from: [4, 0], to: [5, 0] },
    { from: [10, 0], to: [11, 0] }, { from: [11, 0], to: [12, 0] }, { from: [11, 1], to: [12, 1] }, { from: [10, 1], to: [12, 2] },
    { from: [12, 1], to: [13, 0] } ] } });
await wzx('camera.set', { target: [0, 0, 0], yaw: 30, pitch: 55, distance: 26 });
await wzx('play');
await sleep(1);
const st0 = await wzx('rts.state');
const workers = st0.units.filter((u) => u.prefab === 'MechaWorker').map((u) => u.index);
console.log('workers', workers, 'vars', JSON.stringify(st0.vars));
console.log('select', JSON.stringify(await wzx('input.select', { targets: workers })));
await wzx('input.rightclick', { pos: [0, 0, -4.5] });
await sleep(1.2);
console.log('mid-move shot', (await wzx('screenshot')).path);
await sleep(4);
const st = await wzx('rts.state');
for (const u of st.units.filter((u) => workers.includes(u.index))) console.log(u.index, u.pos.map((v) => v.toFixed(2)).join(','), u.clip, u.moving);
console.log('end shot', (await wzx('screenshot', { window: true })).path);
console.log('log', JSON.stringify((await wzx('log')).log?.slice(-6)));
