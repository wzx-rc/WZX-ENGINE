// Une figurine assemblée : le squelette, une pièce par emplacement, les kits et options.
// apply(state) ne reconstruit que ce qui a changé ; les géométries sont en cache dans parts.js.
import * as THREE from 'three';
import { Rig } from './rig.js';
import { SLOTS, KITS, buildVariant, buildKit, buildOption } from './parts.js';

const D2R = Math.PI / 180;

export function defaultXf() { return { p: [0, 0, 0], r: [0, 0, 0], s: 1, m: [false, false, false] }; }

export class Figure {
  constructor(matFn) {
    this.matFn = matFn;
    this.rig = new Rig();
    this.root = new THREE.Group();
    this.root.add(this.rig.root);
    this.slots = {};
    this.extras = {};
    this.selected = null;
    this.rig.setPose('garde', true);
  }

  variantOf(slot, id) { return slot.variants.find(v => v.id === id) || slot.variants.find(v => !v.none); }

  attach(pieces, owner) {
    return pieces.map(pc => {
      const w = new THREE.Group();
      w.userData = { owner, mirrored: !!pc.mirrored };
      for (const tag in pc.geos) {
        const m = new THREE.Mesh(pc.geos[tag], this.matFn(tag, owner === this.selected));
        m.castShadow = m.receiveShadow = true;
        m.userData = { slot: owner, tag };
        w.add(m);
      }
      this.rig.bones[pc.bone].add(w);
      return w;
    });
  }

  detach(wrappers) { for (const w of wrappers) w.removeFromParent(); }

  apply(state, instantPose = false) {
    for (const slot of SLOTS) {
      const v = this.variantOf(slot, state.sel[slot.id]);
      const key = v.id + ':' + state.wear;
      const cur = this.slots[slot.id];
      if (!cur || cur.key !== key) {
        if (cur) this.detach(cur.wrappers);
        this.slots[slot.id] = { key, variant: v, wrappers: this.attach(buildVariant(slot, v, state.wear), slot.id) };
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

  applyXf(id, xf = defaultXf()) {
    for (const w of this.slots[id].wrappers) {
      const mir = w.userData.mirrored;
      w.position.set(mir ? -xf.p[0] : xf.p[0], xf.p[1], xf.p[2]);
      w.rotation.set(xf.r[0] * D2R, (mir ? -xf.r[1] : xf.r[1]) * D2R, (mir ? -xf.r[2] : xf.r[2]) * D2R);
      w.scale.set(xf.s * (xf.m[0] ? -1 : 1), xf.s * (xf.m[1] ? -1 : 1), xf.s * (xf.m[2] ? -1 : 1));
    }
  }

  setVisible(id, v) { for (const w of this.slots[id]?.wrappers || []) w.visible = v; }

  setSelected(id) { this.selected = id; this.refreshMaterials(); }

  refreshMaterials() {
    this.root.traverse(m => { if (m.isMesh) m.material = this.matFn(m.userData.tag, m.userData.slot === this.selected); });
  }

  meshes() { const out = []; this.root.traverse(m => { if (m.isMesh) out.push(m); }); return out; }

  tris() {
    let n = 0;
    this.root.traverseVisible(m => { if (m.isMesh) { const g = m.geometry; n += (g.index ? g.index.count : g.attributes.position.count) / 3; } });
    return n;
  }

  // Groupes d'export : un par emplacement, kits et options joints au corps.
  exportGroups() {
    const groups = [];
    for (const slot of SLOTS) {
      const ws = this.slots[slot.id].wrappers.filter(w => w.visible);
      if (ws.length) groups.push({ id: slot.id, objects: ws, keyAt: this.rig.bones[slot.bone].getWorldPosition(new THREE.Vector3()) });
    }
    const ex = Object.values(this.extras).flatMap(e => e.wrappers);
    if (ex.length) groups.push({ id: 'equipement', objects: ex });
    return groups;
  }
}
