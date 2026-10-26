// Bibliothèque de pièces de départ, générées procéduralement en attendant les vrais STL.
// Une variante = une liste de « pièces », chacune attachée à un os, avec une géométrie par
// matière (skin, metal, leather, cloth, wood, bone, stone, eye) pour le mode « Peint ».
import * as THREE from 'three';
import { mergeGeometries, mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';

// ---------- bruit (relief organique, usure) ----------
function hash(x, y, z) {
  let h = Math.imul(x, 374761393) ^ Math.imul(y, 668265263) ^ Math.imul(z, 1274126177);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}
function vnoise(x, y, z) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const xf = x - xi, yf = y - yi, zf = z - zi;
  const s = t => t * t * (3 - 2 * t);
  const u = s(xf), v = s(yf), w = s(zf);
  const L = (a, b, t) => a + (b - a) * t;
  const c = (dx, dy, dz) => hash(xi + dx, yi + dy, zi + dz);
  return L(L(L(c(0, 0, 0), c(1, 0, 0), u), L(c(0, 1, 0), c(1, 1, 0), u), v),
           L(L(c(0, 0, 1), c(1, 0, 1), u), L(c(0, 1, 1), c(1, 1, 1), u), v), w) * 2 - 1;
}
function fbm(x, y, z) { return vnoise(x, y, z) * 0.6 + vnoise(x * 2.1, y * 2.1, z * 2.1) * 0.28 + vnoise(x * 4.3, y * 4.3, z * 4.3) * 0.12; }

// Prépare une primitive : position seule, sommets soudés (ombrage lisse, pas de fissure au relief).
function prep(g) {
  g.deleteAttribute('uv');
  g.deleteAttribute('normal');
  return mergeVertices(g);
}

// Relief le long de la direction depuis le centre de la primitive, avant la mise en place.
function displace(g, amp, freq, seed) {
  if (amp <= 0) return;
  const p = g.attributes.position, v = new THREE.Vector3(), d = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.fromBufferAttribute(p, i);
    d.copy(v).normalize();
    const n = fbm(v.x * freq + seed, v.y * freq + seed * 0.7, v.z * freq - seed);
    v.addScaledVector(d, n * amp);
    p.setXYZ(i, v.x, v.y, v.z);
  }
}

class Builder {
  constructor(wear = 0) { this.items = {}; this.wear = wear; this.seed = 1; }
  put(tag, g, { s = [1, 1, 1], p = [0, 0, 0], r = [0, 0, 0], n = 0, f = 5 } = {}) {
    g = prep(g);
    const extra = tag === 'metal' ? this.wear * 0.012 : tag === 'skin' ? this.wear * 0.008 : 0;
    displace(g, n + extra, f + (extra ? 9 : 0), this.seed++ * 3.17);
    const m = new THREE.Matrix4().compose(new THREE.Vector3(...p), new THREE.Quaternion().setFromEuler(new THREE.Euler(...r)), new THREE.Vector3(...s));
    g.applyMatrix4(m);
    (this.items[tag] ||= []).push(g);
    return this;
  }
  blob(tag, s, p, r, n = 0.025, f = 5) { return this.put(tag, new THREE.SphereGeometry(1, 40, 28), { s, p, r, n, f }); }
  dome(tag, s, p, r) { return this.put(tag, new THREE.SphereGeometry(1, 36, 14, 0, Math.PI * 2, 0, Math.PI / 2), { s, p, r }); }
  box(tag, s, p, r, rad = 0.02, n = 0) {
    const g = new RoundedBoxGeometry(s[0], s[1], s[2], 3, Math.min(rad, Math.min(...s) / 2 - 1e-4));
    return this.put(tag, g, { p, r, n, f: 9 });
  }
  cyl(tag, rt, rb, h, p, r, seg = 24) { return this.put(tag, new THREE.CylinderGeometry(rt, rb, h, seg, 1), { p, r }); }
  cone(tag, rad, h, p, r) { return this.put(tag, new THREE.ConeGeometry(rad, h, 16, 1), { p, r }); }
  cap(tag, rad, len, p, r, n = 0.012) { return this.put(tag, new THREE.CapsuleGeometry(rad, len, 8, 24), { p, r, n, f: 7 }); }
  torus(tag, R, t, p, r) { return this.put(tag, new THREE.TorusGeometry(R, t, 10, 32), { p, r }); }
  rivets(tag, pts, size = 0.018) { for (const p of pts) this.blob(tag, [size, size, size], p, [0, 0, 0], 0); return this; }
  build() {
    const out = {};
    for (const tag in this.items) {
      const g = mergeGeometries(this.items[tag], false);
      g.computeVertexNormals();
      g.computeBoundingBox();
      out[tag] = g;
    }
    return out;
  }
}

// Miroir gauche → droite : x inversé, triangles retournés pour garder les normales vers l'extérieur.
function mirrorGeo(g) {
  const m = g.clone();
  m.scale(-1, 1, 1);
  const idx = m.index;
  if (idx) for (let i = 0; i < idx.count; i += 3) { const a = idx.getX(i + 1); idx.setX(i + 1, idx.getX(i + 2)); idx.setX(i + 2, a); }
  m.computeVertexNormals();
  m.computeBoundingBox();
  return m;
}
export function mirrorPieces(pieces) {
  return pieces.map(pc => {
    const geos = {};
    for (const t in pc.geos) geos[t] = mirrorGeo(pc.geos[t]);
    return { bone: pc.bone.replace(/_L$/, '_R'), geos, mirrored: true };
  });
}
const piece = (bone, b) => ({ bone, geos: b.build() });
const H = Math.PI / 2;

// ---------- Têtes ----------
function headBase(b, o = {}) {
  const jaw = o.jaw || [0.3, 0.13, 0.24];
  b.blob('skin', [0.2, 0.19, 0.21], [0, 0.12, 0.02], [0, 0, 0], 0.02, 6);
  b.blob('skin', [0.23, 0.07, 0.1], [0, 0.15, 0.15], [0.1, 0, 0], 0.015, 8);
  b.blob('skin', [0.06, 0.05, 0.065], [0, 0.085, 0.2], [0, 0, 0], 0.01);
  b.cone('skin', 0.05, 0.22, [0.22, 0.12, -0.01], [0, 0, -1.25]);
  b.cone('skin', 0.05, 0.22, [-0.22, 0.12, -0.01], [0, 0, 1.25]);
  b.blob('eye', [0.028, 0.028, 0.028], [0.075, 0.115, 0.19], [0, 0, 0], 0);
  b.blob('eye', [0.028, 0.028, 0.028], [-0.075, 0.115, 0.19], [0, 0, 0], 0);
  b.box('skin', jaw, o.jawP || [0, -0.02, 0.12], o.jawR || [0, 0, 0], 0.06, 0.015);
  const tusk = o.tusk ?? 0.09;
  if (tusk > 0) for (const sx of o.tusks || [1, -1]) b.cone('bone', 0.026, tusk, [sx * jaw[0] * 0.34, (o.jawP ? o.jawP[1] : -0.02) + jaw[1] * 0.5 + tusk * 0.4, (o.jawP ? o.jawP[2] : 0.12) + jaw[2] * 0.4], [0.2, 0, -sx * 0.15]);
  return b;
}
const HEADS = [
  { id: 'h_boss', name: 'Mâchoire Boss', tags: ['Boss'], build: w => [piece('head', headBase(new Builder(w), { jaw: [0.36, 0.15, 0.28], tusk: 0.13 }))] },
  { id: 'h_helm', name: 'Casque Kasse-Kou', tags: ['Classique'], build: w => {
    const b = headBase(new Builder(w));
    b.dome('metal', [0.235, 0.17, 0.245], [0, 0.17, 0.01], [0, 0, 0]);
    b.box('metal', [0.5, 0.03, 0.5], [0, 0.17, 0.01], [0, 0, 0], 0.012);
    b.cone('bone', 0.04, 0.2, [0.17, 0.33, 0], [0, 0, -0.55]);
    b.cone('bone', 0.04, 0.2, [-0.17, 0.33, 0], [0, 0, 0.55]);
    b.rivets('metal', [[0.16, 0.2, 0.17], [-0.16, 0.2, 0.17], [0, 0.22, 0.24]]);
    return [piece('head', b)];
  } },
  { id: 'h_scream', name: 'Tête Hurleuse', tags: ['Classique'], build: w => {
    const b = headBase(new Builder(w), { jawP: [0, -0.09, 0.1], jawR: [0.38, 0, 0], tusk: 0.11 });
    for (let i = -2; i <= 2; i++) b.cone('bone', 0.016, 0.05, [i * 0.045, 0.045, 0.2], [Math.PI, 0, 0]);
    return [piece('head', b)];
  } },
  { id: 'h_bionik', name: 'Œil Bionik', tags: ['Bionik', 'Kustom'], build: w => {
    const b = headBase(new Builder(w), { tusks: [-1] });
    b.box('metal', [0.11, 0.2, 0.22], [0.15, 0.13, 0.05], [0, 0, -0.15], 0.02);
    b.cyl('metal', 0.045, 0.05, 0.07, [0.075, 0.115, 0.215], [H, 0, 0]);
    b.torus('metal', 0.05, 0.012, [0.075, 0.115, 0.25], [0, 0, 0]);
    b.rivets('metal', [[0.2, 0.2, 0.13], [0.2, 0.06, 0.13], [0.21, 0.13, 0.0]]);
    return [piece('head', b)];
  } },
];

// ---------- Corps ----------
function torsoBase(b) {
  b.blob('skin', [0.42, 0.34, 0.32], [0, 0.08, 0.02], [0, 0, 0], 0.025, 4);
  b.blob('skin', [0.21, 0.15, 0.12], [0.15, 0.1, 0.22], [0, 0, 0], 0.01);
  b.blob('skin', [0.21, 0.15, 0.12], [-0.15, 0.1, 0.22], [0, 0, 0], 0.01);
  b.blob('skin', [0.24, 0.13, 0.19], [0.2, 0.22, -0.02], [0, 0, -0.3], 0.012);
  b.blob('skin', [0.24, 0.13, 0.19], [-0.2, 0.22, -0.02], [0, 0, 0.3], 0.012);
  b.blob('skin', [0.34, 0.26, 0.25], [0, 0.17, -0.12], [0, 0, 0], 0.02);
  return b;
}
const belly = (w = 0) => new Builder(w).blob('skin', [0.36, 0.3, 0.3], [0, 0.06, 0.08], [0, 0, 0], 0.02, 4);
const TORSOS = [
  { id: 't_boy', name: 'Torse Boy', tags: ['Classique'], build: w => [piece('chest', torsoBase(new Builder(w))), piece('spine', belly(w))] },
  { id: 't_mega', name: 'Armure Méga', tags: ['Boss'], build: w => {
    const b = torsoBase(new Builder(w));
    b.box('metal', [0.62, 0.36, 0.12], [0, 0.06, 0.3], [-0.15, 0, 0], 0.04);
    b.box('metal', [0.54, 0.2, 0.2], [0, 0.3, -0.06], [0.2, 0, 0], 0.05);
    b.rivets('metal', [[-0.24, 0.18, 0.37], [-0.08, 0.2, 0.38], [0.08, 0.2, 0.38], [0.24, 0.18, 0.37], [-0.24, -0.06, 0.34], [0.24, -0.06, 0.34]]);
    const s = belly(w);
    s.box('metal', [0.52, 0.2, 0.1], [0, 0.02, 0.34], [0.1, 0, 0], 0.03);
    s.box('metal', [0.48, 0.16, 0.09], [0, -0.14, 0.32], [0.25, 0, 0], 0.03);
    return [piece('chest', b), piece('spine', s)];
  } },
  { id: 't_rust', name: 'Plastron Rouillé', tags: ['Kustom'], build: w => {
    const b = torsoBase(new Builder(w));
    b.box('metal', [0.36, 0.26, 0.07], [0.09, 0.05, 0.31], [-0.12, 0, 0.18], 0.02, 0.006);
    b.box('leather', [0.08, 0.86, 0.035], [0, 0.04, 0.32], [-0.1, 0, 0.62], 0.012);
    b.blob('bone', [0.065, 0.075, 0.065], [-0.17, 0.17, 0.33], [0, 0, 0], 0.006);
    b.rivets('metal', [[0, 0.12, 0.35], [0.2, 0.0, 0.35]]);
    return [piece('chest', b), piece('spine', belly(w))];
  } },
];

// ---------- Bras (construits côté gauche, miroir pour la droite) ----------
function upperArm(b, metal = false) {
  if (metal) {
    b.cyl('metal', 0.07, 0.06, 0.36, [0.04, -0.2, 0], [0, 0, 0.19]);
    b.cyl('metal', 0.02, 0.02, 0.32, [0.1, -0.2, 0.06], [0, 0, 0.19]);
    b.blob('metal', [0.11, 0.11, 0.11], [0, -0.01, 0], [0, 0, 0], 0);
  } else {
    b.cap('skin', 0.11, 0.3, [0.04, -0.2, 0], [0, 0, 0.19]);
    b.blob('skin', [0.13, 0.17, 0.13], [0.03, -0.16, 0.035], [0, 0, 0.19], 0.012);
    b.blob('skin', [0.15, 0.13, 0.15], [0, -0.02, 0], [0, 0, 0], 0.015);
  }
  return b;
}
function foreArm(b, metal = false) {
  if (metal) {
    b.cyl('metal', 0.07, 0.055, 0.3, [0.01, -0.19, 0.03], [0, 0, 0.05]);
    b.blob('metal', [0.08, 0.08, 0.08], [0, 0, 0], [0, 0, 0], 0);
  } else {
    b.cap('skin', 0.1, 0.27, [0.01, -0.2, 0.03], [0, 0, 0.05]);
    b.blob('skin', [0.12, 0.12, 0.12], [0.01, -0.08, 0.03], [0, 0, 0.05], 0.012);
    b.cyl('leather', 0.105, 0.105, 0.08, [0.015, -0.3, 0.035], [0, 0, 0.05]);
  }
  return b;
}
const fist = (b, o = [0.02, -0.42, 0.06]) => b.blob('skin', [0.11, 0.1, 0.12], o, [0, 0, 0], 0.012).blob('skin', [0.035, 0.07, 0.035], [o[0] + 0.07, o[1] + 0.02, o[2] + 0.05], [0, 0, 0.4], 0);
const ARMS = [
  { id: 'a_fist', name: 'Poing Nu', tags: ['Classique'], build: w => [piece('shoulder_L', upperArm(new Builder(w))), piece('elbow_L', fist(foreArm(new Builder(w))))] },
  { id: 'a_klaw', name: 'Pince Énergétik', tags: ['Boss'], build: w => {
    const f = foreArm(new Builder(w));
    f.box('metal', [0.24, 0.2, 0.24], [0.02, -0.38, 0.05], [0, 0, 0], 0.04);
    f.cyl('metal', 0.03, 0.03, 0.22, [0.12, -0.25, 0.05], [0, 0, 0]);
    for (const [x, z, rz] of [[0.08, 0.1, 0.25], [-0.04, 0.1, -0.1], [0.02, -0.02, 0]]) f.cone('metal', 0.045, 0.28, [x, -0.6, z], [Math.PI, 0, rz]);
    f.rivets('metal', [[0.14, -0.38, 0.15], [-0.1, -0.38, 0.15]]);
    return [piece('shoulder_L', upperArm(new Builder(w))), piece('elbow_L', f)];
  } },
  { id: 'a_bionik', name: 'Bras Bionik', tags: ['Bionik', 'Kustom'], build: w => {
    const f = foreArm(new Builder(w), true);
    f.box('metal', [0.13, 0.12, 0.14], [0.02, -0.4, 0.06], [0, 0, 0], 0.025);
    for (let i = 0; i < 3; i++) f.cyl('metal', 0.018, 0.012, 0.12, [-0.03 + i * 0.03, -0.51, 0.1], [0.3, 0, 0]);
    return [piece('shoulder_L', upperArm(new Builder(w), true)), piece('elbow_L', f)];
  } },
  { id: 'a_brace', name: 'Bras Kogneur', tags: ['Classique', 'Kustom'], build: w => {
    const u = upperArm(new Builder(w));
    u.cyl('leather', 0.125, 0.125, 0.07, [0.05, -0.27, 0], [0, 0, 0.19]);
    const f = fist(foreArm(new Builder(w)));
    f.box('metal', [0.2, 0.22, 0.16], [0.015, -0.17, 0.05], [0, 0, 0.05], 0.03);
    f.cone('metal', 0.025, 0.09, [0.11, -0.17, 0.05], [0, 0, -H]);
    f.cone('metal', 0.025, 0.09, [0.015, -0.17, 0.15], [H, 0, 0]);
    return [piece('shoulder_L', u), piece('elbow_L', f)];
  } },
];

// ---------- Armes (main droite ; l'arme suit l'avant-bras) ----------
const WEAPONS = [
  { id: 'w_none', name: 'Aucune', tags: [], none: true, build: () => [] },
  { id: 'w_choppa', name: 'Kikoup Géant', tags: ['Classique'], build: w => {
    const b = new Builder(w);
    b.cyl('wood', 0.03, 0.03, 0.34, [0, -0.02, 0.06], [H, 0, 0]);
    b.box('metal', [0.025, 0.62, 0.3], [0, 0.28, 0.24], [-0.15, 0, 0], 0.008, 0.004);
    b.box('metal', [0.04, 0.12, 0.08], [0, 0.0, 0.22], [0, 0, 0], 0.01);
    b.cone('metal', 0.03, 0.1, [0, 0.62, 0.3], [-0.15, 0, 0]);
    b.rivets('metal', [[0.018, 0.1, 0.18], [0.018, 0.1, 0.3], [-0.018, 0.1, 0.24]], 0.016);
    return [piece('hand_R', b)];
  } },
  { id: 'w_plasma', name: 'Kanon Plasma', tags: ['Boss', 'Kustom'], build: w => {
    const b = new Builder(w);
    b.cyl('metal', 0.1, 0.11, 0.42, [0, -0.26, 0.09], [0, 0, 0]);
    b.cyl('metal', 0.05, 0.05, 0.28, [0, -0.58, 0.09], [0, 0, 0]);
    for (let i = 0; i < 3; i++) b.torus('metal', 0.075, 0.018, [0, -0.5 - i * 0.07, 0.09], [H, 0, 0]);
    b.box('metal', [0.12, 0.18, 0.1], [0.11, -0.2, 0.09], [0, 0, 0], 0.02);
    b.cyl('eye', 0.035, 0.035, 0.02, [0, -0.72, 0.09], [0, 0, 0]);
    b.cyl('wood', 0.028, 0.028, 0.16, [0, -0.02, 0.06], [H, 0, 0]);
    b.rivets('metal', [[0, -0.1, 0.2], [0, -0.3, 0.2]]);
    return [piece('hand_R', b)];
  } },
  { id: 'w_shoota', name: 'Fling', tags: ['Classique'], build: w => {
    const b = new Builder(w);
    b.box('metal', [0.1, 0.34, 0.15], [0, -0.18, 0.07], [0, 0, 0], 0.02);
    b.cyl('metal', 0.03, 0.03, 0.32, [0, -0.48, 0.1], [0, 0, 0]);
    b.cyl('metal', 0.03, 0.03, 0.26, [0, -0.45, 0.04], [0, 0, 0]);
    b.cyl('metal', 0.08, 0.08, 0.08, [0, -0.14, 0.17], [0, 0, H]);
    b.box('wood', [0.07, 0.18, 0.08], [0, 0.06, 0.02], [0.3, 0, 0], 0.02);
    return [piece('hand_R', b)];
  } },
];

// ---------- Jambes (bassin + gauche, miroir pour la droite) ----------
function legL(b, o = {}) { // cuisse sur hip_L
  b.cap('cloth', 0.135, 0.28, [0.015, -0.22, 0.02], [0, 0, -0.07], 0.01);
  if (o.plates) b.box('metal', [0.2, 0.22, 0.06], [0.03, -0.2, 0.15], [0.05, 0, -0.07], 0.02);
  return b;
}
function shinL(b, o = {}) { // tibia + botte sur knee_L
  b.cap('cloth', 0.1, 0.24, [0, -0.17, -0.02], [0, 0, 0], 0.008);
  b.box('leather', [0.21, 0.17, 0.34], [0, -0.42, 0.06], [0, 0, 0], 0.06, 0.006);
  b.box('leather', [0.23, 0.04, 0.36], [0, -0.5, 0.06], [0, 0, 0], 0.015);
  if (o.plates) { b.box('metal', [0.17, 0.26, 0.06], [0, -0.18, 0.1], [-0.05, 0, 0], 0.02); b.dome('metal', [0.09, 0.07, 0.09], [0, 0, 0.08], [H, 0, 0]); }
  if (o.bionik) { b.cyl('metal', 0.03, 0.03, 0.3, [0.11, -0.18, 0.0], [0, 0, 0]); b.cyl('metal', 0.045, 0.045, 0.12, [0.11, -0.08, 0.0], [0, 0, 0]); }
  return b;
}
function pelvisBase(b) {
  b.blob('cloth', [0.34, 0.2, 0.26], [0, -0.02, 0.02], [0, 0, 0], 0.012);
  b.box('leather', [0.7, 0.09, 0.52], [0, 0.07, 0.03], [0, 0, 0], 0.04);
  b.box('metal', [0.12, 0.1, 0.04], [0, 0.07, 0.29], [0, 0, 0], 0.015);
  b.box('cloth', [0.24, 0.3, 0.03], [0, -0.17, 0.25], [0.1, 0, 0], 0.012, 0.006);
  return b;
}
function legsSet(w, o) {
  const L = [piece('hip_L', legL(new Builder(w), o)), piece('knee_L', shinL(new Builder(w), o))];
  return [piece('pelvis', pelvisBase(new Builder(w))), ...L, ...mirrorPieces(L)];
}
const LEGS = [
  { id: 'l_march', name: 'Jambes Kampagne', tags: ['Classique'], build: w => legsSet(w, {}) },
  { id: 'l_mega', name: 'Jambes Méga', tags: ['Boss'], build: w => legsSet(w, { plates: true }) },
  { id: 'l_bionik', name: 'Pattes Bionik', tags: ['Bionik'], build: w => legsSet(w, { bionik: true }) },
];

// ---------- Dos ----------
const BACKS = [
  { id: 'b_none', name: 'Aucun', tags: [], none: true, build: () => [] },
  { id: 'b_reactor', name: 'Réacteur Plasma', tags: ['Kustom'], build: w => {
    const b = new Builder(w);
    b.box('metal', [0.42, 0.36, 0.22], [0, 0.17, -0.38], [0, 0, 0], 0.05);
    for (const x of [0.12, -0.12]) { b.cyl('metal', 0.05, 0.05, 0.36, [x, 0.48, -0.42], [0, 0, 0]); b.cyl('metal', 0.07, 0.06, 0.06, [x, 0.67, -0.42], [0, 0, 0]); }
    b.torus('metal', 0.08, 0.015, [0, 0.17, -0.5], [0, 0, 0]);
    b.rivets('metal', [[0.17, 0.3, -0.27], [-0.17, 0.3, -0.27], [0.17, 0.04, -0.27], [-0.17, 0.04, -0.27]]);
    return [piece('chest', b)];
  } },
  { id: 'b_banner', name: 'Bannière Klan', tags: ['Classique'], build: w => {
    const b = new Builder(w);
    b.box('leather', [0.2, 0.16, 0.1], [0.12, 0.15, -0.36], [0, 0, 0], 0.02);
    b.cyl('wood', 0.025, 0.025, 1.25, [0.12, 0.62, -0.38], [0, 0, 0]);
    b.cyl('wood', 0.02, 0.02, 0.5, [-0.12, 1.2, -0.38], [0, 0, H]);
    b.box('cloth', [0.46, 0.56, 0.02], [-0.12, 0.9, -0.38], [0, 0, 0], 0.008, 0.01);
    b.blob('bone', [0.07, 0.08, 0.07], [0.12, 1.3, -0.38], [0, 0, 0], 0.006);
    return [piece('chest', b)];
  } },
  { id: 'b_tanks', name: 'Bonbonnes', tags: ['Kustom'], build: w => {
    const b = new Builder(w);
    for (const x of [0.13, -0.13]) b.cap('metal', 0.09, 0.3, [x, 0.18, -0.38], [0, 0, 0], 0);
    b.box('leather', [0.5, 0.06, 0.06], [0, 0.28, -0.3], [0, 0, 0], 0.02);
    b.cyl('metal', 0.02, 0.02, 0.4, [0, 0.45, -0.38], [0, 0, H]);
    return [piece('chest', b)];
  } },
];

// ---------- Épaulières (gauche puis miroir) ----------
function padSet(w, fn) { const L = [piece('clav_L', fn(new Builder(w)))]; return [...L, ...mirrorPieces(L)]; }
const SHOULDERS = [
  { id: 's_none', name: 'Aucune', tags: [], none: true, build: () => [] },
  { id: 's_spikes', name: 'À pointes', tags: ['Boss'], build: w => padSet(w, b => {
    b.dome('metal', [0.2, 0.15, 0.21], [0.27, 0.05, 0], [0, 0, -0.4]);
    b.cone('metal', 0.035, 0.16, [0.25, 0.22, 0.06], [0, 0, -0.3]);
    b.cone('metal', 0.035, 0.16, [0.32, 0.18, -0.06], [0, 0, -0.6]);
    b.cone('metal', 0.035, 0.14, [0.2, 0.2, -0.08], [-0.2, 0, -0.1]);
    return b;
  }) },
  { id: 's_plates', name: 'Plaques', tags: ['Classique'], build: w => padSet(w, b => {
    b.box('metal', [0.32, 0.05, 0.32], [0.28, 0.1, 0], [0, 0, -0.45], 0.02);
    b.box('metal', [0.3, 0.05, 0.3], [0.33, 0.0, 0], [0, 0, -0.7], 0.02);
    b.rivets('metal', [[0.24, 0.14, 0.1], [0.24, 0.14, -0.1]]);
    return b;
  }) },
];

// ---------- Socles ----------
function seeded(seed) { let s = seed; return () => (s = (s * 16807) % 2147483647) / 2147483647; }
const BASES = [
  { id: 'base_32', name: 'Socle rond', tags: ['Classique'], build: () => [piece('root', new Builder().cyl('stone', 0.6, 0.63, 0.1, [0, -0.05, 0], [0, 0, 0], 64))] },
  { id: 'base_ruin', name: 'Socle Ruine', tags: ['Kustom'], build: () => {
    const b = new Builder().cyl('stone', 0.6, 0.63, 0.1, [0, -0.05, 0], [0, 0, 0], 64);
    const r = seeded(7);
    for (let i = 0; i < 14; i++) {
      const a = r() * Math.PI * 2, d = 0.28 + r() * 0.26, s = 0.04 + r() * 0.07;
      b.box('stone', [s * 1.4, s, s * 1.1], [Math.cos(a) * d, s * 0.4, Math.sin(a) * d], [r(), r() * 3, r()], 0.01, 0.004);
    }
    b.cyl('stone', 0.08, 0.09, 0.32, [-0.36, 0.16, -0.22], [0, 0, 0.12]);
    b.blob('bone', [0.06, 0.065, 0.06], [0.35, 0.05, 0.22], [0.4, 0.6, 0], 0.006);
    return [piece('root', b)];
  } },
  { id: 'base_40', name: 'Socle large', tags: ['Boss'], build: () => [piece('root', new Builder().cyl('stone', 0.74, 0.77, 0.1, [0, -0.05, 0], [0, 0, 0], 64))] },
];

// ---------- Kits (accessoires à cocher) ----------
export const KITS = [
  { id: 'k_rokkit1', name: 'Rokkit simple', build: w => {
    const b = new Builder(w);
    b.cyl('metal', 0.06, 0.06, 0.5, [0.3, 0.4, -0.42], [0, 0, 0]); b.cone('metal', 0.06, 0.16, [0.3, 0.73, -0.42], [0, 0, 0]);
    for (const a of [0, H, Math.PI, -H]) b.box('metal', [0.012, 0.12, 0.08], [0.3 + Math.sin(a) * 0.07, 0.17, -0.42 + Math.cos(a) * 0.07], [0, a, 0], 0.004);
    return [piece('chest', b)];
  } },
  { id: 'k_rokkit2', name: 'Rokkits doubles', build: w => {
    const b = new Builder(w);
    for (const x of [0.3, -0.3]) { b.cyl('metal', 0.055, 0.055, 0.46, [x, 0.4, -0.42], [0, 0, 0]); b.cone('metal', 0.055, 0.15, [x, 0.71, -0.42], [0, 0, 0]); }
    return [piece('chest', b)];
  } },
  { id: 'k_bomb', name: 'Bombe à pointes', build: w => {
    const b = new Builder(w);
    b.blob('metal', [0.08, 0.08, 0.08], [0.38, -0.02, 0.12], [0, 0, 0], 0);
    for (const d of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]]) {
      const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), new THREE.Vector3(...d));
      const e = new THREE.Euler().setFromQuaternion(q);
      b.cone('metal', 0.022, 0.07, [0.38 + d[0] * 0.1, -0.02 + d[1] * 0.1, 0.12 + d[2] * 0.1], [e.x, e.y, e.z]);
    }
    return [piece('pelvis', b)];
  } },
  { id: 'k_mine', name: 'Mine à pointes', build: w => {
    const b = new Builder(w);
    b.cyl('metal', 0.1, 0.1, 0.05, [-0.36, 0.0, -0.1], [0, 0, H]);
    for (let i = 0; i < 6; i++) { const a = i / 6 * Math.PI * 2; b.cone('metal', 0.018, 0.06, [-0.39, Math.sin(a) * 0.07, -0.1 + Math.cos(a) * 0.07], [0, 0, H]); }
    return [piece('pelvis', b)];
  } },
];

// ---------- Options d'équipement ----------
export const OPTIONS = {
  plates: { name: 'Plaques d’armure', build: w => {
    const f = new Builder(w).box('metal', [0.2, 0.2, 0.06], [0.04, -0.17, 0.11], [-0.1, 0, 0.05], 0.02);
    const t = new Builder(w).box('metal', [0.2, 0.2, 0.06], [0.03, -0.2, 0.16], [0.05, 0, -0.07], 0.02);
    const L = [piece('elbow_L', f), piece('hip_L', t)];
    return [...L, ...mirrorPieces(L)];
  } },
  pouches: { name: 'Sacoches & équipement', build: w => {
    const b = new Builder(w);
    b.box('leather', [0.12, 0.14, 0.08], [0.3, -0.02, 0.2], [0, -0.5, 0], 0.025);
    b.box('leather', [0.12, 0.14, 0.08], [-0.3, -0.02, 0.2], [0, 0.5, 0], 0.025);
    for (const x of [-0.12, -0.04, 0.04]) b.cyl('metal', 0.025, 0.025, 0.1, [x, -0.02, -0.29], [0, 0, 0]);
    return [piece('pelvis', b)];
  } },
  ornament: { name: 'Ornements', build: w => {
    const b = new Builder(w);
    b.blob('bone', [0.06, 0.07, 0.06], [0.16, -0.02, 0.29], [0, 0, 0], 0.005);
    for (let i = 0; i < 7; i++) { const a = -0.9 + i * 0.3; b.cone('bone', 0.014, 0.06, [Math.sin(a) * 0.2, 0.18 - Math.cos(a) * 0.08, 0.3], [Math.PI, 0, 0]); }
    const c = new Builder(w).torus('leather', 0.2, 0.008, [0, 0.21, 0.25], [1.2, 0, 0]);
    return [piece('pelvis', b), piece('chest', c)];
  } },
};

// ---------- Catalogue ----------
// Un emplacement = une catégorie de la bibliothèque, un os principal (sockets, import) et ses variantes.
export const SLOTS = [
  { id: 'torso', label: 'Corps', cat: 'bodies', bone: 'chest', variants: TORSOS },
  { id: 'head', label: 'Tête', cat: 'heads', bone: 'head', variants: HEADS },
  { id: 'armL', label: 'Bras G', cat: 'arms', bone: 'shoulder_L', variants: ARMS },
  { id: 'armR', label: 'Bras D', cat: 'arms', bone: 'shoulder_R', variants: ARMS, mirror: true },
  // mounts : sockets de réception possibles (glisser une arme vers l'un d'eux). Les pièces construites sur l'os de
  // l'emplacement passent sur l'os du socket, avec son décalage ; mirror retourne la pièce pour le côté gauche.
  { id: 'weapon', label: 'Arme', cat: 'weapons', bone: 'hand_R', variants: WEAPONS, mounts: [
    { id: 'hand_R', label: 'Main D', bone: 'hand_R' },
    { id: 'hand_L', label: 'Main G', bone: 'hand_L', mirror: true },
    { id: 'belt_R', label: 'Ceinture D', bone: 'pelvis', pos: [-0.36, 0.0, 0.1], rot: [0.25, 0, 0.18] },
    { id: 'belt_L', label: 'Ceinture G', bone: 'pelvis', pos: [0.36, 0.0, 0.1], rot: [0.25, 0, -0.18], mirror: true },
    { id: 'back', label: 'Dos', bone: 'chest', pos: [0.02, 0.12, -0.46], rot: [0, 0, 2.55] },
  ] },
  { id: 'legs', label: 'Jambes', cat: 'legs', bone: 'pelvis', variants: LEGS },
  { id: 'back', label: 'Sac à dos', cat: 'backpacks', bone: 'chest', socket: [0, 0.2, -0.4], variants: BACKS },
  { id: 'shoulders', label: 'Épaulières', cat: 'shoulders', bone: 'clav_L', variants: SHOULDERS },
  { id: 'base', label: 'Socle', cat: 'bases', bone: 'root', variants: BASES },
];

export const CATEGORIES = [
  { id: 'weapons', label: 'Armes', icon: 'M3 13h12l3-3h3v4h-3l-2 2H8l-2 4H3z' },
  { id: 'heads', label: 'Têtes', icon: 'M12 3a8 8 0 0 0-8 8v3l2 2v4h12v-4l2-2v-3a8 8 0 0 0-8-8zM9 12h.01M15 12h.01M10 20v-3M14 20v-3' },
  { id: 'bodies', label: 'Corps', icon: 'M7 4h10l3 5-2 2v9H6v-9L4 9zM9 13h6' },
  { id: 'arms', label: 'Bras', icon: 'M17 3l-5 5-3 7 2 5 4-1-1-5 5-6z' },
  { id: 'legs', label: 'Jambes', icon: 'M8 3h8l-1 9 1 9h-3l-1-7-1 7H8l1-9z' },
  { id: 'shoulders', label: 'Épaulières', icon: 'M4 14c0-5 4-8 8-8s8 3 8 8l-3 2-2-3-3 3-3-3-2 3z' },
  { id: 'backpacks', label: 'Sacs à dos', icon: 'M8 3h8v4h2v14H6V7h2zM10 3v4M14 3v4M9 12h6' },
  { id: 'bases', label: 'Socles', icon: 'M3 13c0-2 4-3 9-3s9 1 9 3-4 3-9 3-9-1-9-3zM3 13v4c0 2 4 3 9 3s9-1 9-3v-4' },
  { id: 'presets', label: 'Presets', icon: 'M6 4v16M18 4v16M6 8h12M6 16h12M12 4v16' },
  { id: 'decals', label: 'Décalques', icon: 'M4 4h16v16H4zM8 8h8v8H8z', soon: true },
];

export const PRESETS = [
  { id: 'p_boy', name: 'Boy de base', sel: { torso: 't_boy', head: 'h_scream', armL: 'a_fist', armR: 'a_fist', weapon: 'w_shoota', legs: 'l_march', back: 'b_none', shoulders: 's_none', base: 'base_32' }, kits: [], opts: { pouches: true } },
  { id: 'p_nob', name: 'Nob', sel: { torso: 't_rust', head: 'h_helm', armL: 'a_brace', armR: 'a_fist', weapon: 'w_choppa', legs: 'l_march', back: 'b_banner', shoulders: 's_plates', base: 'base_32' }, kits: [], opts: { ornament: true } },
  { id: 'p_boss', name: 'Boss Méga', sel: { torso: 't_mega', head: 'h_boss', armL: 'a_klaw', armR: 'a_fist', weapon: 'w_plasma', legs: 'l_mega', back: 'b_reactor', shoulders: 's_spikes', base: 'base_ruin' }, kits: ['k_rokkit2'], opts: { plates: true, ornament: true } },
  { id: 'p_mek', name: 'Mékano', sel: { torso: 't_rust', head: 'h_bionik', armL: 'a_bionik', armR: 'a_bionik', weapon: 'w_plasma', legs: 'l_bionik', back: 'b_tanks', shoulders: 's_none', base: 'base_ruin' }, kits: ['k_bomb'], opts: { pouches: true } },
];

export function mountOf(slot, id) { return slot.mounts ? slot.mounts.find(m => m.id === id) || slot.mounts[0] : null; }

const cache = new Map();
// Pièces d'une variante pour un emplacement (miroir pour le bras droit), mises en cache.
export function buildVariant(slot, variant, wear = 0) {
  const key = slot.id + ':' + variant.id + ':' + wear.toFixed(2);
  if (cache.has(key)) return cache.get(key);
  let pieces = variant.build(wear, slot);
  if (slot.mirror && !variant.imported) pieces = mirrorPieces(pieces);
  cache.set(key, pieces);
  return pieces;
}
export function buildKit(kit, wear = 0) {
  const key = 'kit:' + kit.id + ':' + wear.toFixed(2);
  if (!cache.has(key)) cache.set(key, kit.build(wear));
  return cache.get(key);
}
export function buildOption(id, wear = 0) {
  const key = 'opt:' + id + ':' + wear.toFixed(2);
  if (!cache.has(key)) cache.set(key, OPTIONS[id].build(wear));
  return cache.get(key);
}

// Taille cible (hauteur, unités du rig) d'un STL importé selon la catégorie.
export const IMPORT_SIZE = { heads: 0.45, bodies: 0.75, arms: 0.85, legs: 1.05, weapons: 0.75, shoulders: 0.3, backpacks: 0.6, bases: 0.12 };
