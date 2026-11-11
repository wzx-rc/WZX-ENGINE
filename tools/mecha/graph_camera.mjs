// Puts the RTS camera graph (Begin Play -> RTS Camera) in the scene open in the editor and frames the start view.
import { wzx } from './wzx.mjs';
await wzx('graph.set', { graph: { nodes: [
  { id: 1, type: 'event.start', x: 0, y: 0 },
  { id: 2, type: 'rts.camera', x: 260, y: 0, in: { Active: true } } ],
  links: [ { from: [1, 0], to: [2, 0] } ] } });
await wzx('camera.set', { target: [2, 0, 2], yaw: 30, pitch: 50, distance: 20 });
await wzx('scene.save', { path: 'game/scenes/mecha_level1.wzx' });
console.log('objects', (await wzx('state')).objects);
