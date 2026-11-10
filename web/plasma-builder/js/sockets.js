// Sockets de l'ork riggé : les points où l'on accroche les briques (tête, mains, dos, épaules, torse, ceinture,
// socle). Chacun suit un joint du rig ; il est calculé au chargement à partir des joints et de la surface du low
// (rayons lancés vers le corps), dans l'espace de l'ork (cm, gauche = +x, avant = +z), orienté comme la figurine.
// Une brique modélisée en place sur l'ork dans Maya retombe donc exactement là où elle a été modélisée.
import * as THREE from 'three';
import { CHAINS, FINGERS } from './riglab/model.js';

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

// À appeler avec l'ork au repos. Rend [{ id, label, type, bone, rest (Matrix4, espace ork), local (relatif au joint) }].
export function orkSockets(model) {
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
    const rest = new THREE.Matrix4().makeTranslation(p.x, p.y, p.z);
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
