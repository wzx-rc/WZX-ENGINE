// Plays the whole mission of game/scenes/mecha_level1.wzx through the command API (the editor running with that
// scene): workers sent to the nearest Vitae station, the factory placed when affordable, 3 Wardens produced, until
// « Mission accomplie ». Asserts the counters and prints PASS / FAIL lines; --shots saves window captures on the way.
// --device iphone | ipad: played on the device simulator of the editor with the touch gestures (input.tap: a tap
// selects, a double tap selects the workers in view, a tap on a station is the order; the factory placed with a
// tap then Valider).
// Usage: node tools/mecha/test_mission.mjs [--shots] [--timeout 300] [--device iphone]
import { wzx } from './wzx.mjs';

const args = process.argv.slice(2);
const shots = args.includes('--shots');
const timeout = args.includes('--timeout') ? Number(args[args.indexOf('--timeout') + 1]) : 300;
const device = args.includes('--device') ? args[args.indexOf('--device') + 1] : 'pc';
const touch = device !== 'pc';
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
let fails = 0;
const check = (name, ok, info = '') => { console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${info ? '  ' + info : ''}`); if (!ok) ++fails; };
const shot = async (label) => { if (!shots) return; const r = await wzx('screenshot', { window: true }); console.log(`SHOT ${label} ${r.path}`); };
const state = () => wzx('rts.state');

await wzx('rts.device', { device });
await wzx('play');
await sleep(1.5);
let st = await state();
if (touch) check(`device ${device}: touch input, its HUD layout`, st.inputMode === 'Touch' && st.layout === (device === 'iphone' ? 'Phone' : 'Tablet'), `${st.device} ${st.inputMode} ${st.layout}`);
check('Play running, intro message', st.playing && st.message === 'Opération Vitae', st.message);
check('start: 100 Vitae', st.vars.vitae === 100);
const workers = st.units.filter((u) => u.prefab === 'MechaWorker');
const tanks = st.units.filter((u) => u.prefab === 'VitaeTanks');
check('start: 3 workers, 3 stations of 150', workers.length === 3 && tanks.length === 3 && tanks.every((t) => t.stock === 150));
await shot('start');
// the nearest station to the Bastion
tanks.sort((a, b) => Math.hypot(...a.pos) - Math.hypot(...b.pos));
const target = tanks[0];
if (touch) {
  await wzx('input.ok');   // the OK of the intro message (a finger)
  let r = await wzx('input.tap', { target: workers[0].index });
  check('tap on a worker: selected', r.selection.length === 1 && r.selection[0] === workers[0].index);
  r = await wzx('input.tap', { target: workers[0].index, double: true });
  check('double tap: the 3 workers selected', r.selection.length === 3, r.selection.join(', '));
  await wzx('input.tap', { target: target.index });   // with workers selected: the order
} else {
  await wzx('input.select', { targets: workers.map((w) => w.index) });
  await wzx('input.rightclick', { target: target.index });
}
await sleep(1);
st = await state();
check('right click on a station: the workers walk', st.units.filter((u) => u.prefab === 'MechaWorker' && u.moving).length === 3);
await shot('walking');

let factoryAsked = false, wardensAsked = 0, t0 = Date.now(), harvestShot = false, returnShot = false;
let lastGathered = 0;
while ((Date.now() - t0) / 1000 < timeout) {
  await sleep(1);
  st = await state();
  const v = st.vars;
  if (!harvestShot && st.units.some((u) => u.prefab === 'MechaWorker' && u.clip === 'Harvest')) { harvestShot = true; await shot('harvesting'); }
  if (!returnShot && st.units.some((u) => u.prefab === 'MechaWorker' && u.cargo > 0 && u.moving)) { returnShot = true; await shot('returning'); }
  if (v.gathered !== lastGathered) { console.log(`t=${((Date.now() - t0) / 1000).toFixed(0)}s vitae ${v.vitae} gathered ${v.gathered} wardens ${v.wardens ?? 0}`); lastGathered = v.gathered; }
  if (!factoryAsked && v.vitae >= 150) {
    await wzx('input.hud', { id: 'factory' });
    await sleep(0.3);
    await shot('placing');
    let r;
    if (touch) {   // the ghost at the centre of the view, a tap puts it there, Valider places it
      await wzx('input.tap', { pos: [5, 0, -12] });
      await sleep(0.3);
      await shot('placing-touch');
      r = await wzx('input.command', { name: 'ok' });
    } else {
      r = await wzx('input.place', { pos: [5, 0, -12] });
    }
    check('the factory placed (150 taken)', r.placed && (await state()).vars.vitae === v.vitae - 150);
    factoryAsked = true;
    await shot('factory');
    continue;
  }
  if (factoryAsked && wardensAsked < 3 && v.vitae >= 100) {
    await wzx('input.hud', { id: 'warden' });
    ++wardensAsked;
    console.log('warden ordered', wardensAsked);
    if (wardensAsked === 1) { await sleep(2); await shot('production'); }
  }
  if (st.message === 'Mission accomplie') break;
}
st = await state();
const tanksNow = st.units.filter((u) => u.prefab === 'VitaeTanks');
check('gathered at least 300', st.vars.gathered >= 300, `${st.vars.gathered}`);
check('a station was drained', tanksNow.some((t) => t.stock < 150), tanksNow.map((t) => t.stock).join(', '));
check('3 Wardens produced', st.vars.wardens === 3 && st.units.filter((u) => u.prefab === 'MechaWarden').length === 3);
check('objectives done', st.objectives.length === 3 && st.objectives.every((o) => o.done));
check('« Mission accomplie »', st.message === 'Mission accomplie', `after ${((Date.now() - t0) / 1000).toFixed(0)} s`);
await shot('victory');
await wzx('stop');
await wzx('rts.device', { device: 'pc' });
console.log(fails ? `FAILED ${fails}` : 'ALL PASSED');
process.exit(fails ? 1 : 0);
