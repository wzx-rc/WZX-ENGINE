// Gizmo de transformation façon Maya (remplace le TransformControls de three.js, même API pour le reste de l'app :
// attach / detach / setMode / setSpace / setSize, object, dragging, axis, événements mouseDown, objectChange,
// mouseUp, dragging-changed).
// - Look : traits fins, X rouge, Y vert, Z bleu, poignée survolée ou active en jaune ; la dernière poignée tirée
//   reste jaune comme dans Maya. Taille constante à l'écran (touches + / - pour l'agrandir ou la réduire).
// - Déplacer : flèches à pointe conique, carrés de plan (XY, YZ, XZ) et carré central (plan de la vue).
// - Tourner : trois anneaux dont seule la moitié face à la caméra est dessinée, anneau bleu clair de la vue autour,
//   boule libre au centre (tourner librement en tirant à l'intérieur).
// - Échelle : axes à cube, cube central = échelle uniforme (calculée depuis le début du geste, à la taille du
//   gizmo à l'écran : tirer d'une longueur de gizmo double l'échelle sur un axe, deux longueurs en uniforme).
// - Marking menu comme Maya : garder W, E ou R enfoncé + clic gauche dans le viewport, relâcher sur Objet (local),
//   Monde ou Auto (l'espace choisi par l'outil). Le choix est gardé sur ce poste.
import * as THREE from 'three';
import { LineSegments2 } from 'three/addons/lines/LineSegments2.js';
import { LineSegmentsGeometry } from 'three/addons/lines/LineSegmentsGeometry.js';
import { LineMaterial } from 'three/addons/lines/LineMaterial.js';

const COL = { X: 0xff4a4a, Y: 0x4fe04f, Z: 0x4a8dff, V: 0x7fd4ff, C: 0xffff66, ACTIVE: 0xffff3a, XY: 0x4a8dff, YZ: 0xff4a4a, XZ: 0x4fe04f, XYZ: 0xffff66, E: 0xbfbfbf };
const AX = { X: new THREE.Vector3(1, 0, 0), Y: new THREE.Vector3(0, 1, 0), Z: new THREE.Vector3(0, 0, 1) };
const PICK_PX = 10;

export class MayaGizmo extends THREE.EventDispatcher {
  constructor(camera, canvas) {
    super();
    this.camera = camera;
    this.canvas = canvas;
    this.object = null;
    this.mode = 'translate';
    this.space = 'local';
    this.size = 1;
    this.axis = null;        // poignée survolée ou tirée : 'X', 'Y', 'Z', 'XY', 'YZ', 'XZ', 'V' (vue), 'E' (boule), 'XYZ'
    this.last = null;        // dernière poignée tirée (reste jaune)
    this.dragging = false;
    this.enabled = true;
    this.userSpace = null;   // 'local' | 'world' choisi au marking menu, null = celui de l'outil
    try { const sp = localStorage.getItem('plasmaork.gizmo.space'); if (sp === 'local' || sp === 'world') this.userSpace = sp; } catch { /* ignoré */ }
    this._held = null;       // touche W / E / R tenue (marking menu)
    this.root = new THREE.Group();
    this.root.visible = false;
    this.root.userData.helper = true;
    this.root.renderOrder = 40;
    this.handles = [];       // { id, kind: 'line' | 'mesh', obj, pts (local, pour le picking), color }
    this._res = new THREE.Vector2(1, 1);
    this._build();
    this._onDown = this._onDown.bind(this);
    this._onMove = this._onMove.bind(this);
    this._onUp = this._onUp.bind(this);
    canvas.addEventListener('pointerdown', this._onDown, true);
    // marking menu : sur tout le viewport (y compris les pastilles posées sur le canvas)
    (canvas.parentElement || canvas).addEventListener('pointerdown', e => {
      if (!this.enabled || e.button !== 0 || !this._held || this.dragging || e.altKey || e.target.closest?.('.navui')) return;
      e.stopImmediatePropagation(); e.preventDefault();
      this._menu(e);
    }, true);
    canvas.addEventListener('pointermove', this._onMove);
    addEventListener('pointerup', this._onUp);
    addEventListener('keydown', e => {
      if (e.target.closest?.('input, select, textarea')) return;
      const k = e.key.toLowerCase();
      if ((k === 'w' || k === 'e' || k === 'r') && !e.ctrlKey && !e.metaKey && !e.altKey) this._held = k;
      if (!this.object) return;
      if (e.key === '+' || e.key === '=') this.size = Math.min(3, this.size * 1.15);
      else if (e.key === '-' || e.key === '_') this.size = Math.max(0.3, this.size / 1.15);
    });
    addEventListener('keyup', e => { if (e.key.toLowerCase() === this._held) this._held = null; });
    addEventListener('blur', () => { this._held = null; });
  }

  // ------------------------------------------------------------ marking menu (W / E / R tenu + clic gauche)
  setUserSpace(sp) {
    this.userSpace = sp;
    try { if (sp) localStorage.setItem('plasmaork.gizmo.space', sp); else localStorage.removeItem('plasmaork.gizmo.space'); } catch { /* ignoré */ }
    this.dispatchEvent({ type: 'space-changed', value: sp });
  }
  _menu(e) {
    const fr = (window.plasmaLang || document.documentElement.lang || 'fr').startsWith('fr');
    const items = [
      { id: 'local', label: fr ? 'Objet (local)' : 'Object (local)', a: -Math.PI / 2 },
      { id: 'world', label: fr ? 'Monde' : 'World', a: Math.PI / 2 },
      { id: null, label: fr ? 'Auto (outil)' : 'Auto (tool)', a: 0 },
    ];
    if (!document.getElementById('mmStyle')) {
      const st = document.createElement('style');
      st.id = 'mmStyle';
      st.textContent = `.mm{position:fixed;inset:0;z-index:9999;cursor:default}
.mm svg{position:absolute;overflow:visible;pointer-events:none}
.mm b{position:absolute;transform:translate(-50%,-50%);padding:6px 12px;border-radius:7px;font:600 12px/1.2 system-ui,sans-serif;white-space:nowrap;
background:rgba(30,33,37,.94);color:#e9e6df;border:1px solid rgba(255,255,255,.14);box-shadow:0 4px 14px rgba(0,0,0,.45)}
.mm b{transition:background .08s,border-color .08s,color .08s}
.mm b.cur{border-color:var(--line2,#3a3c42);color:var(--accent,#f0435a)}
.mm b.on{background:var(--accent-bg,#2a1418);color:var(--accent,#f0435a);border-color:var(--accent,#f0435a)}
.mm i{position:absolute;transform:translate(-50%,0);font:11px system-ui,sans-serif;color:rgba(255,255,255,.6);font-style:normal;white-space:nowrap}`;
      document.head.appendChild(st);
    }
    const R = 140;   // options deux fois plus loin du centre
    const x = Math.min(Math.max(e.clientX, R + 70), innerWidth - R - 70), y = Math.min(Math.max(e.clientY, R + 30), innerHeight - R - 50);
    const el = document.createElement('div');
    el.className = 'mm';
    el.innerHTML = `<svg style="left:${x}px;top:${y}px" width="0" height="0"><circle r="2.5" fill="var(--accent,#f0435a)"/><circle r="${R * 0.21}" fill="none" stroke="rgba(255,255,255,.55)" stroke-width="1.2" stroke-dasharray="3 4"/></svg>`
      + items.map((it, i) => `<b data-i="${i}" class="${it.id === this.userSpace ? 'cur' : ''}" style="left:${x + Math.cos(it.a) * R}px;top:${y + Math.sin(it.a) * R}px">${it.label}</b>`).join('')
      + `<i style="left:${x}px;top:${y + R * 0.21 + 14}px">${fr ? 'Espace du gizmo' : 'Gizmo space'}</i>`;
    document.body.appendChild(el);
    let pick = -1;
    const move = ev => {
      const dx = ev.clientX - x, dy = ev.clientY - y;
      pick = -1;
      const over = ev.target.closest?.('.mm b');
      if (over) pick = +over.dataset.i;
      else if (dx * dx + dy * dy > 20 * 20) {
        const a = Math.atan2(dy, dx);
        let bd = Infinity;
        items.forEach((it, i) => { const d = Math.abs(Math.atan2(Math.sin(a - it.a), Math.cos(a - it.a))); if (d < bd) { bd = d; pick = i; } });
      }
      el.querySelectorAll('b').forEach((b, i) => b.classList.toggle('on', i === pick));
    };
    const up = ev => {
      move(ev);
      el.remove();
      removeEventListener('pointermove', move, true);
      removeEventListener('pointerup', up, true);
      if (pick >= 0) this.setUserSpace(items[pick].id);
    };
    addEventListener('pointermove', move, true);
    addEventListener('pointerup', up, true);
  }

  // ------------------------------------------------------------ API (compatible TransformControls)
  getHelper() { return this.root; }
  setSize(s) { this.size = s / 0.8; }
  setSpace(s) { this.space = s; }
  setMode(m) { if (this.mode !== m) { this.mode = m; this.last = null; } this._show(); }
  attach(o) { this.object = o; this.root.visible = true; this._show(); return this; }
  detach() {
    if (this.dragging) this._end();
    this.object = null; this.axis = null; this.root.visible = false;
    return this;
  }

  // ------------------------------------------------------------ construction des formes (repère unité, L = 1)
  _mat(color, opacity = 1) {
    return new LineMaterial({ color, linewidth: 2, transparent: true, opacity, depthTest: false, depthWrite: false });
  }
  _line(id, pts, color, group, opacity = 1) {
    const g = new LineSegmentsGeometry().setPositions(pts.flatMap(p => [p.x, p.y, p.z]));
    const l = new LineSegments2(g, this._mat(color, opacity));
    l.renderOrder = 41;
    l.userData.helper = true;
    group.add(l);
    const h = { id, kind: 'line', obj: l, pts, color, group };
    this.handles.push(h);
    return h;
  }
  _mesh(id, geo, color, group, opacity = 1, pick = true) {
    const m = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ color, transparent: true, opacity, depthTest: false, depthWrite: false, side: THREE.DoubleSide }));
    m.renderOrder = 42;
    m.userData.helper = true;
    group.add(m);
    const h = { id, kind: 'mesh', obj: m, color, group, pick, opacity };
    this.handles.push(h);
    return h;
  }
  _build() {
    const ring = (n, r, seg = 64) => {
      const u = Math.abs(n.y) < 0.9 ? new THREE.Vector3(0, 1, 0).cross(n).normalize() : new THREE.Vector3(1, 0, 0).cross(n).normalize();
      const v = n.clone().cross(u);
      const pts = [];
      for (let i = 0; i < seg; i++) for (const k of [i, i + 1]) { const a = k / seg * Math.PI * 2; pts.push(u.clone().multiplyScalar(Math.cos(a) * r).addScaledVector(v, Math.sin(a) * r)); }
      return pts;
    };
    // déplacer
    const T = this.gT = new THREE.Group();
    for (const a of ['X', 'Y', 'Z']) {
      this._line(a, [AX[a].clone().multiplyScalar(0.12), AX[a].clone().multiplyScalar(0.92)], COL[a], T);
      const cone = new THREE.ConeGeometry(0.045, 0.18, 14);
      cone.translate(0, 0.98, 0);
      if (a === 'X') cone.rotateZ(-Math.PI / 2); else if (a === 'Z') cone.rotateX(Math.PI / 2);
      this._mesh(a, cone, COL[a], T);
    }
    const plane = (id, a, b) => {
      const q = 0.22, s = 0.1;
      const p = (x, y) => AX[a].clone().multiplyScalar(x).addScaledVector(AX[b], y);
      const geo = new THREE.BufferGeometry().setFromPoints([p(q, q), p(q + s, q), p(q + s, q + s), p(q, q), p(q + s, q + s), p(q, q + s)]);
      this._mesh(id, geo, COL[id], T, 0.55);
    };
    plane('XY', 'X', 'Y'); plane('YZ', 'Y', 'Z'); plane('XZ', 'X', 'Z');
    // carré central (plan de la vue), orienté vers la caméra à chaque image
    this.center = this._line('V', [[-1, -1], [1, -1], [1, -1], [1, 1], [1, 1], [-1, 1], [-1, 1], [-1, -1]].map(([x, y]) => new THREE.Vector3(x * 0.07, y * 0.07, 0)), COL.C, T);
    this.center.billboard = true;
    // tourner
    const R = this.gR = new THREE.Group();
    this.rings = {};
    for (const a of ['X', 'Y', 'Z']) { this.rings[a] = this._line(a, ring(AX[a], 1), COL[a], R); this.rings[a].ring = AX[a]; }
    this.viewRing = this._line('V', ring(new THREE.Vector3(0, 0, 1), 1.18, 72), COL.V, R);
    this.viewRing.billboard = true;
    const ball = this._mesh('E', new THREE.CircleGeometry(1, 48), 0x9a9a9a, R, 0.08);
    ball.billboard = true;
    this.ball = ball;
    // échelle
    const S = this.gS = new THREE.Group();
    for (const a of ['X', 'Y', 'Z']) {
      this._line(a, [AX[a].clone().multiplyScalar(0.12), AX[a].clone().multiplyScalar(0.92)], COL[a], S);
      const cube = new THREE.BoxGeometry(0.09, 0.09, 0.09);
      cube.translate(AX[a].x * 0.97, AX[a].y * 0.97, AX[a].z * 0.97);
      this._mesh(a, cube, COL[a], S);
    }
    this._mesh('XYZ', new THREE.BoxGeometry(0.11, 0.11, 0.11), COL.C, S);
    this.root.add(T, R, S);
  }
  _show() {
    this.gT.visible = this.mode === 'translate';
    this.gR.visible = this.mode === 'rotate';
    this.gS.visible = this.mode === 'scale';
  }
  _active(h) { return h.group.visible; }

  // ------------------------------------------------------------ chaque image : position, taille, orientation, couleurs
  _frame() {
    const o = this.object;
    o.updateWorldMatrix(true, false);
    const pos = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3();
    o.matrixWorld.decompose(pos, q, s);
    this._pos = pos;
    const space = this.userSpace || this.space;
    this._q = (space === 'local' || this.mode === 'scale') ? q : new THREE.Quaternion();
    const cam = this.camera;
    const dist = cam.position.distanceTo(pos);
    const L = this._L = dist * Math.tan(THREE.MathUtils.degToRad(cam.fov) / 2) * 0.24 * this.size;
    this.root.position.copy(pos);
    this.root.scale.setScalar(L);
    for (const g of [this.gT, this.gR, this.gS]) g.quaternion.copy(this._q);
    this.root.updateMatrixWorld(true);
    // formes tournées vers la caméra (carré central, anneau de vue, boule)
    const inv = this._q.clone().invert();
    const camQ = cam.quaternion.clone();
    for (const h of [this.center, this.viewRing, this.ball]) { h.obj.quaternion.copy(inv).multiply(camQ); }
    // anneaux : seulement la moitié avant (face à la caméra)
    const toCam = cam.position.clone().sub(pos).normalize().applyQuaternion(inv);
    for (const a of ['X', 'Y', 'Z']) {
      const h = this.rings[a];
      const front = [];
      for (let i = 0; i < h.pts.length; i += 2) {
        const m = h.pts[i].clone().add(h.pts[i + 1]);
        if (m.dot(toCam) > -0.02 || (this.dragging && this.axis === a)) front.push(h.pts[i], h.pts[i + 1]);
      }
      h.obj.geometry.setPositions(front.flatMap(p => [p.x, p.y, p.z]));
      h.vis = front;
    }
    this.root.updateMatrixWorld(true);
    // couleurs : survol / tirée = jaune, dernière tirée = jaune
    this.canvas.ownerDocument && this._res.set(this.canvas.clientWidth, this.canvas.clientHeight);
    for (const h of this.handles) {
      const on = h.id === this.axis || (!this.axis && h.id === this.last);
      const m = h.obj.material;
      if (h.id === 'E') { m.color.set(on ? 0xd8d8d8 : h.color); m.opacity = on ? 0.16 : h.opacity; continue; }   // boule libre : gris très léger, comme Maya
      m.color.set(on ? COL.ACTIVE : h.color);
      if (h.kind === 'line') { m.resolution.copy(this._res); m.linewidth = on ? 3 : 2; }
      else m.opacity = on ? Math.max(h.opacity, 0.7) : h.opacity;
    }
  }
  // appelé par le viewport juste avant le rendu de l'overlay
  update() { if (this.object && this.root.visible) this._frame(); }

  // ------------------------------------------------------------ picking à l'écran
  _ndc(e) { const r = this.canvas.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top, r]; }
  _screen(p, r) { const v = p.clone().project(this.camera); return new THREE.Vector3((v.x + 1) / 2 * r.width, (1 - v.y) / 2 * r.height, v.z); }
  _pick(e) {
    if (!this.object || !this.root.visible) return null;
    this._frame();
    const [mx, my, r] = this._ndc(e);
    let best = null, bd = PICK_PX * PICK_PX;
    const seg = (a, b) => {
      const dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
      const t = l2 ? Math.max(0, Math.min(1, ((mx - a.x) * dx + (my - a.y) * dy) / l2)) : 0;
      const ex = a.x + dx * t - mx, ey = a.y + dy * t - my;
      return ex * ex + ey * ey;
    };
    for (const h of this.handles) {
      if (!this._active(h) || h.id === 'E') continue;
      const mw = h.obj.matrixWorld;
      if (h.kind === 'line') {
        const pts = h.vis || h.pts;
        for (let i = 0; i < pts.length; i += 2) {
          const a = this._screen(pts[i].clone().applyMatrix4(mw), r), b = this._screen(pts[i + 1].clone().applyMatrix4(mw), r);
          const d = seg(a, b);
          if (d < bd) { bd = d; best = h.id; }
        }
      } else if (h.pick) {
        // maille : distance au centre de sa boîte, ou dedans
        const box = new THREE.Box3().setFromBufferAttribute(h.obj.geometry.attributes.position);
        const c = this._screen(box.getCenter(new THREE.Vector3()).applyMatrix4(mw), r);
        const ext = this._screen(box.max.clone().applyMatrix4(mw), r);
        const rad = Math.max(6, Math.hypot(ext.x - c.x, ext.y - c.y) * 0.8);
        const d = (c.x - mx) ** 2 + (c.y - my) ** 2;
        if (d < rad * rad && d * 0.6 < bd) { bd = d * 0.6; best = h.id; }
      }
    }
    // boule libre : à l'intérieur de l'anneau des axes
    if (!best && this.mode === 'rotate') {
      const c = this._screen(this._pos, r), e1 = this._screen(this._pos.clone().add(new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion).multiplyScalar(this._L)), r);
      if (Math.hypot(mx - c.x, my - c.y) < Math.hypot(e1.x - c.x, e1.y - c.y) * 0.95) best = 'E';
    }
    return best;
  }

  // ------------------------------------------------------------ tirer
  _ray(e) {
    const r = this.canvas.getBoundingClientRect();
    const ray = new THREE.Raycaster();
    ray.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), this.camera);
    return ray.ray;
  }
  _axisW(id) { return AX[id].clone().applyQuaternion(this._q).normalize(); }
  _planeHit(e, normal, point) {
    const p = new THREE.Plane().setFromNormalAndCoplanarPoint(normal, point);
    return this._ray(e).intersectPlane(p, new THREE.Vector3());
  }
  // point de l'axe le plus proche du rayon de la souris
  _axisParam(e, dir) {
    const ray = this._ray(e), p0 = this._start.pos;
    const w0 = p0.clone().sub(ray.origin);
    const a = dir.dot(dir), b = dir.dot(ray.direction), c = ray.direction.dot(ray.direction), d = dir.dot(w0), ee = ray.direction.dot(w0);
    const den = a * c - b * b;
    return Math.abs(den) < 1e-9 ? 0 : (b * ee - c * d) / den;
  }
  _onDown(e) {
    if (!this.enabled || e.button !== 0 || !this.object || this.blocked?.(e)) return;
    const id = this._pick(e);
    if (!id) return;
    e.stopImmediatePropagation();
    this.axis = id;
    this.dragging = true;
    this.canvas.setPointerCapture?.(e.pointerId);
    const o = this.object;
    o.updateWorldMatrix(true, false);
    const pos = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3();
    o.matrixWorld.decompose(pos, q, s);
    const camDir = this.camera.getWorldDirection(new THREE.Vector3());
    this._start = { pos, q, s, local: { p: o.position.clone(), q: o.quaternion.clone(), s: o.scale.clone() }, x: e.clientX, y: e.clientY, camDir };
    if (this.mode === 'translate') {
      if (['X', 'Y', 'Z'].includes(id)) this._start.t = this._axisParam(e, this._axisW(id));
      else {
        const n = id === 'V' ? camDir.clone().negate() : this._axisW({ XY: 'Z', YZ: 'X', XZ: 'Y' }[id]);
        this._start.n = n;
        this._start.hit = this._planeHit(e, n, pos);
      }
    } else if (this.mode === 'rotate') {
      const n = id === 'V' ? camDir.clone().negate() : id === 'E' ? null : this._axisW(id);
      this._start.n = n;
      if (n) {
        // comme Maya : on tire l'anneau le long de sa tangente à l'écran au point saisi (un tour = le périmètre)
        const [, , r] = this._ndc(e);
        const hit = this._planeHit(e, n, pos);
        let v = hit ? hit.sub(pos) : null;
        if (!v || v.lengthSq() < 1e-12 || Math.abs(n.dot(camDir)) < 0.15) v = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion).cross(n);
        v.normalize().multiplyScalar(this._L);
        const tan = n.clone().cross(v);
        const a = this._screen(pos.clone().add(v), r), b = this._screen(pos.clone().add(v).add(tan), r);
        const t2 = new THREE.Vector2(b.x - a.x, b.y - a.y);
        const len = t2.length();
        this._start.t2 = len > 1e-3 ? t2.divideScalar(len) : new THREE.Vector2(1, 0);
        this._start.rpx = Math.max(40, len);
      }
    } else {
      // échelle : longueur du gizmo à l'écran, et position de la prise le long de l'axe
      const [mx, my, r] = this._ndc(e);
      const c = this._screen(pos, r);
      const dirW = id === 'XYZ' ? new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion) : this._axisW(id);
      const end = this._screen(pos.clone().addScaledVector(dirW, this._L), r);
      const sd = new THREE.Vector2(end.x - c.x, end.y - c.y);
      const g = Math.max(30, sd.length());
      this._start.g = g;
      this._start.sd = sd.divideScalar(sd.length() || 1);
      this._start.t0 = Math.max(g * 0.3, (mx - c.x) * this._start.sd.x + (my - c.y) * this._start.sd.y);
      this._start.c = c;
    }
    this.dispatchEvent({ type: 'dragging-changed', value: true });
    this.dispatchEvent({ type: 'mouseDown', mode: this.mode });
  }
  _onMove(e) {
    if (!this.object || !this.root.visible) return;
    if (!this.dragging) {
      if (e.buttons) return;
      const id = e.altKey || this.blocked?.(e) ? null : this._pick(e);
      if (id !== this.axis) this.axis = id;
      this.canvas.style.cursor = id ? 'pointer' : '';
      return;
    }
    const o = this.object, st = this._start, id = this.axis;
    const parentInvQ = new THREE.Quaternion();
    if (o.parent) { o.parent.updateWorldMatrix(true, false); o.parent.getWorldQuaternion(parentInvQ).invert(); }
    if (this.mode === 'translate') {
      let delta = null;
      if (['X', 'Y', 'Z'].includes(id)) { const dir = this._axisW(id); delta = dir.multiplyScalar(this._axisParam(e, dir) - st.t); }
      else { const hit = this._planeHit(e, st.n, st.pos); if (hit && st.hit) delta = hit.sub(st.hit); }
      if (!delta) return;
      const target = st.pos.clone().add(delta);
      if (o.parent) o.position.copy(o.parent.worldToLocal(target)); else o.position.copy(target);
    } else if (this.mode === 'rotate') {
      let rot;
      if (id === 'E') {
        // boule libre : glisser = tourner autour des axes de l'écran
        const dx = e.clientX - st.x, dy = e.clientY - st.y;
        const up = new THREE.Vector3(0, 1, 0).applyQuaternion(this.camera.quaternion), right = new THREE.Vector3(1, 0, 0).applyQuaternion(this.camera.quaternion);
        rot = new THREE.Quaternion().setFromAxisAngle(up, dx * 0.01).multiply(new THREE.Quaternion().setFromAxisAngle(right, dy * 0.01));
      } else {
        let ang = ((e.clientX - st.x) * st.t2.x + (e.clientY - st.y) * st.t2.y) / st.rpx;
        if (e.ctrlKey) ang = Math.round(ang / (Math.PI / 12)) * (Math.PI / 12);   // Ctrl : pas de 15°
        rot = new THREE.Quaternion().setFromAxisAngle(st.n, ang);
      }
      const world = rot.multiply(st.q);
      o.quaternion.copy(parentInvQ.multiply(world));
    } else {
      // échelle, toujours depuis le début du geste (pas d'accumulation) :
      // axe = rapport des distances au centre le long de l'axe (comme Maya) ;
      // uniforme = glisser à droite / en haut, deux longueurs de gizmo pour doubler. Shift : 10 fois plus fin.
      const fine = e.shiftKey ? 0.1 : 1;
      let k;
      if (id === 'XYZ') {
        const d = ((e.clientX - st.x) - (e.clientY - st.y)) / 2;
        k = Math.pow(2, d * fine / (2 * st.g));
      } else {
        const [mx, my] = this._ndc(e);
        const t = (mx - st.c.x) * st.sd.x + (my - st.c.y) * st.sd.y;
        k = 1 + (t / st.t0 - 1) * fine;
      }
      if (e.ctrlKey) k = Math.max(0.1, Math.round(k * 10) / 10);   // Ctrl : pas de 10 %
      k = Math.max(0.01, k);
      o.scale.copy(st.local.s);
      if (id === 'XYZ') o.scale.multiplyScalar(k); else o.scale[id.toLowerCase()] *= k;
    }
    o.updateMatrixWorld(true);
    this.dispatchEvent({ type: 'objectChange' });
  }
  _onUp(e) { if (this.dragging) { this._end(); } }
  _end() {
    this.last = this.axis;
    this.dragging = false;
    this.dispatchEvent({ type: 'dragging-changed', value: false });
    this.dispatchEvent({ type: 'mouseUp', mode: this.mode });
    this.axis = null;
  }
}
