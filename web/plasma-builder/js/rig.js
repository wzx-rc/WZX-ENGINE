// Squelette de l'Ork : os, sockets et poses.
// Les pièces STL sont rigides : chacune suit un seul os (pas de déformation de maillage).
import * as THREE from 'three';

// [nom, parent, position locale au repos]. G = +x, D = -x, avant = +z.
const BONES = [
  ['root', null, [0, 0, 0]],
  ['pelvis', 'root', [0, 1.0, 0]],
  ['spine', 'pelvis', [0, 0.22, 0.02]],
  ['chest', 'spine', [0, 0.28, 0.06]],
  ['neck', 'chest', [0, 0.3, 0.1]],
  ['head', 'neck', [0, 0.1, 0.06]],
  ['clav_L', 'chest', [0.2, 0.2, 0]],
  ['shoulder_L', 'clav_L', [0.24, 0, 0]],
  ['elbow_L', 'shoulder_L', [0.08, -0.42, 0]],
  ['hand_L', 'elbow_L', [0.02, -0.4, 0.06]],
  ['clav_R', 'chest', [-0.2, 0.2, 0]],
  ['shoulder_R', 'clav_R', [-0.24, 0, 0]],
  ['elbow_R', 'shoulder_R', [-0.08, -0.42, 0]],
  ['hand_R', 'elbow_R', [-0.02, -0.4, 0.06]],
  ['hip_L', 'pelvis', [0.18, -0.06, 0]],
  ['knee_L', 'hip_L', [0.03, -0.45, 0.04]],
  ['foot_L', 'knee_L', [0, -0.42, -0.04]],
  ['hip_R', 'pelvis', [-0.18, -0.06, 0]],
  ['knee_R', 'hip_R', [-0.03, -0.45, 0.04]],
  ['foot_R', 'knee_R', [0, -0.42, -0.04]],
];

// Os qu'on peut tourner à la main dans l'onglet Rig, par emplacement.
export const POSABLE = {
  head: ['head', 'neck'],
  torso: ['chest', 'spine'],
  armL: ['shoulder_L', 'elbow_L'],
  armR: ['shoulder_R', 'elbow_R'],
  weapon: ['hand_R'],
  legs: ['hip_L', 'knee_L', 'hip_R', 'knee_R'],
  back: ['chest'],
  shoulders: ['clav_L', 'clav_R'],
  base: [],
};

const NEUTRAL = { shoulder_L: [0, 0, 0.2], shoulder_R: [0, 0, -0.2], elbow_L: [-0.3, 0, 0], elbow_R: [-0.3, 0, 0], chest: [0.08, 0, 0], head: [-0.05, 0, 0] };

export const POSES = [
  { id: 'garde', name: 'Garde', rot: NEUTRAL },
  { id: 'tir', name: 'Tir', rot: { ...NEUTRAL, shoulder_R: [-1.35, 0.2, -0.15], elbow_R: [-0.15, 0, 0], chest: [0.05, -0.25, 0], head: [0, -0.25, 0] } },
  { id: 'visee', name: 'Visée', rot: { ...NEUTRAL, shoulder_R: [-1.5, 0.35, 0], elbow_R: [0, 0, 0], shoulder_L: [-0.9, -0.4, 0.2], elbow_L: [-1.1, 0, 0], head: [0.15, -0.2, 0], chest: [0.12, -0.3, 0] } },
  { id: 'charge', name: 'Charge', rot: { chest: [0.3, 0.1, 0], head: [-0.25, 0, 0], shoulder_L: [-1.1, 0, 0.45], elbow_L: [-0.7, 0, 0], shoulder_R: [0.6, 0, -0.3], elbow_R: [-0.5, 0, 0], hip_L: [-0.7, 0, 0], knee_L: [0.9, 0, 0], hip_R: [0.35, 0, 0], knee_R: [0.35, 0, 0], foot_R: [-0.3, 0, 0], pelvis: [0.12, 0, 0] } },
  { id: 'hurle', name: 'Hurlement', rot: { chest: [-0.12, 0, 0], neck: [-0.2, 0, 0], head: [-0.35, 0, 0], shoulder_L: [0, 0, 0.95], shoulder_R: [0, 0, -0.95], elbow_L: [-1.0, 0, 0], elbow_R: [-1.0, 0, 0], hip_L: [0, 0, 0.12], hip_R: [0, 0, -0.12] } },
  { id: 'waaagh', name: 'Waaagh !', rot: { chest: [-0.15, 0, 0], head: [-0.3, 0, 0], shoulder_L: [0, 0, 2.3], shoulder_R: [0, 0, -2.3], elbow_L: [-0.45, 0, 0], elbow_R: [-0.45, 0, 0], hip_L: [0, 0, 0.18], hip_R: [0, 0, -0.18], knee_L: [0.15, 0, 0], knee_R: [0.15, 0, 0] } },
];

export class Rig {
  constructor() {
    this.root = null;
    this.bones = {};
    for (const [name, parent, p] of BONES) {
      const b = new THREE.Bone();
      b.name = name;
      b.position.fromArray(p);
      b.userData.rest = b.position.clone();
      this.bones[name] = b;
      if (parent) this.bones[parent].add(b); else this.root = b;
    }
    this.target = {};   // quaternions visés (transition de pose)
    this.manual = {};   // rotations ajoutées à la main (onglet Rig), en radians
    this.poseId = 'garde';
    this.helper = null;
  }

  names() { return Object.keys(this.bones); }

  // Position de repos (monde) d'un os, sans pose : sert aux vignettes.
  restWorld(name) {
    const v = new THREE.Vector3();
    let b = this.bones[name];
    while (b) { v.add(b.userData.rest); b = b.parent && b.parent.isBone ? b.parent : null; }
    return v;
  }

  eulerFor(poseId, name) {
    const pose = POSES.find(p => p.id === poseId) || POSES[0];
    const r = pose.rot[name] || [0, 0, 0];
    const m = this.manual[name] || [0, 0, 0];
    return new THREE.Euler(r[0] + m[0], r[1] + m[1], r[2] + m[2]);
  }

  setPose(poseId, instant = false) {
    this.poseId = poseId;
    for (const name of this.names()) {
      const q = new THREE.Quaternion().setFromEuler(this.eulerFor(poseId, name));
      this.target[name] = q;
      if (instant) this.bones[name].quaternion.copy(q);
    }
  }

  // Applique une pose immédiatement et rend l'ancienne (pour les vignettes).
  snapshot() { const s = {}; for (const n of this.names()) s[n] = this.bones[n].quaternion.clone(); return s; }
  restore(s) { for (const n in s) this.bones[n].quaternion.copy(s[n]); this.root.updateMatrixWorld(true); }
  applyInstant(poseId) {
    for (const n of this.names()) this.bones[n].quaternion.setFromEuler(this.eulerFor(poseId, n));
    this.root.updateMatrixWorld(true);
  }

  update(dt) {
    const k = 1 - Math.exp(-dt * 10);
    let moving = false;
    for (const n of this.names()) {
      const q = this.target[n];
      if (!q) continue;
      const b = this.bones[n];
      if (b.quaternion.angleTo(q) > 1e-4) { b.quaternion.slerp(q, k); moving = true; }
    }
    return moving;
  }
}
