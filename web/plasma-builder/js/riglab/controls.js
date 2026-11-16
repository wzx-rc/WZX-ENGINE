// Contrôleurs de pose, comme un rig Maya : cercles FK autour des os (tourner), boîtes IK aux mains et rectangles IK
// sous les pieds (déplacer, bras / jambe suivent), carré du centre de gravité au bassin (déplacer, pieds ancrés).
// Couleurs Maya : côté gauche de l'ork bleu, droit rouge, centre jaune ; le contrôleur choisi est blanc.
// Les formes sont calculées une fois au repos, à la taille du maillage autour de chaque os (low), dans la scène
// overlay du viewport : elles ne vont ni dans les vignettes, ni dans le rendu HQ / path tracing, ni dans l'export.
import * as THREE from 'three';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';
import { CHAINS, FINGERS, GROUPS } from './model.js';

const Q = n => 'QuickRigCharacter_' + n;
const COLORS = { L: 0x4c9be8, R: 0xe8524f, C: 0xf0c94a };

// Côté d'un os : L (gauche de l'ork, +x), R, ou C (centre).
function sideOf(name) {
  const g = GROUPS.find(g => g.bones.includes(name));
  return g && /L$/.test(g.id) ? 'L' : g && /R$/.test(g.id) ? 'R' : 'C';
}

// Cercle de n segments, centre c, normale n, rayon r (paires de points pour LineSegments).
function circle(c, n, r, seg = 40) {
  const u = new THREE.Vector3(), v = new THREE.Vector3();
  const ref = Math.abs(n.y) < 0.9 ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(1, 0, 0);
  u.crossVectors(n, ref).normalize(); v.crossVectors(n, u).normalize();
  const pts = [];
  for (let i = 0; i < seg; i++) {
    for (const k of [i, i + 1]) {
      const a = k / seg * Math.PI * 2;
      pts.push(c.clone().addScaledVector(u, Math.cos(a) * r).addScaledVector(v, Math.sin(a) * r));
    }
  }
  return pts;
}
// Rectangle horizontal (xz) de min à max, à la hauteur y.
function rect(minX, maxX, minZ, maxZ, y) {
  const c = [[minX, minZ], [maxX, minZ], [maxX, maxZ], [minX, maxZ]].map(([x, z]) => new THREE.Vector3(x, y, z));
  const pts = [];
  for (let i = 0; i < 4; i++) pts.push(c[i], c[(i + 1) % 4]);
  // petite flèche vers l'avant (+z) au milieu du bord avant
  const mx = (minX + maxX) / 2, w = (maxX - minX) * 0.18;
  pts.push(new THREE.Vector3(mx - w, y, maxZ), new THREE.Vector3(mx, y, maxZ + w * 1.2), new THREE.Vector3(mx, y, maxZ + w * 1.2), new THREE.Vector3(mx + w, y, maxZ));
  return pts;
}
// Boîte orientée (centre, axes unitaires, demi-tailles).
function box(c, ax, h) {
  const corner = (sx, sy, sz) => c.clone().addScaledVector(ax[0], sx * h[0]).addScaledVector(ax[1], sy * h[1]).addScaledVector(ax[2], sz * h[2]);
  const pts = [];
  const s = [-1, 1];
  for (const a of s) for (const b of s) {
    pts.push(corner(-1, a, b), corner(1, a, b));
    pts.push(corner(a, -1, b), corner(a, 1, b));
    pts.push(corner(a, b, -1), corner(a, b, 1));
  }
  return pts;
}

// model : OrkModel (au repos pendant la construction). vp : Viewport (scène overlay, canvas, caméra).
export function createControls(model, vp) {
  const keep = model.getPose();
  model.setPose(null);
  model.root.updateMatrixWorld(true);

  // positions monde au repos des sommets du low, et os dominant de chaque sommet
  const low = model.meshes.BODY_low, lg = low.geometry, n = lg.attributes.position.count;
  const P = new Float32Array(n * 3), dom = new Int32Array(n);
  const v = new THREE.Vector3();
  for (let i = 0; i < n; i++) {
    low.getVertexPosition(i, v); v.applyMatrix4(low.matrixWorld);
    P[i * 3] = v.x; P[i * 3 + 1] = v.y; P[i * 3 + 2] = v.z;
    let best = 0, bw = -1;
    for (let j = 0; j < 4; j++) { const w = lg.attributes.skinWeight.getComponent(i, j); if (w > bw) { bw = w; best = lg.attributes.skinIndex.getComponent(i, j); } }
    dom[i] = best;
  }
  const wp = b => b.getWorldPosition(new THREE.Vector3());
  // os enfant qui prolonge l'os : le plus centré quand il y en a plusieurs (Spine2 -> Neck, pas les clavicules)
  const childBone = b => {
    const kids = b.children.filter(c => c.isBone);
    if (kids.length < 2) return kids[0];
    const x = wp(b).x;
    return kids.reduce((best, c) => (Math.abs(wp(c).x - x) < Math.abs(wp(best).x - x) ? c : best));
  };
  const axisOf = b => {
    const c = childBone(b);
    if (c) return wp(c).sub(wp(b)).normalize();
    return b.parent?.isBone ? wp(b).sub(wp(b.parent)).normalize() : new THREE.Vector3(0, 1, 0);
  };
  // rayon : distance des sommets de l'os à son axe (70e centile), un peu au-dessus de la peau
  function radius(b, fallback, bones = [b], ax = axisOf(b)) {
    const ids = new Set(bones.map(x => model.bones.indexOf(x)));
    const o = wp(b), d = [];
    for (let i = 0; i < n; i++) {
      if (!ids.has(dom[i])) continue;
      v.set(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]).sub(o);
      d.push(v.addScaledVector(ax, -v.dot(ax)).length());
    }
    if (d.length < 8) return fallback;
    d.sort((a, b2) => a - b2);
    return d[Math.floor(d.length * 0.7)] * 1.15;
  }
  const len = b => { const c = childBone(b); return c ? wp(c).distanceTo(wp(b)) : 0.1; };

  const ctrls = [];
  // shape : points monde au repos. mode : 'fk' (tourner) ou 'ik' (déplacer).
  function add(bone, mode, pts, color) {
    const geo = new LineSegmentsGeometry().setPositions(pts.flatMap(p => [p.x, p.y, p.z]));
    const mat = new LineMaterial({ color, linewidth: 2.2, transparent: true, opacity: 0.95, depthTest: false });
    const line = new LineSegments2(geo, mat);
    line.renderOrder = 11;
    line.matrixAutoUpdate = false;
    line.userData.helper = true;
    const inv = bone.matrixWorld.clone().invert();
    ctrls.push({ bone, mode, line, color, pts, inv, base: color });
  }
  const B = name => model.bone(name);
  const hips = B(Q('Hips'));
  const hipR = radius(hips, 0.25);

  // centre de gravité : carré horizontal au bassin (IK = déplacer le bassin), cercle du bassin (FK)
  const hp = wp(hips);
  add(hips, 'ik', rect(hp.x - hipR * 1.45, hp.x + hipR * 1.45, hp.z - hipR * 1.45, hp.z + hipR * 1.45, hp.y), COLORS.C);
  add(hips, 'fk', circle(hp, new THREE.Vector3(0, 1, 0), hipR * 1.12), COLORS.C);
  // dos, cou, tête
  // dos et cou : cercles horizontaux, comme les contrôleurs de torse d'un rig Maya (l'ork est voûté : l'axe des os
  // du haut du dos part vers l'avant)
  const UP = new THREE.Vector3(0, 1, 0);
  for (const nm of ['Spine', 'Spine1', 'Spine2']) { const b = B(Q(nm)); add(b, 'fk', circle(wp(b), UP, radius(b, hipR, [b], UP)), COLORS.C); }
  const neck = B(Q('Neck')), head = B(Q('Head'));
  add(neck, 'fk', circle(wp(neck), UP, Math.min(radius(neck, hipR * 0.5, [neck], UP), hipR * 0.75)), COLORS.C);
  {
    // la tête est un maillage à part (rigide) : rayon tiré du HEAD_low
    const hm = model.meshes.HEAD_low, hgp = hm?.geometry.attributes.position;
    let r = hipR * 0.8, cy = wp(head).y;
    if (hgp) {
      const box3 = new THREE.Box3();
      for (let i = 0; i < hgp.count; i++) { hm.getVertexPosition(i, v); box3.expandByPoint(v.applyMatrix4(hm.matrixWorld)); }
      const s = box3.getSize(new THREE.Vector3());
      r = Math.max(s.x, s.z) * 0.62; cy = box3.min.y + s.y * 0.45;
    }
    const hc = wp(head); hc.y = cy;
    add(head, 'fk', circle(hc, new THREE.Vector3(0, 1, 0), r), COLORS.C);
  }
  // bras et jambes, par côté
  for (const side of ['L', 'R']) {
    const col = COLORS[side];
    const [arm, fore, hand] = CHAINS[side === 'L' ? 'handL' : 'handR'].map(B);
    const clav = B(side === 'L' ? 'joint40' : 'joint41');
    add(clav, 'fk', circle(wp(clav).addScaledVector(axisOf(clav), len(clav) * 0.55), axisOf(clav), len(clav) * 0.42), col);
    for (const b of [arm, fore]) add(b, 'fk', circle(wp(b).addScaledVector(axisOf(b), len(b) * 0.3), axisOf(b), radius(b, hipR * 0.4)), col);
    // poignet (FK) et boîte IK de la main, entre le poignet et la base du majeur
    const fingers = FINGERS[side].flat().map(B);
    const handR = radius(hand, hipR * 0.4, [hand, ...fingers]);
    const hAx = axisOf(fore);
    const mid = wp(B(FINGERS[side][2][0]));
    add(hand, 'fk', circle(wp(hand), hAx, Math.min(radius(hand, handR * 0.6), mid.distanceTo(wp(hand)) * 0.7)), col);
    const hc = wp(hand).lerp(mid, 0.55);
    const ax0 = mid.clone().sub(wp(hand)).normalize();
    const ax1 = wp(B(FINGERS[side][1][0])).sub(wp(B(FINGERS[side][4][0]))).normalize();
    ax1.addScaledVector(ax0, -ax1.dot(ax0)).normalize();
    const ax2 = new THREE.Vector3().crossVectors(ax0, ax1);
    const hl = mid.distanceTo(wp(hand));
    add(hand, 'ik', box(hc, [ax0, ax1, ax2], [hl * 0.85, hl * 0.75, hl * 0.55]), col);
    // base des doigts : petits cercles
    for (const chain of FINGERS[side]) { const f = B(chain[0]); add(f, 'fk', circle(wp(f).addScaledVector(axisOf(f), len(f) * 0.35), axisOf(f), Math.max(len(f) * 0.32, handR * 0.12), 20), col); }
    // jambe : cuisse, genou (FK), rectangle IK sous le pied
    const [up, leg, foot] = CHAINS[side === 'L' ? 'footL' : 'footR'].map(B);
    for (const b of [up, leg]) add(b, 'fk', circle(wp(b).addScaledVector(axisOf(b), len(b) * 0.3), axisOf(b), radius(b, hipR * 0.5)), col);
    const toe = B(Q(side === 'L' ? 'LeftToeBase' : 'LeftToeBase1'));
    add(foot, 'fk', circle(wp(foot), axisOf(leg), radius(leg, hipR * 0.4) * 0.8), col);
    const ids = new Set([foot, toe].map(b => model.bones.indexOf(b)));
    const fb = new THREE.Box3();
    for (let i = 0; i < n; i++) if (ids.has(dom[i])) fb.expandByPoint(v.set(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]));
    if (!fb.isEmpty()) {
      const pad = (fb.max.x - fb.min.x) * 0.12;
      add(foot, 'ik', rect(fb.min.x - pad, fb.max.x + pad, fb.min.z - pad, fb.max.z + pad, fb.min.y + pad * 0.3), col);
    }
  }

  const group = new THREE.Group();
  group.visible = false;
  for (const c of ctrls) group.add(c.line);
  vp.overlay.add(group);
  model.setPose(keep);
  model.root.updateMatrixWorld(true);

  // ------------------------------------------------------------ suivi des os, couleurs
  const res = new THREE.Vector2();
  function update(sel, tool, hover) {
    if (!group.visible) return;
    vp.renderer.getSize(res);
    for (const c of ctrls) {
      c.line.matrix.multiplyMatrices(c.bone.matrixWorld, c.inv);
      c.line.matrixWorldNeedsUpdate = true;
      const on = c.bone === sel && (c.mode === tool || !ctrls.some(x => x.bone === sel && x.mode === tool));
      c.line.material.color.set(on ? 0xffffff : c.base);
      c.line.material.linewidth = on ? 3.6 : c === hover ? 3 : 2.2;
      c.line.material.opacity = on || c === hover ? 1 : 0.85;
      c.line.material.resolution.copy(res);
    }
  }

  // ------------------------------------------------------------ picking à l'écran (distance du pointeur aux traits)
  const a = new THREE.Vector3(), b2 = new THREE.Vector3(), m = new THREE.Matrix4();
  function pick(ev, px = 9) {
    if (!group.visible) return null;
    const r = vp.canvas.getBoundingClientRect();
    const mx = ev.clientX - r.left, my = ev.clientY - r.top;
    const toScreen = (p, out) => { out.copy(p).applyMatrix4(m).project(vp.camera); out.set((out.x + 1) / 2 * r.width, (1 - out.y) / 2 * r.height, out.z); return out; };
    let best = null, bd = px * px;
    for (const c of ctrls) {
      m.multiplyMatrices(c.bone.matrixWorld, c.inv);
      for (let i = 0; i < c.pts.length; i += 2) {
        toScreen(c.pts[i], a); toScreen(c.pts[i + 1], b2);
        if (a.z > 1 || b2.z > 1) continue;
        const dx = b2.x - a.x, dy = b2.y - a.y, l2 = dx * dx + dy * dy;
        const t = l2 ? Math.max(0, Math.min(1, ((mx - a.x) * dx + (my - a.y) * dy) / l2)) : 0;
        const ex = a.x + dx * t - mx, ey = a.y + dy * t - my, d = ex * ex + ey * ey;
        // à distance égale, les petits contrôleurs (doigts, poignet) passent devant les grands
        if (d < bd - 0.5) { bd = d; best = c; }
      }
    }
    return best;
  }

  return {
    ctrls, group, update, pick,
    set visible(on) { group.visible = on; },
    get visible() { return group.visible; },
  };
}
