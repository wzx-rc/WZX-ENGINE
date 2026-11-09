// Une figurine assemblée : le squelette, une pièce par emplacement, les kits et options.
// apply(state) ne reconstruit que ce qui a changé ; les géométries sont en cache dans parts.js.
import * as THREE from 'three';
import { Rig } from './rig.js';
import { SLOTS, KITS, buildVariant, buildKit, buildOption, mountOf } from './parts.js';

const D2R = Math.PI / 180;

export function defaultXf() { return { p: [0, 0, 0], r: [0, 0, 0], s: 1, m: [false, false, false] }; }

// Transformation d'emplacement sur des wrappers (les pièces en miroir inversent x et les rotations y / z).
// sq : petit rebond d'une pièce qui vient d'être posée (BrickIt), écrasement en y à volume constant.
export function xfWrappers(wrappers, xf = defaultXf(), sq = 1) {
  for (const w of wrappers) {
    const mir = w.userData.mirrored;
    w.position.set(mir ? -xf.p[0] : xf.p[0], xf.p[1], xf.p[2]);
    w.rotation.set(xf.r[0] * D2R, (mir ? -xf.r[1] : xf.r[1]) * D2R, (mir ? -xf.r[2] : xf.r[2]) * D2R);
    w.scale.set(xf.s * (xf.m[0] ? -1 : 1) / Math.sqrt(sq), xf.s * (xf.m[1] ? -1 : 1) * sq, xf.s * (xf.m[2] ? -1 : 1) / Math.sqrt(sq));
  }
}

export class Figure {
  constructor(matFn) {
    this.matFn = matFn;
    this.rig = new Rig();
    this.root = new THREE.Group();
    this.root.add(this.rig.root);
    this.slots = {};
    this.extras = {};
    this.selected = null;
    this.squash = {};
    this.rig.setPose('garde', true);
  }

  variantOf(slot, id) { return slot.variants.find(v => v.id === id) || slot.variants.find(v => !v.none); }

  // ghost : matériau fantôme de la pièce en main (BrickIt), hors export, hors sélection.
  // mount : socket de réception ({ bone, pos, rot, mirror }) qui remplace l'os from pour les pièces qui y sont construites.
  attach(pieces, owner, ghost = null, mount = null, from = null) {
    return pieces.map(pc => {
      const w = new THREE.Group();
      w.userData = { owner, mirrored: !!pc.mirrored };
      for (const tag in pc.geos) {
        const m = new THREE.Mesh(pc.geos[tag], ghost || this.matFn(tag));
        m.castShadow = m.receiveShadow = !ghost;
        m.userData = ghost ? { helper: true, noExport: true } : { slot: owner, tag, outline: owner === this.selected };
        w.add(m);
      }
      if (mount && pc.bone === from) {
        const holder = new THREE.Group();
        holder.position.fromArray(mount.pos || [0, 0, 0]);
        holder.rotation.fromArray(mount.rot || [0, 0, 0]);
        if (mount.mirror) holder.scale.x = -1;
        holder.add(w);
        w.userData.holder = holder;
        this.rig.bones[mount.bone].add(holder);
      } else this.rig.bones[pc.bone].add(w);
      return w;
    });
  }

  detach(wrappers) { for (const w of wrappers) (w.userData.holder || w).removeFromParent(); }

  // Socket de réception courant d'un emplacement (null s'il n'en a qu'un).
  mount(slot, state) { return mountOf(slot, state.mount?.[slot.id]); }

  apply(state, instantPose = false) {
    for (const slot of SLOTS) {
      const v = this.variantOf(slot, state.sel[slot.id]);
      const mount = this.mount(slot, state);
      const key = v.id + ':' + state.wear + ':' + (mount ? mount.id : '');
      const cur = this.slots[slot.id];
      if (!cur || cur.key !== key) {
        if (cur) this.detach(cur.wrappers);
        this.slots[slot.id] = { key, variant: v, mount, wrappers: this.attach(buildVariant(slot, v, state.wear), slot.id, null, mount, slot.bone) };
      }
      this.applyXf(slot.id, state.xf[slot.id]);
    }
    const want = new Set([...state.kits.map(k => 'kit:' + k), ...Object.keys(state.opts).filter(k => state.opts[k]).map(k => 'opt:' + k)]);
    for (const k of Object.keys(this.extras)) {
      if (!want.has(k) || this.extras[k].wear !== state.wear) { this.detach(this.extras[k].wrappers); delete this.extras[k]; }
    }
    for (const k of want) {
      if (this.extras[k]) continue;
      const [type, id] = k.split(':');
      const pieces = type === 'kit' ? buildKit(KITS.find(x => x.id === id), state.wear) : buildOption(id, state.wear);
      this.extras[k] = { wear: state.wear, wrappers: this.attach(pieces, type === 'kit' ? 'kits' : 'options') };
    }
    this.rig.manual = JSON.parse(JSON.stringify(state.manual || {}));
    this.rig.setPose(state.pose, instantPose);
    this.root.updateMatrixWorld(true);
  }

  applyXf(id, xf = defaultXf()) { xfWrappers(this.slots[id].wrappers, xf, this.squash[id] ?? 1); }

  setVisible(id, v) { for (const w of this.slots[id]?.wrappers || []) w.visible = v; }

  setSelected(id) {
    this.selected = id;
    this.root.traverse(m => { if (m.isMesh && !m.userData.helper) m.userData.outline = !!id && m.userData.slot === id; });
  }

  refreshMaterials() {
    this.root.traverse(m => { if (m.isMesh && !m.userData.helper) m.material = this.matFn(m.userData.tag); });
  }

  meshes() { const out = []; this.root.traverse(m => { if (m.isMesh && !m.userData.helper) out.push(m); }); return out; }

  tris() {
    let n = 0;
    this.root.traverseVisible(m => { if (m.isMesh && !m.userData.helper) { const g = m.geometry; n += (g.index ? g.index.count : g.attributes.position.count) / 3; } });
    return n;
  }

  // Groupes d'export : un par emplacement, kits et options joints au corps.
  exportGroups() {
    const groups = [];
    for (const slot of SLOTS) {
      const ws = this.slots[slot.id].wrappers.filter(w => w.visible);
      const bone = this.slots[slot.id].mount?.bone || slot.bone;
      if (ws.length) groups.push({ id: slot.id, objects: ws, keyAt: this.rig.bones[bone].getWorldPosition(new THREE.Vector3()) });
    }
    const ex = Object.values(this.extras).flatMap(e => e.wrappers);
    if (ex.length) groups.push({ id: 'equipement', objects: ex });
    return groups;
  }
}
