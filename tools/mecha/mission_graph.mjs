// The mission of the mecha RTS level, built only with graph nodes, put in the scene open in the editor and saved with
// it (game/scenes/mecha_level1.wzx keeps its graph; game/main.graph.json is not touched).
//
// Start: the Bastion, 3 workers, 100 Vitae, 3 Vitae tank stations of 150 each. Right click a station with workers
// selected: each walks there, harvests 2 s (Harvest clip), carries 10 (its back tank glows), walks to the nearest
// drop-off (Bastion or depot), +10 Vitae, and goes back until the station is empty (its glow fades), then to the
// next one. HUD: « Ouvrier (50) » at the Bastion, « Usine (150) » places the factory, then « Warden (100) » there.
// Objectives: gather 300 Vitae, build a factory, produce 3 Wardens -> « Mission accomplie ».
//
// Per-unit state machine: « On Timer » is the dispatcher (its Object pin is the unit): go -> walk to its station,
// harvest -> take 10, back -> walk to the drop-off. Usage: node tools/mecha/mission_graph.mjs [--no-save]
import { wzx } from './wzx.mjs';

const nodes = [];
const links = [];
let next = 1;
const N = (type, inp = {}, x = 0, y = 0) => { const id = next++; nodes.push({ id, type, x, y, in: inp }); return id; };
const L = (a, out, b, inp) => links.push({ from: [a, out], to: [b, inp] });
const chain = (...ids) => { for (let i = 0; i + 1 < ids.length; ++i) L(ids[i], 0, ids[i + 1], 0); };
// a few data helpers
const getVar = (name, x, y) => N('data.getVar', { Name: name }, x, y);
const objVar = (obj, objPin, name, x, y) => { const n = N('unit.getVar', { Name: name }, x, y); L(obj, objPin, n, 'Object'); return n; };
const objLink = (obj, objPin, name, x, y) => { const n = N('unit.getLink', { Name: name }, x, y); L(obj, objPin, n, 'Object'); return n; };
const posOf = (obj, objPin, x, y) => { const n = N('data.getPosition', {}, x, y); L(obj, objPin, n, 'Object'); return n; };
const strIs = (src, srcPin, text, x, y) => { const n = N('math.strEqual', { B: text }, x, y); L(src, srcPin, n, 'A'); return n; };
const atLeast = (src, srcPin, value, x, y) => { const lt = N('math.less', { B: value }, x, y); L(src, srcPin, lt, 'A'); const no = N('math.not', {}, x + 160, y); L(lt, 'Result', no, 'A'); return no; };
const branch = (cond, condPin, x, y) => { const b = N('flow.branch', {}, x, y); L(cond, condPin, b, 'Condition'); return b; };

// ------------------------------------------------------------------ Begin Play
let y = 0;
const start = N('event.start', {}, 0, y);
const cam = N('rts.camera', { Active: true }, 220, y);
const v1 = N('action.setVar', { Name: 'vitae', Value: 100 }, 440, y);
const v2 = N('action.setVar', { Name: 'gathered', Value: 0 }, 660, y);
const v3 = N('action.setVar', { Name: 'wardens', Value: 0 }, 880, y);
const v4 = N('action.setVar', { Name: 'factoryBuilt', Value: 0 }, 1100, y);
const c1 = N('hud.counter', { Id: 'vitae', Label: 'Vitae', Variable: 'vitae', Color: [0.3, 1.0, 0.45] }, 1320, y);
const c2 = N('hud.counter', { Id: 'gathered', Label: 'Récolté', Variable: 'gathered', Color: [0.55, 0.85, 1.0] }, 1540, y);
const o1 = N('hud.objective', { Id: 'gather', Text: 'Récolter 300 Vitae', Done: false }, 1760, y);
const o2 = N('hud.objective', { Id: 'factory', Text: 'Construire une Usine', Done: false }, 1980, y);
const o3 = N('hud.objective', { Id: 'wardens', Text: 'Produire 3 Wardens', Done: false }, 2200, y);
const b1 = N('rts.hudButton', { Id: 'worker', Label: 'Ouvrier (50)' }, 2420, y);
const b2 = N('rts.hudButton', { Id: 'factory', Label: 'Usine (150)' }, 2640, y);
chain(start, cam, v1, v2, v3, v4, c1, c2, o1, o2, o3, b1, b2);
// the tank stations hold 150 each, the workers carry 10, the Bastion and the depots take the Vitae
y = 200;
const fT = N('flow.forEachPart', { Type: 'VitaeTanks' }, 0, y);
const sT1 = N('unit.setVar', { Name: 'stock', Value: 150 }, 220, y);
const sT2 = N('unit.setVar', { Name: 'stockMax', Value: 150 }, 440, y);
L(fT, 0, sT1, 0); L(fT, 'Object', sT1, 'Object'); L(sT1, 0, sT2, 0); L(fT, 'Object', sT2, 'Object');
const fW = N('flow.forEachPart', { Type: 'MechaWorker' }, 660, y);
const sW1 = N('unit.setVar', { Name: 'cargo', Value: 0 }, 880, y);
const sW2 = N('unit.setVar', { Name: 'cargoMax', Value: 10 }, 1100, y);
L(fW, 0, sW1, 0); L(fW, 'Object', sW1, 'Object'); L(sW1, 0, sW2, 0); L(fW, 'Object', sW2, 'Object');
const fB = N('flow.forEachPart', { Type: 'BastionHQ' }, 1320, y);
const sB = N('unit.setVar', { Name: 'dropoff', Value: 1 }, 1540, y);
L(fB, 0, sB, 0); L(fB, 'Object', sB, 'Object');
const fD = N('flow.forEachPart', { Type: 'VitaeDepot' }, 1760, y);
const sD = N('unit.setVar', { Name: 'dropoff', Value: 1 }, 1980, y);
L(fD, 0, sD, 0); L(fD, 'Object', sD, 'Object');
const intro = N('hud.message', { Title: 'Opération Vitae',
  Text: 'Sélectionnez vos ouvriers et faites un clic droit sur une station de Vitae pour la récolter. ' +
        'Construisez une Usine, puis produisez 3 Wardens.' }, 2200, y);
L(b2, 0, fT, 0); L(fT, 'Completed', fW, 0); L(fW, 'Completed', fB, 0); L(fB, 'Completed', fD, 0); L(fD, 'Completed', intro, 0);

// ------------------------------------------------------------------ orders
// right click on a station: the selected workers are assigned to it
y = 450;
const rco = N('event.rightClickObject', {}, 0, y);
const isTank = strIs(N('unit.type', {}, 0, y + 120), 'Type', 'VitaeTanks', 220, y + 120);
L(rco, 'Object', isTank - 1, 'Object');
const bTank = branch(isTank, 'Result', 220, y);
const eachSel = N('unit.forEachSelected', {}, 440, y);
const selType = N('unit.type', {}, 440, y + 120);
L(eachSel, 'Object', selType, 'Object');
const isWorker = strIs(selType, 'Type', 'MechaWorker', 660, y + 120);
const bWorker = branch(isWorker, 'Result', 660, y);
const link = N('unit.setLink', { Name: 'station' }, 880, y);
const st1 = N('unit.setVar', { Name: 'state', Value: 1 }, 1100, y);
const tGo = N('unit.startTimer', { Name: 'go', Seconds: 0 }, 1320, y);
chain(rco, bTank); L(bTank, 'True', eachSel, 0); L(eachSel, 'Loop', bWorker, 0); L(bWorker, 'True', link, 0); chain(link, st1, tGo);
L(eachSel, 'Object', link, 'Object'); L(rco, 'Object', link, 'Target');
L(eachSel, 'Object', st1, 'Object'); L(eachSel, 'Object', tGo, 'Object');
// right click on the ground: the selection walks there (a worker leaves its job)
y = 700;
const rcg = N('event.rightClickGround', {}, 0, y);
const eachG = N('unit.forEachSelected', {}, 220, y);
const st0 = N('unit.setVar', { Name: 'state', Value: 0 }, 440, y);
const moveAny = N('unit.move', { Speed: 3.4 }, 660, y);
chain(rcg, eachG); L(eachG, 'Loop', st0, 0); chain(st0, moveAny);
L(eachG, 'Object', st0, 'Object'); L(eachG, 'Object', moveAny, 'Object'); L(rcg, 'Point', moveAny, 'Target');

// ------------------------------------------------------------------ the workers' jobs (On Timer = dispatcher)
y = 950;
const tim = N('event.timer', {}, 0, y);
const isGo = strIs(tim, 'Name', 'go', 220, y + 120);
const bGo = branch(isGo, 'Result', 220, y);
const station = objLink(tim, 'Object', 'station', 440, y + 120);
const moveGo = N('unit.move', { Speed: 3.4 }, 660, y);
L(tim, 0, bGo, 0); L(bGo, 'True', moveGo, 0);
L(tim, 'Object', moveGo, 'Object'); L(posOf(station, 'Target', 660, y + 120), 'Position', moveGo, 'Target');
// at the station: Harvest for 2 s
const harvestClip = N('prefab.playClip', { Clip: 'Harvest', Loop: true, Speed: 1 }, 880, y);
const tHarvest = N('unit.startTimer', { Name: 'harvest', Seconds: 2.0 }, 1100, y);
L(moveGo, 'Arrived', harvestClip, 0); chain(harvestClip, tHarvest);
L(moveGo, 'Unit', harvestClip, 'Object'); L(moveGo, 'Unit', tHarvest, 'Object');

// harvest: take 10 from the station (if it has them), the back tank glows, then back to the drop-off
y = 1250;
const isHarvest = strIs(tim, 'Name', 'harvest', 220, y + 120);
const bHarvest = branch(isHarvest, 'Result', 220, y);
L(bGo, 'False', bHarvest, 0);
const working = N('math.equal', { B: 1 }, 440, y + 120);
L(objVar(tim, 'Object', 'state', 440, y + 220), 'Value', working, 'A');
const bWorking = branch(working, 'Result', 440, y);
L(bHarvest, 'True', bWorking, 0);
const station2 = objLink(tim, 'Object', 'station', 660, y + 220);
const hasStock = atLeast(objVar(station2, 'Target', 'stock', 660, y + 320), 'Value', 10, 660, y + 120);
const bStock = branch(hasStock, 'Result', 880, y);
L(bWorking, 'True', bStock, 0);
const take = N('unit.addVar', { Name: 'stock', Amount: -10 }, 1100, y);
const load = N('unit.setVar', { Name: 'cargo', Value: 10 }, 1320, y);
const glowUp = N('prefab.setPartGlow', { Part: 'Back Tank Core', Glow: 4 }, 1540, y);
L(bStock, 'True', take, 0); chain(take, load, glowUp);
L(station2, 'Target', take, 'Object'); L(tim, 'Object', load, 'Object'); L(tim, 'Object', glowUp, 'Object');
// the station emptied: its liquid fades
const emptied = N('math.less', { B: 10 }, 1760, y + 120);
L(objVar(station2, 'Target', 'stock', 1760, y + 220), 'Value', emptied, 'A');
const bEmptied = branch(emptied, 'Result', 1760, y);
const fade1 = N('prefab.setPartGlow', { Part: 'Liquid', Glow: 0.1 }, 1980, y);
const fade2 = N('prefab.setPartGlow', { Part: 'Pipe Glow', Glow: 0.1 }, 2200, y);
L(glowUp, 0, bEmptied, 0); L(bEmptied, 'True', fade1, 0); chain(fade1, fade2);
L(station2, 'Target', fade1, 'Object'); L(station2, 'Target', fade2, 'Object');
const tBack1 = N('unit.startTimer', { Name: 'back', Seconds: 0 }, 2420, y);
const tBack2 = N('unit.startTimer', { Name: 'back', Seconds: 0 }, 2420, y + 120);
L(fade2, 0, tBack1, 0); L(bEmptied, 'False', tBack2, 0);
L(tim, 'Object', tBack1, 'Object'); L(tim, 'Object', tBack2, 'Object');
// nothing left at its station: the nearest station that still has Vitae, else it waits
const other = N('unit.findNearest', { Type: 'VitaeTanks', 'Var Name': 'stock', Min: 10 }, 1100, y + 420);
L(posOf(tim, 'Object', 880, y + 520), 'Position', other, 'From');
const otherOk = N('data.isValid', {}, 1320, y + 520);
L(other, 'Object', otherOk, 'Object');
const bOther = branch(otherOk, 'Valid', 1320, y + 420);
const relink = N('unit.setLink', { Name: 'station' }, 1540, y + 420);
const tGo2 = N('unit.startTimer', { Name: 'go', Seconds: 0 }, 1760, y + 420);
const idle = N('prefab.playClip', { Clip: 'Idle', Loop: true, Speed: 1 }, 1540, y + 540);
const noMore = N('action.print', { Text: 'Toutes les stations de Vitae sont vides' }, 1760, y + 540);
L(bStock, 'False', bOther, 0); L(bOther, 'True', relink, 0); chain(relink, tGo2); L(bOther, 'False', idle, 0); chain(idle, noMore);
L(tim, 'Object', relink, 'Object'); L(other, 'Object', relink, 'Target'); L(tim, 'Object', tGo2, 'Object'); L(tim, 'Object', idle, 'Object');

// back: to the nearest drop-off (Bastion or depot)
y = 1950;
const isBack = strIs(tim, 'Name', 'back', 220, y + 120);
const bBack = branch(isBack, 'Result', 220, y);
L(bHarvest, 'False', bBack, 0);
const drop = N('unit.findNearest', { Type: 'Building', 'Var Name': 'dropoff', Min: 1 }, 440, y + 120);
L(posOf(tim, 'Object', 440, y + 220), 'Position', drop, 'From');
const moveBack = N('unit.move', { Speed: 3.4 }, 660, y);
L(bBack, 'True', moveBack, 0); L(tim, 'Object', moveBack, 'Object'); L(posOf(drop, 'Object', 660, y + 120), 'Position', moveBack, 'Target');
// delivered: +cargo, the tank goes dim, the goals are checked, and back to work
const cargo = objVar(moveBack, 'Unit', 'cargo', 880, y + 120);
const addV = N('action.addVar', { Name: 'vitae' }, 880, y);
const addG = N('action.addVar', { Name: 'gathered' }, 1100, y);
const unload = N('unit.setVar', { Name: 'cargo', Value: 0 }, 1320, y);
const glowDown = N('prefab.setPartGlow', { Part: 'Back Tank Core', Glow: 1 }, 1540, y);
const goals1 = N('flow.fireCustom', { Name: 'goals' }, 1760, y);
L(moveBack, 'Arrived', addV, 0); chain(addV, addG, unload, glowDown, goals1);
L(cargo, 'Value', addV, 'Amount'); L(cargo, 'Value', addG, 'Amount');
L(moveBack, 'Unit', unload, 'Object'); L(moveBack, 'Unit', glowDown, 'Object');
const still = N('math.equal', { B: 1 }, 1980, y + 120);
L(objVar(moveBack, 'Unit', 'state', 1980, y + 220), 'Value', still, 'A');
const bStill = branch(still, 'Result', 1980, y);
const tGo3 = N('unit.startTimer', { Name: 'go', Seconds: 0.2 }, 2200, y);
L(goals1, 0, bStill, 0); L(bStill, 'True', tGo3, 0); L(moveBack, 'Unit', tGo3, 'Object');

// ------------------------------------------------------------------ HUD buttons: worker, factory, warden
y = 2300;
const hud = N('event.hudButton', {}, 0, y);
// Ouvrier (50) at the Bastion
const isW = strIs(hud, 'Id', 'worker', 220, y + 120);
const bW = branch(isW, 'Result', 220, y);
const can50 = atLeast(getVar('vitae', 440, y + 220), 'Value', 50, 440, y + 120);
const bCan50 = branch(can50, 'Result', 660, y);
const pay50 = N('action.addVar', { Name: 'vitae', Amount: -50 }, 880, y);
const prodW = N('unit.produce', { Prefab: 'MechaWorker', Seconds: 5 }, 1100, y);
L(N('data.findObject', { Name: 'Bastion HQ' }, 1100, y + 160), 'Object', prodW, 'Building');
const nW1 = N('unit.setVar', { Name: 'cargo', Value: 0 }, 1320, y);
const nW2 = N('unit.setVar', { Name: 'cargoMax', Value: 10 }, 1540, y);
const poor50 = N('action.print', { Text: 'Pas assez de Vitae : un Ouvrier coûte 50' }, 880, y + 120);
L(hud, 0, bW, 0); L(bW, 'True', bCan50, 0); L(bCan50, 'True', pay50, 0); chain(pay50, prodW); L(bCan50, 'False', poor50, 0);
L(prodW, 'Spawned', nW1, 0); chain(nW1, nW2); L(prodW, 'Unit', nW1, 'Object'); L(prodW, 'Unit', nW2, 'Object');
// Usine (150): placed by the player, once
y = 2600;
const isF = strIs(hud, 'Id', 'factory', 220, y + 120);
const bF = branch(isF, 'Result', 220, y);
L(bW, 'False', bF, 0);
const notBuilt = N('math.equal', { B: 0 }, 440, y + 120);
L(getVar('factoryBuilt', 440, y + 220), 'Value', notBuilt, 'A');
const bNotBuilt = branch(notBuilt, 'Result', 440, y);
const place = N('unit.placeBuilding', { Prefab: 'MechaFactory', 'Cost Var': 'vitae', Cost: 150 }, 660, y);
const built = N('action.setVar', { Name: 'factoryBuilt', Value: 1 }, 880, y);
const oF = N('hud.objective', { Id: 'factory', Text: 'Construire une Usine', Done: true }, 1100, y);
const bWarden = N('rts.hudButton', { Id: 'warden', Label: 'Warden (100)' }, 1320, y);
const goals2 = N('flow.fireCustom', { Name: 'goals' }, 1540, y);
L(bF, 'True', bNotBuilt, 0); L(bNotBuilt, 'True', place, 0); L(place, 'Placed', built, 0); chain(built, oF, bWarden, goals2);
// Warden (100) at the factory
y = 2850;
const isWa = strIs(hud, 'Id', 'warden', 220, y + 120);
const bWa = branch(isWa, 'Result', 220, y);
L(bF, 'False', bWa, 0);
const can100 = atLeast(getVar('vitae', 440, y + 220), 'Value', 100, 440, y + 120);
const bCan100 = branch(can100, 'Result', 660, y);
const pay100 = N('action.addVar', { Name: 'vitae', Amount: -100 }, 880, y);
const prodWa = N('unit.produce', { Prefab: 'MechaWarden', Seconds: 8 }, 1100, y);
L(N('unit.findNearest', { Type: 'MechaFactory' }, 1100, y + 160), 'Object', prodWa, 'Building');
const addWa = N('action.addVar', { Name: 'wardens', Amount: 1 }, 1320, y);
const goals3 = N('flow.fireCustom', { Name: 'goals' }, 1540, y);
const poor100 = N('action.print', { Text: 'Pas assez de Vitae : un Warden coûte 100' }, 880, y + 120);
L(bWa, 'True', bCan100, 0); L(bCan100, 'True', pay100, 0); chain(pay100, prodWa); L(bCan100, 'False', poor100, 0);
L(prodWa, 'Spawned', addWa, 0); chain(addWa, goals3);

// ------------------------------------------------------------------ objectives
y = 3150;
const goals = N('event.custom', { Name: 'goals' }, 0, y);
const gDone = atLeast(getVar('gathered', 220, y + 220), 'Value', 300, 220, y + 120);
const oG = N('hud.objective', { Id: 'gather', Text: 'Récolter 300 Vitae' }, 440, y);
L(gDone, 'Result', oG, 'Done');
const wDone = atLeast(getVar('wardens', 660, y + 220), 'Value', 3, 660, y + 120);
const oW = N('hud.objective', { Id: 'wardens', Text: 'Produire 3 Wardens' }, 880, y);
L(wDone, 'Result', oW, 'Done');
const fDone = N('math.equal', { B: 1 }, 1100, y + 220);
L(getVar('factoryBuilt', 1100, y + 320), 'Value', fDone, 'A');
const all1 = N('math.and', {}, 1320, y + 120); L(gDone, 'Result', all1, 'A'); L(wDone, 'Result', all1, 'B');
const all2 = N('math.and', {}, 1320, y + 220); L(all1, 'Result', all2, 'A'); L(fDone, 'Result', all2, 'B');
const bAll = branch(all2, 'Result', 1100, y);
const once = N('flow.doOnce', {}, 1320, y);
const win = N('hud.message', { Title: 'Mission accomplie', Text: 'Opération Vitae réussie : la colonie est opérationnelle et protégée par vos Wardens.' }, 1540, y);
chain(goals, oG, oW, bAll); L(bAll, 'True', once, 0); chain(once, win);

const graph = { format: 'wzx-graph', version: 1, name: 'mecha_level1', nodes, links };
await wzx('graph.set', { graph });
const diag = await wzx('graph.compile');
console.log('nodes', nodes.length, 'links', links.length, 'compile', JSON.stringify(diag).slice(0, 400));
if (!process.argv.includes('--no-save')) {
  await wzx('scene.save', { path: 'game/scenes/mecha_level1.wzx' });
  console.log('saved in game/scenes/mecha_level1.wzx');
}
