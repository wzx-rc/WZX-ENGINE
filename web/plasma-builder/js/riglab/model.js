// Ork riggé importé de Maya (assets/ork_boy.glb, produit par tools/mb2glb) : squelette de 60 joints,
// basemesh low skinné, high-poly avec poids transférés, tête / dents / yeux rigides sur Head.
// La pose est une rotation locale par joint, relative au repos (bone.quaternion = repos * offset),
// plus une translation du bassin.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { applySlots } from './meshslots.js';

const HIPS = 'QuickRigCharacter_Hips';
const Q = n => 'QuickRigCharacter_' + n;

// Côté gauche de l'Ork = +x (convention du builder). Les noms QuickRig « Left…1 » sont le côté droit.
const LABELS = {
  [Q('Hips')]: 'Bassin', [Q('Spine')]: 'Bas du dos', [Q('Spine1')]: 'Dos', [Q('Spine2')]: 'Torse',
  [Q('Neck')]: 'Cou', [Q('Head')]: 'Tête',
  joint40: 'Clavicule', [Q('LeftArm')]: 'Bras', [Q('LeftForeArm')]: 'Avant-bras', [Q('LeftHand')]: 'Poignet',
  joint41: 'Clavicule', [Q('LeftArm1')]: 'Bras', [Q('LeftForeArm1')]: 'Avant-bras', [Q('LeftHand1')]: 'Poignet',
  [Q('LeftUpLeg')]: 'Cuisse', [Q('LeftLeg')]: 'Genou', [Q('LeftFoot')]: 'Cheville', [Q('LeftToeBase')]: 'Orteils',
  [Q('LeftUpLeg1')]: 'Cuisse', [Q('LeftLeg1')]: 'Genou', [Q('LeftFoot1')]: 'Cheville', [Q('LeftToeBase1')]: 'Orteils',
};
// Doigts : [pouce, index, majeur, annulaire, auriculaire], joints de la base vers le bout.
export const FINGERS = {
  L: [['joint1', 'joint2', 'joint3'], ['joint5', 'joint6', 'joint7', 'joint8'], ['joint9', 'joint10', 'joint11', 'joint12'], ['joint13', 'joint14', 'joint15', 'joint16'], ['joint17', 'joint18', 'joint19', 'joint20']],
  R: [['joint37', 'joint38', 'joint39'], ['joint33', 'joint34', 'joint35', 'joint36'], ['joint29', 'joint30', 'joint31', 'joint32'], ['joint25', 'joint26', 'joint27', 'joint28'], ['joint21', 'joint22', 'joint23', 'joint24']],
};
const FINGER_NAMES = ['Pouce', 'Index', 'Majeur', 'Annulaire', 'Auriculaire'];

export const GROUPS = [
  { id: 'body', label: 'Corps', bones: [Q('Hips'), Q('Spine'), Q('Spine1'), Q('Spine2'), Q('Neck'), Q('Head')] },
  { id: 'armL', label: 'Bras gauche', bones: ['joint40', Q('LeftArm'), Q('LeftForeArm'), Q('LeftHand')] },
  { id: 'armR', label: 'Bras droit', bones: ['joint41', Q('LeftArm1'), Q('LeftForeArm1'), Q('LeftHand1')] },
  { id: 'handL', label: 'Main gauche', bones: FINGERS.L.flat() },
  { id: 'handR', label: 'Main droite', bones: FINGERS.R.flat() },
  { id: 'legL', label: 'Jambe gauche', bones: [Q('LeftUpLeg'), Q('LeftLeg'), Q('LeftFoot'), Q('LeftToeBase')] },
  { id: 'legR', label: 'Jambe droite', bones: [Q('LeftUpLeg1'), Q('LeftLeg1'), Q('LeftFoot1'), Q('LeftToeBase1')] },
];

// Chaînes IK à deux os : racine, milieu, effecteur.
export const CHAINS = {
  handL: [Q('LeftArm'), Q('LeftForeArm'), Q('LeftHand')],
  handR: [Q('LeftArm1'), Q('LeftForeArm1'), Q('LeftHand1')],
  footL: [Q('LeftUpLeg'), Q('LeftLeg'), Q('LeftFoot')],
  footR: [Q('LeftUpLeg1'), Q('LeftLeg1'), Q('LeftFoot1')],
};

// Libellés traduits par i18n.js (plasmaT) quand l'interface est en anglais ; côté G / D devient L / R.
const T = s => (globalThis.plasmaT ? globalThis.plasmaT(s) : s);
const SIDE = globalThis.plasmaLang === 'fr' ? { L: 'G', R: 'D' } : { L: 'L', R: 'R' };
export function label(name) {
  for (const side of ['L', 'R']) {
    const f = FINGERS[side].findIndex(c => c.includes(name));
    if (f >= 0) return `${T(FINGER_NAMES[f])} ${FINGERS[side][f].indexOf(name) + 1} ${SIDE[side]}`;
  }
  const g = GROUPS.find(g => g.bones.includes(name));
  const side = g && /L$/.test(g.id) ? ' ' + SIDE.L : g && /R$/.test(g.id) ? ' ' + SIDE.R : '';
  return T(LABELS[name] || name) + side;
}

const _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3();

export class OrkModel {
  static async load(url, onProgress) {
    const loader = new GLTFLoader();
    let gltf = await loader.loadAsync(url, onProgress).catch(() => null);
    // Hébergeurs qui ne servent pas le .glb (démo artifact) : copie en base64 à côté.
    if (!gltf) {
      const res = await fetch(url + '.b64.txt');
      if (!res.ok) throw new Error('GLB introuvable : ' + url);
      const bin = Uint8Array.from(atob((await res.text()).trim()), c => c.charCodeAt(0));
      gltf = await loader.parseAsync(bin.buffer, '');
    }
    const model = new OrkModel(gltf);
    // meshes remplacés dans le Rig Lab (Meshes) : même rig, autre maillage
    await applySlots(model);
    return model;
  }

  constructor(gltf) {
    this.root = gltf.scene;                      // groupe à l'échelle du viewport (le GLB est en cm)
    this.ork = this.root.getObjectByName('ORK_BOY');
    this.info = this.ork.userData || {};
    this.meshes = {};
    this.root.traverse(o => {
      if (o.isSkinnedMesh) {
        this.meshes[o.name] = o;
        if (!o.geometry.attributes.normal) o.geometry.computeVertexNormals();   // le GLB n'en stocke pas
        o.castShadow = o.receiveShadow = true;
        o.frustumCulled = false;
      }
    });
    this.skeleton = this.meshes.BODY_low.skeleton;
    this.bones = this.skeleton.bones;
    this.index = new Map(this.bones.map((b, i) => [b.name, i]));
    this.hips = this.bone(HIPS);
    this.root.updateMatrixWorld(true);
    for (const b of this.bones) {
      b.userData.restQ = b.quaternion.clone();
      b.userData.restP = b.position.clone();
      b.userData.restWQ = b.getWorldQuaternion(new THREE.Quaternion());
      b.userData.restW = b.getWorldPosition(new THREE.Vector3());   // espace de ORK_BOY = espace du root non mis à l'échelle
    }
    // Ordre parent avant enfant, utile au miroir.
    this.ordered = [];
    const visit = b => { this.ordered.push(b); for (const c of b.children) if (c.isBone) visit(c); };
    visit(this.hips);
    this.computeMirror();
    this.computeCurlAxes();
    // Poids du low par sommet, pour retrouver l'os sous la souris (picking sur le low, rapide).
    const lw = this.meshes.BODY_low.geometry;
    this.lowSkin = { index: lw.attributes.skinIndex, weight: lw.attributes.skinWeight };
  }

  bone(name) { return this.bones[this.index.get(name)]; }

  // Échelle viewport : le builder travaille avec une figurine d'environ 2.1 unités de haut, plateau à y = -0.1.
  fit(height = 2.1, floorY = -0.1) {
    const box = new THREE.Box3().setFromObject(this.meshes.BODY_high);
    const k = height / (box.max.y - box.min.y);
    this.root.scale.setScalar(k);
    this.root.position.set(0, floorY - box.min.y * k, 0);
    this.k = k;
    this.root.updateMatrixWorld(true);
  }

  // Paires gauche / droite : le joint dont la position de repos est la plus proche du symétrique (x -> -x).
  computeMirror() {
    this.mirror = new Map();
    for (const b of this.bones) {
      const p = b.userData.restW.clone(); p.x = -p.x;
      let best = b, d = Infinity;
      for (const c of this.bones) { const e = c.userData.restW.distanceToSquared(p); if (e < d) { d = e; best = c; } }
      this.mirror.set(b, best);
    }
  }

  // Axe de flexion des doigts : l'axe qui traverse la paume (pouce -> auriculaire), en local de chaque phalange.
  computeCurlAxes() {
    this.curlAxis = new Map();
    for (const side of ['L', 'R']) {
      const ch = FINGERS[side];
      const across = this.bone(ch[4][0]).userData.restW.clone().sub(this.bone(ch[1][0]).userData.restW).normalize();
      for (let f = 0; f < 5; f++) {
        let axis = across.clone();
        if (f === 0) {   // le pouce se replie vers la paume : axe le long de la main
          const hand = this.bone(CHAINS[side === 'L' ? 'handL' : 'handR'][2]);
          axis = this.bone(ch[2][0]).userData.restW.clone().sub(hand.userData.restW).normalize();
        }
        if (side === 'R') axis.negate();   // l'axe est un vecteur polaire : la symétrie inverse le sens de rotation
        for (const n of ch[f]) {
          const b = this.bone(n);
          this.curlAxis.set(b, axis.clone().applyQuaternion(b.userData.restWQ.clone().invert()));
        }
      }
    }
  }

  // ---------------------------------------------------------------- état de pose
  offset(b) { return b.userData.restQ.clone().invert().multiply(b.quaternion); }

  getPose() {
    const rot = {};
    for (const b of this.bones) {
      const o = this.offset(b);
      if (1 - Math.abs(o.w) > 1e-7) rot[b.name] = o.toArray().map(x => +x.toFixed(6));
    }
    const t = this.hips.position.clone().sub(this.hips.userData.restP);
    return { rot, hips: t.toArray().map(x => +x.toFixed(5)) };
  }

  setPose(pose) {
    for (const b of this.bones) {
      const o = pose?.rot?.[b.name];
      b.quaternion.copy(b.userData.restQ);
      if (o) b.quaternion.multiply(_q.fromArray(o));
    }
    this.hips.position.copy(this.hips.userData.restP);
    if (pose?.hips) this.hips.position.add(_v.fromArray(pose.hips));
    this.root.updateMatrixWorld(true);
  }

  resetBone(b) { b.quaternion.copy(b.userData.restQ); if (b === this.hips) b.position.copy(b.userData.restP); }

  // Rotation d'un os autour d'un axe exprimé dans l'espace de la figurine (cm, avant = +z, gauche = +x).
  rotateWorld(name, axis, deg) {
    const b = typeof name === 'string' ? this.bone(name) : name;
    this.ork.updateMatrixWorld(true);
    const parentQ = this.figureQuat(b.parent);
    _q.setFromAxisAngle(_v.fromArray(axis).normalize(), THREE.MathUtils.degToRad(deg));
    const wq = parentQ.clone().multiply(b.quaternion);
    b.quaternion.copy(parentQ.invert().multiply(_q.multiply(wq)));
    b.updateMatrixWorld(true);
  }

  // Rotation monde d'un objet relative à ORK_BOY (sans l'échelle du viewport).
  figureQuat(o) {
    const q = new THREE.Quaternion();
    const chain = [];
    for (let x = o; x && x !== this.ork; x = x.parent) chain.push(x);
    for (let i = chain.length - 1; i >= 0; i--) q.multiply(chain[i].quaternion);
    return q;
  }
  figurePos(o, target = new THREE.Vector3()) {
    o.updateWorldMatrix(true, false);
    return this.ork.worldToLocal(o.getWorldPosition(target));
  }

  // ---------------------------------------------------------------- doigts
  curl(side, amount) {
    FINGERS[side].forEach((chain, f) => {
      chain.forEach((n, k) => {
        const b = this.bone(n);
        const a = (f === 0 ? 0.6 : 1) * amount * (k === 0 ? 0.7 : 1);
        b.quaternion.copy(b.userData.restQ).multiply(_q.setFromAxisAngle(this.curlAxis.get(b), a));
      });
    });
    this.root.updateMatrixWorld(true);
  }

  // ---------------------------------------------------------------- miroir
  // Rotation monde de chaque joint relative à son repos, symétrisée (x, -y, -z, w), appliquée au joint opposé.
  mirrorPose(mode = 'swap') {
    const delta = new Map();
    for (const b of this.bones) delta.set(b, this.figureQuat(b).multiply(b.userData.restWQ.clone().invert()));
    const hipsT = this.hips.position.clone().sub(this.hips.userData.restP);
    for (const b of this.ordered) {
      const src = this.mirror.get(b);
      const side = b.userData.restW.x > 0.5 ? 'L' : b.userData.restW.x < -0.5 ? 'R' : 'C';
      if (mode === 'LtoR' && side !== 'R') continue;
      if (mode === 'RtoL' && side !== 'L') continue;
      const d = delta.get(src);
      const m = new THREE.Quaternion(d.x, -d.y, -d.z, d.w);
      const target = m.multiply(b.userData.restWQ);
      const parentQ = this.figureQuat(b.parent);
      b.quaternion.copy(parentQ.invert().multiply(target));
    }
    if (mode === 'swap') { hipsT.x = -hipsT.x; this.hips.position.copy(this.hips.userData.restP).add(hipsT); }
    this.root.updateMatrixWorld(true);
  }

  // Recopie la rotation de quelques os sur leurs opposés (symétrie auto, « os → opposé »).
  mirrorBones(list) {
    const set = new Set(list);
    for (const b of this.ordered) {
      if (!set.has(b)) continue;
      const m = this.mirror.get(b);
      if (m === b) continue;
      const d = this.figureQuat(b).multiply(b.userData.restWQ.clone().invert());
      const target = new THREE.Quaternion(d.x, -d.y, -d.z, d.w).multiply(m.userData.restWQ);
      m.quaternion.copy(this.figureQuat(m.parent).invert().multiply(target));
    }
    this.root.updateMatrixWorld(true);
  }

  // ---------------------------------------------------------------- IK deux os
  // Cible en espace figure. Garde le plan du membre (le coude / genou reste du même côté) et l'orientation
  // monde de l'effecteur (la main ne tourne pas quand on la déplace).
  solveIK(chainId, target, keepEnd = true) {
    const [a, b, c] = CHAINS[chainId].map(n => this.bone(n));
    this.ork.updateMatrixWorld(true);
    const endQ = this.figureQuat(c);
    const pa = this.figurePos(a), pb = this.figurePos(b), pc = this.figurePos(c);
    const l1 = pa.distanceTo(pb), l2 = pb.distanceTo(pc);
    const toT = _v.copy(target).sub(pa);
    let d = toT.length();
    d = THREE.MathUtils.clamp(d, Math.abs(l1 - l2) + 1e-3, l1 + l2 - 1e-3);
    const dir = toT.normalize();
    // plan courant du membre ; s'il est dégénéré (bras tendu), on garde le plan du repos.
    let n = _v2.copy(pb).sub(pa).cross(_v3.copy(pc).sub(pa));
    if (n.lengthSq() < 1e-6) n = b.userData.restW.clone().sub(a.userData.restW).cross(c.userData.restW.clone().sub(a.userData.restW));
    n.normalize();
    const perp = new THREE.Vector3().crossVectors(n, dir).normalize();
    if (perp.dot(_v3.copy(pb).sub(pa)) < 0) perp.negate();
    const cosA = THREE.MathUtils.clamp((l1 * l1 + d * d - l2 * l2) / (2 * l1 * d), -1, 1);
    const sinA = Math.sqrt(1 - cosA * cosA);
    const pbNew = pa.clone().addScaledVector(dir, l1 * cosA).addScaledVector(perp, l1 * sinA);
    this.aim(a, pb.clone().sub(pa).normalize(), pbNew.clone().sub(pa).normalize());
    const pb2 = this.figurePos(b), pc2 = this.figurePos(c);
    const goal = pa.clone().addScaledVector(dir, d);
    this.aim(b, pc2.clone().sub(pb2).normalize(), goal.sub(pb2).normalize());
    if (keepEnd) c.quaternion.copy(this.figureQuat(c.parent).invert().multiply(endQ));
    this.root.updateMatrixWorld(true);
  }

  // Tourne un os du plus petit arc qui amène la direction `from` sur `to` (espace figure).
  aim(b, from, to) {
    _q.setFromUnitVectors(from, to);
    const parentQ = this.figureQuat(b.parent);
    const wq = parentQ.clone().multiply(b.quaternion);
    b.quaternion.copy(parentQ.invert().multiply(_q.multiply(wq)));
    b.updateMatrixWorld(true);
  }

  // ---------------------------------------------------------------- picking
  // Os dominant au point touché du low (somme des poids des trois sommets, pondérée par les barycentriques).
  boneAtFace(face, bary) {
    const acc = new Map();
    [face.a, face.b, face.c].forEach((vi, k) => {
      const w = bary ? bary.getComponent(k) : 1 / 3;
      for (let j = 0; j < 4; j++) {
        const idx = this.lowSkin.index.getComponent(vi, j), wt = this.lowSkin.weight.getComponent(vi, j);
        if (wt > 0) acc.set(idx, (acc.get(idx) || 0) + wt * w);
      }
    });
    let best = -1, bw = -1;
    for (const [i, w] of acc) if (w > bw) { bw = w; best = i; }
    return this.bones[best];
  }

  // Géométrie posée (pour l'export STL) : skinning appliqué sur le CPU, en coordonnées monde.
  bakedMeshes(names) {
    this.root.updateMatrixWorld(true);
    const out = [];
    for (const n of names) {
      const m = this.meshes[n];
      if (!m) continue;
      const src = m.geometry, pos = src.attributes.position, arr = new Float32Array(pos.count * 3);
      m.skeleton.update();
      for (let i = 0; i < pos.count; i++) {
        m.getVertexPosition(i, _v);
        _v.applyMatrix4(m.matrixWorld);
        arr[i * 3] = _v.x; arr[i * 3 + 1] = _v.y; arr[i * 3 + 2] = _v.z;
      }
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.BufferAttribute(arr, 3));
      g.setIndex(src.index.clone());
      out.push(new THREE.Mesh(g));
    }
    return out;
  }
}
