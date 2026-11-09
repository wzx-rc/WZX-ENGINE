// Pièce « en main », reprise du BrickIt Builder (VIEWPORT3D/web) : la pièce flotte et suit la souris
// avec un ressort, se balance et s'écrase, un fantôme et un visor montrent où elle va se poser,
// le socket visé s'allume, puis elle tombe sur son socket et rebondit.
// Ici le snap se fait sur les sockets du squelette au lieu des voxels de briques.
import * as THREE from 'three';

const SNAP = 0xffe066, SNAP_HALO = 0xf7c51e;

// Les ressorts de BrickIt sont réglés pour 60 i/s : on les intègre par pas de 1/240 s pour rester stable
// quand l'image ralentit (path tracing, machine lente).
function substeps(dt, fn) {
  const n = Math.max(1, Math.ceil(dt * 240));
  for (let i = 0; i < n; i++) fn(dt / n);
}

export class Hold {
  constructor(vp) {
    this.vp = vp;
    this.active = null;
    this.settles = [];
    this.pointer = { x: 0, y: 0, inside: false };
    // Visor et éclat : dessinés par-dessus l'image finale (scène d'aides du viewport).
    const scene = vp.overlay;

    this.ghostMat = new THREE.MeshStandardMaterial({ color: 0xffe08a, transparent: true, opacity: 0.32, depthWrite: false, emissive: 0x6b5a1a, emissiveIntensity: 0.6 });

    // Socket visé : pop avec un léger dépassement, puis pulsation (comme le voxel visé de BrickIt).
    this.glowMat = new THREE.MeshBasicMaterial({ color: SNAP, transparent: true, opacity: 0.9, depthTest: false, depthWrite: false, blending: THREE.AdditiveBlending });
    this.glow = new THREE.Mesh(new THREE.SphereGeometry(0.035, 16, 12), this.glowMat);
    this.halo = new THREE.Mesh(new THREE.SphereGeometry(0.075, 16, 12), new THREE.MeshBasicMaterial({ color: SNAP_HALO, transparent: true, opacity: 0.25, depthTest: false, depthWrite: false, blending: THREE.AdditiveBlending }));
    this.glow.add(this.halo);
    this.glow.renderOrder = this.halo.renderOrder = 1004;
    this.glow.visible = false;
    this.glow.userData.helper = true;
    scene.add(this.glow);

    // Anneau du visor, tourné vers la caméra, et ligne pointillée entre la pièce et son socket.
    this.ringMat = new THREE.MeshBasicMaterial({ color: SNAP_HALO, transparent: true, depthTest: false, depthWrite: false, side: THREE.DoubleSide });
    this.ring = new THREE.Mesh(new THREE.RingGeometry(0.075, 0.105, 40), this.ringMat);
    this.ring.renderOrder = 1001;
    this.ring.visible = false;
    this.ring.userData.helper = true;
    scene.add(this.ring);
    this.lineMat = new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: 0.04, gapSize: 0.03, transparent: true, opacity: 0.75, depthTest: false });
    this.line = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3()]), this.lineMat);
    this.line.renderOrder = 1000;
    this.line.visible = false;
    this.line.userData.helper = true;
    scene.add(this.line);

    // Sockets de réception possibles : un petit anneau sur chacun pendant que la pièce est en main.
    this.markGeo = new THREE.RingGeometry(0.035, 0.052, 32);
    this.markMat = new THREE.MeshBasicMaterial({ color: SNAP_HALO, transparent: true, opacity: 0.7, depthTest: false, depthWrite: false, side: THREE.DoubleSide });
    this.marks = [];

    this.flashT = -1;
    this.flashPos = new THREE.Vector3();
  }

  // opts : candidates (ids d'emplacements), object (Group de la pièce, origine = os du socket),
  // socketOf(id) → Vector3 monde, ghostOf(id) → wrappers fantômes accrochés aux os (ou []),
  // onDrop(id), onCancel(), auto (pose automatique sur le premier socket), from (Vector3 de départ).
  start(opts) {
    if (this.active) this.cancel();
    const obj = opts.object;
    const target = opts.socketOf(opts.candidates[0]);
    const from = opts.from ? opts.from.clone() : target.clone().add(new THREE.Vector3(0, 0.9, 0.4));
    obj.position.copy(from);
    obj.traverse(m => { if (m.isMesh) { m.castShadow = true; m.userData.helper = true; } });
    this.vp.scene.add(obj);
    this.active = {
      ...opts, obj, slot: null, ghosts: [], target: target.clone(), pos: from.clone(), vel: new THREE.Vector3(0, -2, 0),
      squash: 0.55, squashVel: 0, tilt: { x: 0, z: 0 }, time: 0, snapT: 0, dropping: false, dropT: 0,
    };
    this.pick(opts.candidates[0]);
  }

  pick(id) {
    const H = this.active;
    if (!H || H.slot === id) return;
    for (const w of H.ghosts) (w.userData.holder || w).removeFromParent();
    H.slot = id;
    H.snapT = 0;
    H.ghosts = H.ghostOf(id);
    for (const w of H.ghosts) w.traverse(m => { if (m.isMesh) { m.material = this.ghostMat; m.renderOrder = 998; m.castShadow = false; m.userData.helper = true; } });
  }

  // Socket le plus proche du pointeur, à l'écran.
  nearest() {
    const H = this.active, el = this.vp.canvas, r = el.getBoundingClientRect();
    if (!this.pointer.inside || H.auto) return H.slot;
    let best = H.slot, bd = Infinity;
    for (const id of H.candidates) {
      const p = H.socketOf(id).project(this.vp.camera);
      const d = Math.hypot((p.x + 1) / 2 * r.width + r.left - this.pointer.x, (1 - p.y) / 2 * r.height + r.top - this.pointer.y);
      if (d < bd) { bd = d; best = id; }
    }
    return best;
  }

  drop() {
    const H = this.active;
    if (!H || H.dropping) return;
    H.dropping = true; H.dropT = 0; H.dropFrom = H.pos.clone();
  }

  cancel() {
    const H = this.active;
    if (!H) return;
    this.cleanup();
    H.onCancel?.();
  }

  updateMarks(H) {
    const list = H && H.candidates.length > 1 ? H.candidates : [];
    while (this.marks.length < list.length) {
      const m = new THREE.Mesh(this.markGeo, this.markMat);
      m.renderOrder = 1002;
      m.userData.helper = true;
      this.vp.overlay.add(m);
      this.marks.push(m);
    }
    this.marks.forEach((m, i) => {
      m.visible = i < list.length && list[i] !== H.slot;
      if (!m.visible) return;
      m.position.copy(H.socketOf(list[i]));
      m.quaternion.copy(this.vp.camera.quaternion);
      m.scale.setScalar(1 + 0.15 * Math.sin(H.time * 4 + i));
    });
  }

  cleanup() {
    const H = this.active;
    for (const w of H.ghosts) (w.userData.holder || w).removeFromParent();
    H.obj.removeFromParent();
    this.glow.visible = this.ring.visible = this.line.visible = false;
    this.active = null;
    this.updateMarks(null);
  }

  // Petit rebond d'un emplacement qui reçoit une pièce : squash de 0.86 vers 1 sur un ressort.
  settle(id, s = 0.86) { this.settles.push({ id, s, v: 0, t: 0 }); }

  // Éclat bref sur un socket (sélection).
  flash(pos) { this.flashPos.copy(pos); this.flashT = 0; }

  update(dt, applySquash) {
    for (let i = this.settles.length - 1; i >= 0; i--) {
      const s = this.settles[i];
      s.t += dt;
      const w = 30, z = 0.28;
      substeps(dt, h => { s.v += ((1 - s.s) * w * w - s.v * 2 * z * w) * h; s.s += s.v * h; });
      const done = s.t > 0.6;
      applySquash(s.id, done ? 1 : s.s);
      if (done) this.settles.splice(i, 1);
    }
    const H = this.active;
    if (!H) { this.updateFlash(dt); return; }
    H.time += dt;
    this.pick(this.nearest());
    H.target.copy(H.socketOf(H.slot));
    this.updateMarks(H);
    const obj = H.obj;

    if (H.dropping) {
      H.dropT += dt / 0.15;
      const t = Math.min(H.dropT, 1);
      H.pos.lerpVectors(H.dropFrom, H.target, t * t);
      H.tilt.x *= 0.7; H.tilt.z *= 0.7;
      this.place(obj, H);
      if (H.dropT >= 1) {
        const id = H.slot;
        this.cleanup();
        H.onDrop(id);
        this.settle(id);
      }
      return;
    }

    const bob = 0.03 * Math.sin(H.time * 3) + 0.01 * Math.sin(H.time * 7.3);
    // Au-dessus du socket et un peu vers la caméra, pour que la pièce ne passe pas dans le corps.
    const toCam = this.vp.camera.position.clone().sub(H.target).setY(0).normalize().multiplyScalar(0.3);
    const desired = H.target.clone().add(toCam).add(new THREE.Vector3(0, 0.32 + bob, 0));
    const w = 14, acc = new THREE.Vector3();
    substeps(dt, h => {
      acc.copy(desired).sub(H.pos).multiplyScalar(w * w).addScaledVector(H.vel, -2 * w);
      H.vel.addScaledVector(acc, h);
      H.pos.addScaledVector(H.vel, h);
    });
    const k = 1 - Math.exp(-dt * 10);
    const tx = THREE.MathUtils.clamp(H.vel.z * 0.08, -0.3, 0.3), tz = THREE.MathUtils.clamp(-H.vel.x * 0.08, -0.3, 0.3);
    H.tilt.x += (tx + 0.03 * Math.sin(H.time * 2.1) - H.tilt.x) * k;
    H.tilt.z += (tz + 0.03 * Math.sin(H.time * 1.7) - H.tilt.z) * k;
    const ws = 22, zs = 0.35;
    substeps(dt, h => { H.squashVel += ((1 - H.squash) * ws * ws - H.squashVel * 2 * zs * ws) * h; H.squash += H.squashVel * h; });
    this.place(obj, H);

    // Visor : socket allumé, anneau qui respire, ligne de chute.
    const pulse = 0.5 + 0.5 * Math.sin(H.time * 5);
    H.snapT += dt;
    const t = Math.min(H.snapT / 0.18, 1);
    const pop = t < 1 ? 1 - Math.pow(1 - t, 3) * Math.cos(t * 6) : 1;
    this.glow.visible = true;
    this.glow.position.copy(H.target);
    this.glow.scale.setScalar(Math.max(0.01, pop) * (1 + 0.15 * pulse));
    this.glowMat.opacity = 0.45 + 0.35 * t;
    this.halo.material.opacity = (0.08 + 0.12 * pulse) * t;
    this.ring.visible = true;
    this.ring.position.copy(H.target);
    this.ring.quaternion.copy(this.vp.camera.quaternion);
    this.ring.scale.setScalar(1 + 0.35 * pulse);
    this.line.geometry.setFromPoints([H.pos, H.target]);
    this.line.computeLineDistances();
    this.line.visible = true;
    this.ghostMat.opacity = 0.22 + 0.12 * pulse;

    // Pose automatique (clic sur une vignette) : la pièce vole jusqu'à son socket puis tombe.
    if (H.auto && H.time > 0.45 && H.pos.distanceTo(desired.copy(H.target).add(toCam).add(new THREE.Vector3(0, 0.32, 0))) < 0.08) this.drop();
  }

  place(obj, H) {
    obj.position.copy(H.pos);
    obj.rotation.set(H.tilt.x, 0, H.tilt.z, 'XZY');
    obj.scale.set(1, H.squash, 1);
  }

  updateFlash(dt) {
    if (this.flashT < 0) { this.glow.visible = false; return; }
    this.flashT += dt;
    const t = Math.min(this.flashT / 0.18, 1);
    const pop = t < 1 ? 1 - Math.pow(1 - t, 3) * Math.cos(t * 6) : 1;
    const fade = Math.max(0, 1 - Math.max(0, this.flashT - 0.25) / 0.35);
    this.glow.visible = fade > 0;
    this.glow.position.copy(this.flashPos);
    this.glow.scale.setScalar(Math.max(0.01, pop));
    this.glowMat.opacity = 0.8 * fade;
    this.halo.material.opacity = 0.2 * fade;
    if (fade <= 0) this.flashT = -1;
  }
}
