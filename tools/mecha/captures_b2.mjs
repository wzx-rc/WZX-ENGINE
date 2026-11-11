// B2 / mission captures in Play from the RTS camera (window captures, printed as "SHOT label path").
import { wzx } from './wzx.mjs';
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const shot = async (label) => { await sleep(1.05); const r = await wzx('screenshot', { window: true }); console.log(`SHOT ${label} ${r.path}`); };
const state = () => wzx('rts.state');
await wzx('play');
await sleep(1.5);
await shot('11_intro_mission');
await wzx('input.ok');
let st = await state();
const workers = st.units.filter((u) => u.prefab === 'MechaWorker').map((u) => u.index);
const tanks = st.units.filter((u) => u.prefab === 'VitaeTanks').sort((a, b) => Math.hypot(...a.pos) - Math.hypot(...b.pos));
await wzx('input.select', { targets: workers });
await shot('12_selection_anneaux_hud');
// a right click behind the Bastion: they walk around it
await wzx('rts.camera', { target: [0, 0, 0], distance: 24 });
await wzx('input.rightclick', { pos: [0, 0, -5.5] });
await sleep(0.2);
await shot('13a_ordre_contourne_la_base');
await sleep(0.4);
await shot('13b_ordre_contourne_la_base');
// harvest
await wzx('input.rightclick', { target: tanks[0].index });
await sleep(9);
await wzx('rts.camera', { target: [tanks[0].pos[0] - 2.5, 0, tanks[0].pos[1] + 0.5], distance: 22 });
await shot('14_recolte_reservoirs_lumineux');
// enough Vitae for the factory
for (let i = 0; i < 90 && (await state()).vars.vitae < 150; ++i) await sleep(1);
await wzx('rts.camera', { target: [4, 0, -7], distance: 30 });
await wzx('input.hud', { id: 'factory' });
await wzx('input.ghost', { pos: [5, 0, -12] });
await shot('15a_placement_usine_vert');
await wzx('input.ghost', { pos: [1, 0, -1] });
await shot('15b_placement_usine_rouge');
const placed = await wzx('input.place', { pos: [5, 0, -12] });
console.log('placed', placed.placed);
for (let i = 0; i < 90 && (await state()).vars.vitae < 100; ++i) await sleep(1);
await wzx('input.hud', { id: 'warden' });
st = await state();
const factory = st.units.find((u) => u.prefab === 'MechaFactory');
await wzx('input.select', { targets: [factory.index] });
await sleep(3);
await shot('16_production_warden_barre');
await sleep(6);
await wzx('input.select', { targets: (await state()).units.filter((u) => u.prefab === 'MechaWarden').map((u) => u.index) });
await wzx('rts.camera', { target: [5, 0, -9], distance: 22 });
await shot('17_premier_warden_sorti');
await wzx('stop');
