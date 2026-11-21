// Contrôleurs de pose dessinés comme dans RIGGO, réduits à l'essentiel :
//  - centre de gravité (anneau jaune à 4 flèches au bassin) : déplacer le bassin, pieds ancrés ;
//  - tête (anneau jaune) : tourner ;
//  - mains (boîtes IK) et pieds (semelles IK) : déplacer, bras / jambe suivent ;
//  - correcteurs (losanges devant coudes et genoux, pole vectors) : orienter le coude / genou.
// Couleurs RIGGO : gauche de l'ork bleu, droit rouge, centre jaune ; survol / sélection éclaircis vers le blanc.
// Les formes sont calculées une fois au repos à la taille du maillage low, dans la scène overlay du viewport :
// elles ne vont ni dans les vignettes, ni dans le rendu HQ / path tracing, ni dans l'export.
import * as THREE from 'three';
import { CHAINS, FINGERS } from './model.js';

import { Q } from './rigdef.js';
const COLORS = { L: 0x46a6ff, R: 0xff5b6b, C: 0xffc23d };
const AX_Z = new THREE.Vector3(0, 0, 1);
const UP = new THREE.Vector3(0, 1, 0);
const WHITE = new THREE.Color(0xffffff);

function mat(color, opacity, Line = false) {
  const o = { color, transparent: true, opacity, depthTest: false, depthWrite: false };
  return Line ? new THREE.LineBasicMaterial(o) : new THREE.MeshBasicMaterial({ ...o, side: THREE.DoubleSide });
}
const PICK = new THREE.MeshBasicMaterial({ visible: false, side: THREE.DoubleSide });

// model : OrkModel (au repos pendant la construction). vp : Viewport (scène overlay, canvas, caméra).
export function createControls(model, vp) {
  const keep = model.getPose();
  model.setPose(null);
  model.root.updateMatrixWorld(true);

  const low = model.meshes.BODY_low, lg = low.geometry, n = lg.attributes.position.count;
  const P = new Float32Array(n * 3), dom = new Int32Array(n);
  const v = new THREE.Vector3();
  const box = new THREE.Box3();
  for (let i = 0; i < n; i++) {
    low.getVertexPosition(i, v); v.applyMatrix4(low.matrixWorld);
    box.expandByPoint(v);
    P[i * 3] = v.x; P[i * 3 + 1] = v.y; P[i * 3 + 2] = v.z;
    let best = 0, bw = -1;
    for (let j = 0; j < 4; j++) { const w = lg.attributes.skinWeight.getComponent(i, j); if (w > bw) { bw = w; best = lg.attributes.skinIndex.getComponent(i, j); } }
    dom[i] = best;
  }
  const H = box.max.y - box.min.y, u = H / 100, floorY = box.min.y;
  const wp = b => b.getWorldPosition(new THREE.Vector3());
  // rayon : distance des sommets des os à un axe passant par o (70e centile), un peu au-dessus de la peau
  function radius(o, bones, ax, fallback) {
    const ids = new Set(bones.map(x => model.bones.indexOf(x)));
    const d = [];
    for (let i = 0; i < n; i++) {
      if (!ids.has(dom[i])) continue;
      v.set(P[i * 3], P[i * 3 + 1], P[i * 3 + 2]).sub(o);
      d.push(v.addScaledVector(ax, -v.dot(ax)).length());
    }
    if (d.length < 8) return fallback;
    d.sort((a, b) => a - b);
    return Math.min(0.32 * H, d[Math.floor(d.length * 0.7)] * 1.18 + 1.2 * u);
  }

  const ctrls = [];
  // obj : groupe dessiné, construit en espace monde au repos ; il suit l'os par bone.matrixWorld * inv(repos).
  // parts : [{ m: matériau, op: opacité, fill: bool }] pour le survol ; picks : maillages invisibles pour le clic.
  function add(bone, mode, kind, obj, parts, picks, color, extra = {}) {
    obj.matrixAutoUpdate = false;
    obj.traverse(o => { o.userData.helper = true; o.renderOrder = 30; o.frustumCulled = false; });
    const anchor = new THREE.Vector3().setFromMatrixPosition(obj.matrix);
    const c = { bone, mode, kind, obj, parts, picks, color: new THREE.Color(color), inv: bone.matrixWorld.clone().invert(), rest: obj.matrix.clone(), pts: [anchor], ...extra };
    for (const p of picks) p.userData.ctrl = c;
    ctrls.push(c);
    return c;
  }
  const place = (obj, pos, quat) => { obj.matrix.compose(pos, quat || new THREE.Quaternion(), new THREE.Vector3(1, 1, 1)); return obj; };

  // anneau (normale = axe) avec flèches facultatives
  function ring(bone, mode, pos, axis, r, color, arrows = false) {
    const g = new THREE.Group();
    const mRing = mat(color, 0.95), mFill = mat(color, 0);
    g.add(new THREE.Mesh(new THREE.TorusGeometry(r, 0.32 * u, 6, 56), mRing));
    g.add(new THREE.Mesh(new THREE.CircleGeometry(r, 40), mFill));
    const parts = [{ m: mRing, op: 0.95 }, { m: mFill, op: 0, fill: true }];
    if (arrows) {
      const mA = mat(color, 0.95);
      const cone = new THREE.ConeGeometry(1.1 * u, 2.4 * u, 12);
      for (let k = 0; k < 4; k++) {
        const a = k * Math.PI / 2, m = new THREE.Mesh(cone, mA);
        m.position.set(Math.cos(a) * (r + 2 * u), Math.sin(a) * (r + 2 * u), 0);
        m.rotation.z = a - Math.PI / 2;
        g.add(m);
      }
      parts.push({ m: mA, op: 0.95 });
    }
    const pk = new THREE.Mesh(new THREE.TorusGeometry(r, 1.5 * u, 6, 40), PICK);
    g.add(pk);
    place(g, pos, new THREE.Quaternion().setFromUnitVectors(AX_Z, axis.clone().normalize()));
    return add(bone, mode, 'ring', g, parts, [pk], color);
  }

  // ---------------------------------------------------------------- centre de gravité (bassin)
  const B = name => model.bone(name);
  const hips = B(Q('Hips'));
  const hp = wp(hips);
  const rHips = radius(hp, [hips, B(Q('Spine'))], UP, 0.12 * H);
  ring(hips, 'ik', hp, UP, rHips * 1.45, COLORS.C, true);

  // ---------------------------------------------------------------- tête : anneau horizontal autour du crâne
  {
    const head = B(Q('Head'));
    const hm = model.meshes.HEAD_low, hgp = hm?.geometry.attributes.position;
    let r = 0.1 * H;
    const hc = wp(head); hc.y += 0.035 * H;
    if (hgp) {
      const b3 = new THREE.Box3();
      for (let i = 0; i < hgp.count; i++) { hm.getVertexPosition(i, v); b3.expandByPoint(v.applyMatrix4(hm.matrixWorld)); }
      const s = b3.getSize(new THREE.Vector3()), c = b3.getCenter(new THREE.Vector3());
      r = Math.max(s.x, s.z) * 0.6 + 1.2 * u;
      hc.set(c.x, b3.min.y + s.y * 0.45, c.z);
    }
    ring(head, 'fk', hc, UP, r, COLORS.C);
  }

  for (const side of ['L', 'R']) {
    const col = COLORS[side];
    // ---------------------------------------------------------------- main : boîte IK le long de la main
    {
      const chain = side === 'L' ? 'handL' : 'handR';
      const hand = B(CHAINS[chain][2]);
      const w = wp(hand), mid = wp(B(FINGERS[side][2][0]));
      const len = mid.distanceTo(w);
      const d = mid.clone().sub(w).normalize();
      const across = wp(B(FINGERS[side][1][0])).sub(wp(B(FINGERS[side][4][0])));
      across.addScaledVector(d, -across.dot(d)).normalize();
      const third = new THREE.Vector3().crossVectors(d, across);
      const fingers = FINGERS[side].flat().map(B);
      const rh = Math.min(radius(w.clone().addScaledVector(d, len * 0.45), [hand, ...fingers.filter((_, i) => i % 3 === 0)], d, len * 0.5), len * 0.55);
      const geo = new THREE.BoxGeometry(Math.max(len * 1.15, 4 * u), rh * 2.1, rh * 2.1);
      const mFill = mat(col, 0.14), mLine = mat(col, 1, true);
      const g = new THREE.Group();
      const fill = new THREE.Mesh(geo, mFill);
      g.add(fill, new THREE.LineSegments(new THREE.EdgesGeometry(geo), mLine));
      const q = new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(d, across, third));
      place(g, w.clone().addScaledVector(d, len * 0.45), q);
      add(hand, 'ik', 'box', g, [{ m: mFill, op: 0.14, fill: true }, { m: mLine, op: 1 }], [fill], col);
    }
    // ---------------------------------------------------------------- pied : semelle IK arrondie au sol
    {
      const chain = side === 'L' ? 'footL' : 'footR';
      const foot = B(CHAINS[chain][2]);
      const toe = B(Q(side === 'L' ? 'LeftToeBase' : 'LeftToeBase1'));
      const a = wp(foot), t = toe ? wp(toe) : a.clone().add(new THREE.Vector3(0, 0, 0.05 * H));
      const fwd = t.clone().sub(a); fwd.y = 0;
      const dist = Math.max(fwd.length(), 0.015 * H);
      fwd.normalize();
      const len = Math.max(2.05 * dist, 0.03 * H), wid = Math.max(len * 0.5, 0.05 * H);
      const cr = Math.min(wid, len) * 0.45;
      const shape = new THREE.Shape();
      const x0 = -wid / 2, x1 = wid / 2, y0 = -len / 2, y1 = len / 2;
      shape.moveTo(x0 + cr, y0); shape.lineTo(x1 - cr, y0); shape.quadraticCurveTo(x1, y0, x1, y0 + cr);
      shape.lineTo(x1, y1 - cr); shape.quadraticCurveTo(x1, y1, x1 - cr, y1);
      shape.lineTo(x0 + cr, y1); shape.quadraticCurveTo(x0, y1, x0, y1 - cr);
      shape.lineTo(x0, y0 + cr); shape.quadraticCurveTo(x0, y0, x0 + cr, y0);
      const geo = new THREE.ShapeGeometry(shape, 8).rotateX(-Math.PI / 2);
      const outline = new THREE.BufferGeometry().setFromPoints(shape.getPoints(8).map(p => new THREE.Vector3(p.x, 0, -p.y)));
      const mFill = mat(col, 0.16), mLine = mat(col, 1, true);
      const g = new THREE.Group();
      const fill = new THREE.Mesh(geo, mFill);
      g.add(fill, new THREE.LineLoop(outline, mLine));
      const c = a.clone().addScaledVector(fwd, 0.62 * dist); c.y = floorY + 0.3 * u;
      place(g, c, new THREE.Quaternion().setFromAxisAngle(UP, Math.atan2(fwd.x, fwd.z)));
      add(foot, 'ik', 'plate', g, [{ m: mFill, op: 0.16, fill: true }, { m: mLine, op: 1 }], [fill], col, { restY: a.y });
    }
    // ---------------------------------------------------------------- correcteurs : losanges des coudes / genoux
    for (const chain of side === 'L' ? ['handL', 'footL'] : ['handR', 'footR']) {
      const midBone = B(CHAINS[chain][1]);
      const mD = mat(col, 0.9), mL = mat(col, 0.45, true);
      const g = new THREE.Group();
      const dia = new THREE.Mesh(new THREE.OctahedronGeometry(1.25 * u), mD);
      const pk = new THREE.Mesh(new THREE.SphereGeometry(2.6 * u, 10, 8), PICK);
      g.add(dia, pk);
      // trait du coude / genou au losange (mis à jour à chaque image)
      const lineGeo = new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(new Float32Array(6), 3));
      const line = new THREE.Line(lineGeo, mL);
      line.matrixAutoUpdate = false;
      line.userData.helper = true; line.renderOrder = 29; line.frustumCulled = false;
      place(g, model.ork.localToWorld(model.polePos(chain)));
      add(midBone, 'ik', 'pole', g, [{ m: mD, op: 0.9 }, { m: mL, op: 0.45 }], [pk], col, { chain, line });
    }
  }

  const group = new THREE.Group();
  group.visible = false;
  for (const c of ctrls) { group.add(c.obj); if (c.line) group.add(c.line); }
  vp.overlay.add(group);
  model.setPose(keep);
  model.root.updateMatrixWorld(true);

  // ---------------------------------------------------------------- suivi des os, couleurs
  const _p = new THREE.Vector3(), _q = new THREE.Vector3();
  function follow(c) {
    if (c.kind === 'pole') {
      model.ork.localToWorld(model.polePos(c.chain, _p));
      c.obj.matrix.makeTranslation(_p.x, _p.y, _p.z);
      c.bone.getWorldPosition(_q);
      const pos = c.line.geometry.attributes.position;
      pos.setXYZ(0, _q.x, _q.y, _q.z); pos.setXYZ(1, _p.x, _p.y, _p.z); pos.needsUpdate = true;
    } else {
      c.obj.matrix.multiplyMatrices(c.bone.matrixWorld, c.inv).multiply(c.rest);
    }
    c.obj.matrixWorldNeedsUpdate = true;
    c.obj.updateMatrixWorld(true);
    c.pts[0].setFromMatrixPosition(c.obj.matrixWorld);
  }
  // l'anneau du bassin reste horizontal et les semelles restent au sol (comme RIGGO) : seule leur position suit
  const flat = c => c.kind === 'plate' || (c.kind === 'ring' && c.bone === hips);
  const _m = new THREE.Matrix4(), _r = new THREE.Quaternion(), _s = new THREE.Vector3(), _t = new THREE.Vector3();
  function followFlat(c) {
    _m.multiplyMatrices(c.bone.matrixWorld, c.inv).multiply(c.rest);
    _m.decompose(_t, _r, _s);
    const rq = new THREE.Quaternion(), rp = new THREE.Vector3(), rs = new THREE.Vector3();
    c.rest.decompose(rp, rq, rs);
    // lacet seul pour la semelle : direction avant de l'os projetée au sol
    if (c.kind === 'plate') {
      const f = new THREE.Vector3(0, 0, 1).applyQuaternion(_r); f.y = 0;
      const f0 = new THREE.Vector3(0, 0, 1).applyQuaternion(rq); f0.y = 0;
      if (f.lengthSq() > 1e-6 && f0.lengthSq() > 1e-6) rq.premultiply(new THREE.Quaternion().setFromUnitVectors(f0.normalize(), f.normalize()));
      _t.y = rp.y + Math.max(0, c.bone.getWorldPosition(_p).y - c.restY);
    }
    c.obj.matrix.compose(_t, rq, rs);
    c.obj.matrixWorldNeedsUpdate = true;
    c.obj.updateMatrixWorld(true);
    c.pts[0].setFromMatrixPosition(c.obj.matrixWorld);
  }

  const tmp = new THREE.Color();
  function update(sel, tool, hover) {
    if (!group.visible) return;
    for (const c of ctrls) {
      if (flat(c)) followFlat(c); else follow(c);
      const on = c.bone === sel && (c.mode === tool || !ctrls.some(x => x.bone === sel && x.mode === tool));
      const k = on ? 0.62 : c === hover ? 0.32 : 0;
      tmp.copy(c.color).lerp(WHITE, k);
      for (const p of c.parts) {
        p.m.color.copy(tmp);
        p.m.opacity = p.op + (p.fill ? (on ? 0.1 : 0) + (c === hover ? 0.12 : 0) : 0);
      }
    }
  }

  // ---------------------------------------------------------------- picking : rayon sur les maillages de clic
  const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
  const pickables = ctrls.flatMap(c => c.picks);
  function pick(ev) {
    if (!group.visible) return null;
    update(null, null, null);
    const r = vp.canvas.getBoundingClientRect();
    ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(ndc, vp.camera);
    const hits = ray.intersectObjects(pickables, false);
    if (!hits.length) return null;
    // à égalité, les petits contrôleurs (correcteurs, mains) passent devant les grands anneaux
    let best = null, bd = Infinity;
    for (const h of hits) {
      const c = h.object.userData.ctrl;
      const d = h.distance + (c.kind === 'ring' ? 2 * u : 0);
      if (d < bd) { bd = d; best = c; }
    }
    return best;
  }

  return {
    ctrls, group, update, pick,
    set visible(on) { group.visible = on; },
    get visible() { return group.visible; },
  };
}
