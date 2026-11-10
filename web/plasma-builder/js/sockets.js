// Sockets de l'ork riggé : les points où l'on accroche les briques (tête, mains, dos, épaules, torse, ceinture,
// socle). Chacun suit un joint du rig ; il est calculé au chargement à partir des joints et de la surface du low
// (rayons lancés vers le corps), dans l'espace de l'ork (cm, gauche = +x, avant = +z), orienté comme la figurine.
// Une brique modélisée en place sur l'ork dans Maya retombe donc exactement là où elle a été modélisée.
import * as THREE from 'three';
import { CHAINS, FINGERS } from './riglab/model.js';
import { PRESETS, buildPreset } from './riglab/poses.js';

export const SOCKET_TYPES = {
  head: 'Tête', grip: 'Main', back: 'Dos', shoulder: 'Épaule', chest: 'Torse', belt: 'Ceinture', base: 'Socle', any: 'Libre',
};

const Q = n => 'QuickRigCharacter_' + n;

// Rayon vers le joint (espace ork) : la surface du low la plus proche du joint de ce côté (un bras qui pend
// devant la hanche ne compte pas), sinon le joint lui-même.
function surface(model, at, dir) {
  const ork = model.ork, low = model.meshes.BODY_low;
  const from = ork.localToWorld(at.clone().addScaledVector(dir, -200)), to = ork.localToWorld(at.clone());
  const ray = new THREE.Raycaster(from, to.clone().sub(from).normalize());
  const reach = from.distanceTo(to);
  const hits = ray.intersectObject(low, false).filter(h => h.distance < reach);
  return hits.length ? ork.worldToLocal(hits[hits.length - 1].point.clone()) : at.clone();
}

// Repère des mains (grip) : le manche passe dans le poing. Y = axe du poing (de l'auriculaire vers l'index), Z = l'avant
// de l'ork, X = Y × Z, réglés dans la pose de garde (celle du Builder) : une arme modélisée debout dans Maya (manche
// selon Y, à l'origine, PLUG sans rotation au milieu de la prise) apparaît en garde comme dans Maya, juste inclinée
// dans l'axe du poing, puis suit la main dans les autres poses.
function gripRotations(model) {
  const ork = model.ork, out = {};
  const sample = () => {
    ork.updateMatrixWorld(true);
    const inv = ork.matrixWorld.clone().invert(), res = {};
    for (const side of ['L', 'R']) {
      const at = n => new THREE.Vector3().setFromMatrixPosition(inv.clone().multiply(model.bone(n).matrixWorld));
      const hand = new THREE.Matrix4().extractRotation(inv.clone().multiply(model.bone(CHAINS[side === 'L' ? 'handL' : 'handR'][2]).matrixWorld));
      res[side] = { hand, fist: at(FINGERS[side][1][0]).sub(at(FINGERS[side][4][0])).normalize() };
    }
    return res;
  };
  model.setPose(null);
  const rest = sample();
  const garde = PRESETS.find(p => p.id === 'garde');
  if (garde) buildPreset(model, garde);
  const pose = sample();
  model.setPose(null);
  model.root.updateMatrixWorld(true);
  for (const side of ['L', 'R']) {
    const y = pose[side].fist;
    const z = new THREE.Vector3(0, 0, 1).addScaledVector(y, -y.z).normalize();
    const x = new THREE.Vector3().crossVectors(y, z);
    const inGarde = new THREE.Matrix4().makeBasis(x, y, z);
    // la main porte le socket : repos = main au repos · (main en garde)⁻¹ · repère voulu en garde
    out[side] = rest[side].hand.clone().multiply(pose[side].hand.clone().invert()).multiply(inGarde);
  }
  return out;
}

// À appeler avec l'ork au repos. Rend [{ id, label, type, bone, rest (Matrix4, espace ork), local (relatif au joint) }].
export function orkSockets(model) {
  const grip = gripRotations(model);
  model.setPose(null);
  model.root.updateMatrixWorld(true);
  const pos = name => model.bone(name).userData.restW.clone();
  const handPos = side => {
    const hand = pos(CHAINS[side === 'L' ? 'handL' : 'handR'][2]);
    return hand.lerp(pos(FINGERS[side][2][0]), 0.75);
  };
  const spine = pos(Q('Spine2')), hips = pos(Q('Hips'));
  const shoulder = name => { const p = pos(name); return surface(model, p, new THREE.Vector3(0, -1, 0)); };
  const defs = [
    ['head', 'Tête', 'head', Q('Head'), pos(Q('Head'))],
    ['grip_R', 'Main droite', 'grip', CHAINS.handR[2], handPos('R')],
    ['grip_L', 'Main gauche', 'grip', CHAINS.handL[2], handPos('L')],
    ['back', 'Dos', 'back', Q('Spine2'), surface(model, spine, new THREE.Vector3(0, 0, 1))],
    ['chest', 'Torse', 'chest', Q('Spine2'), surface(model, spine, new THREE.Vector3(0, 0, -1))],
    ['shoulder_L', 'Épaule gauche', 'shoulder', CHAINS.handL[0], shoulder(CHAINS.handL[0])],
    ['shoulder_R', 'Épaule droite', 'shoulder', CHAINS.handR[0], shoulder(CHAINS.handR[0])],
    ['belt_L', 'Ceinture gauche', 'belt', Q('Hips'), surface(model, hips, new THREE.Vector3(-1, 0, 0))],
    ['belt_R', 'Ceinture droite', 'belt', Q('Hips'), surface(model, hips, new THREE.Vector3(1, 0, 0))],
    ['base', 'Socle', 'base', null, new THREE.Vector3(hips.x, model.info.floor ?? 0, hips.z)],
  ];
  const orkInv = model.ork.matrixWorld.clone().invert();
  return defs.map(([id, label, type, boneName, p]) => {
    const bone = boneName ? model.bone(boneName) : null;
    const rest = id.startsWith('grip_') ? grip[id.slice(-1)].clone().setPosition(p) : new THREE.Matrix4().makeTranslation(p.x, p.y, p.z);
    // repère du joint au repos, dans l'espace ork
    const boneRest = bone ? orkInv.clone().multiply(bone.matrixWorld) : new THREE.Matrix4();
    const local = boneRest.invert().multiply(rest);
    return { id, label, type, bone, rest, local };
  });
}

// Noms des locators Maya : SOCKET_<type>[_<nom>] et PLUG_<type> (préfixe en majuscules ou non : Socket_grip passe).
export function parseLocatorName(name) {
  const m = /^(SOCKET|PLUG)_([A-Za-z]+)(?:_(.+))?$/i.exec(name.replace(/^.*[|:]/, ''));
  if (!m) return null;
  const type = m[2].toLowerCase();
  return { kind: m[1].toUpperCase() === 'PLUG' ? 'plug' : 'socket', type: SOCKET_TYPES[type] ? type : 'any', name: m[3] || type };
}
