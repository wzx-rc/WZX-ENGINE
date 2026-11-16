// Navigation du viewport façon Maya / Autodesk :
//  - ViewCube en haut à droite : cliquer une face, une arête ou un coin = vue alignée (26 vues), glisser = tourner,
//    maison = vue de départ. Il suit la caméra et prend la couleur de la faction (--accent).
//  - Barre de navigation en dessous : Tourner, Déplacer, Zoom (outil actif = le clic gauche fait ce geste ;
//    on peut aussi glisser directement sur le bouton), Cadrer (F), Pivot (cliquer un point du modèle pour tourner
//    autour). Échap ou un second clic rend le clic gauche normal.
// Alt + clic gauche / milieu / droit navigue toujours, sans sélectionner ni prendre le gizmo (Viewport.navBlocks).
import * as THREE from 'three';

const fr = () => (window.plasmaLang || document.documentElement.lang || 'fr').startsWith('fr');
const T = (a, b) => (fr() ? a : b);
const accent = () => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#f0435a';

const ICON = {
  orbit: '<path d="M20 12a8 8 0 1 1-3-6.2M20 4v5h-5"/>',
  pan: '<path d="M12 2v20M2 12h20M12 2l-3 3M12 2l3 3M12 22l-3-3M12 22l3-3M2 12l3-3M2 12l3 3M22 12l-3-3M22 12l-3 3"/>',
  zoom: '<circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5L21 21M10.5 7.5v6M7.5 10.5h6"/>',
  frame: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/><rect x="9" y="9" width="6" height="6"/>',
  pivot: '<circle cx="12" cy="12" r="5"/><path d="M12 2v5M12 17v5M2 12h5M17 12h5"/>',
  home: '<path d="M4 11l8-7 8 7M6 10v10h12V10"/>',
};
const svg = k => `<svg viewBox="0 0 24 24" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">${ICON[k]}</svg>`;

const CSS = `
.navui{position:absolute;top:10px;right:10px;z-index:6;display:flex;flex-direction:column;align-items:center;gap:10px;pointer-events:none}
.navui>*{pointer-events:auto}
.navcube{position:relative;width:96px;height:96px;border-radius:50%;background:radial-gradient(circle,rgba(255,255,255,.07),rgba(255,255,255,.02) 62%,transparent 70%)}
.navcube canvas{width:96px;height:96px;display:block;cursor:pointer}
.navcube .home{position:absolute;left:2px;top:2px;width:24px;height:24px;border:0;border-radius:6px;background:rgba(20,21,24,.7);color:#bfc1c7;display:grid;place-items:center;cursor:pointer;padding:0}
.navcube .home svg{width:15px;height:15px}
.navcube .home:hover{color:#fff}
.navbar{display:flex;flex-direction:column;gap:4px;padding:6px;border-radius:14px;background:rgba(24,25,29,.88);border:1px solid rgba(255,255,255,.07);box-shadow:0 6px 20px rgba(0,0,0,.35)}
.navbar button{width:38px;height:38px;border:0;border-radius:10px;background:transparent;color:#d6d7db;display:grid;place-items:center;cursor:pointer;padding:0;touch-action:none}
.navbar button svg{width:20px;height:20px}
.navbar button:hover{background:rgba(255,255,255,.07);color:#fff}
.navbar button[aria-pressed="true"]{background:color-mix(in srgb,var(--accent,#f0435a) 30%,transparent);color:var(--accent-soft,#ff8fa0)}
.navbar hr{width:24px;border:0;border-top:1px solid rgba(255,255,255,.12);margin:3px auto}
`;

export function createNavUI(vp) {
  if (!document.getElementById('navuiStyle')) {
    const st = document.createElement('style'); st.id = 'navuiStyle'; st.textContent = CSS; document.head.appendChild(st);
  }
  const host = vp.canvas.parentElement;
  const root = document.createElement('div');
  root.className = 'navui';
  root.innerHTML = `<div class="navcube"><canvas width="192" height="192"></canvas><button type="button" class="home" data-nav="home">${svg('home')}</button></div>
<div class="navbar" role="toolbar">
  <button type="button" data-nav="orbit">${svg('orbit')}</button>
  <button type="button" data-nav="pan">${svg('pan')}</button>
  <button type="button" data-nav="zoom">${svg('zoom')}</button>
  <hr>
  <button type="button" data-nav="frame">${svg('frame')}</button>
  <button type="button" data-nav="pivot">${svg('pivot')}</button>
</div>`;
  host.appendChild(root);
  const tips = () => ({
    home: T('Vue de départ', 'Home view'),
    orbit: T('Tourner (ou Alt + clic gauche). Glisser sur le bouton pour tourner', 'Orbit (or Alt + left click). Drag on the button to orbit'),
    pan: T('Déplacer (ou Alt + clic milieu). Glisser sur le bouton pour déplacer', 'Pan (or Alt + middle click). Drag on the button to pan'),
    zoom: T('Zoom (ou Alt + clic droit). Glisser sur le bouton pour zoomer', 'Zoom (or Alt + right click). Drag on the button to zoom'),
    frame: T('Cadrer la sélection (F)', 'Frame selection (F)'),
    pivot: T('Pivot : clique un point du modèle pour tourner autour', 'Pivot: click a point on the model to orbit around it'),
  });
  const setTips = () => { const t = tips(); root.querySelectorAll('[data-nav]').forEach(b => { b.title = t[b.dataset.nav]; b.setAttribute('aria-label', t[b.dataset.nav]); }); };
  setTips();

  // ------------------------------------------------------------ outils de la barre
  const btn = id => root.querySelector(`[data-nav="${id}"]`);
  function render() { for (const id of ['orbit', 'pan', 'zoom', 'pivot']) btn(id).setAttribute('aria-pressed', String(vp.navTool === id)); }
  function setTool(t) { vp.setNavTool(vp.navTool === t ? null : t); render(); }
  addEventListener('keydown', e => { if (e.key === 'Escape' && vp.navTool) { vp.setNavTool(null); render(); } });

  // glisser sur un bouton : le geste directement, sans changer d'outil ; un clic sans glisser bascule l'outil
  const cam = vp.camera, ctl = vp.controls;
  const sph = new THREE.Spherical(), off = new THREE.Vector3();
  function orbitBy(dx, dy) {
    off.copy(cam.position).sub(ctl.target);
    sph.setFromVector3(off);
    sph.theta -= dx * 0.008;
    sph.phi = THREE.MathUtils.clamp(sph.phi - dy * 0.008, 0.02, Math.PI - 0.02);
    cam.position.copy(ctl.target).add(off.setFromSpherical(sph));
    cam.lookAt(ctl.target);
  }
  function panBy(dx, dy) {
    const d = cam.position.distanceTo(ctl.target) * Math.tan(THREE.MathUtils.degToRad(cam.fov) / 2) * 2 / vp.canvas.clientHeight;
    const right = new THREE.Vector3().setFromMatrixColumn(cam.matrix, 0), up = new THREE.Vector3().setFromMatrixColumn(cam.matrix, 1);
    const m = right.multiplyScalar(-dx * d).add(up.multiplyScalar(dy * d));
    cam.position.add(m); ctl.target.add(m);
  }
  function zoomBy(dy) {
    off.copy(cam.position).sub(ctl.target);
    const len = THREE.MathUtils.clamp(off.length() * Math.exp(dy * 0.006), ctl.minDistance, ctl.maxDistance);
    cam.position.copy(ctl.target).add(off.setLength(len));
  }
  for (const id of ['orbit', 'pan', 'zoom']) {
    const b = btn(id);
    b.addEventListener('pointerdown', e => {
      if (e.button !== 0) return;
      e.preventDefault();
      b.setPointerCapture(e.pointerId);
      let x = e.clientX, y = e.clientY, moved = 0;
      vp.camAnim = null;
      const mv = ev => {
        const dx = ev.clientX - x, dy = ev.clientY - y; x = ev.clientX; y = ev.clientY;
        moved += Math.abs(dx) + Math.abs(dy);
        if (id === 'orbit') orbitBy(dx, dy); else if (id === 'pan') panBy(dx, dy); else zoomBy(dy);
      };
      const up = () => { b.removeEventListener('pointermove', mv); b.removeEventListener('pointerup', up); if (moved < 4) setTool(id); };
      b.addEventListener('pointermove', mv);
      b.addEventListener('pointerup', up);
    });
  }
  btn('pivot').addEventListener('click', () => setTool('pivot'));
  btn('frame').addEventListener('click', () => vp.focus());
  btn('home').addEventListener('click', () => vp.homeView());

  // outil pivot : clic sur le modèle = nouveau centre de rotation (la caméra ne saute pas, elle glisse)
  const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
  let pdown = null;
  vp.canvas.addEventListener('pointerdown', e => { pdown = vp.navTool === 'pivot' && e.button === 0 && !e.altKey ? [e.clientX, e.clientY] : null; }, true);
  vp.canvas.addEventListener('pointerup', e => {
    if (!pdown || Math.hypot(e.clientX - pdown[0], e.clientY - pdown[1]) > 4) return;
    const r = vp.canvas.getBoundingClientRect();
    ndc.set((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1);
    ray.setFromCamera(ndc, cam);
    const meshes = [];
    vp.scene.traverseVisible(o => { if (o.isMesh && !o.userData.helper && !o.userData.rasterOnly && o !== vp.ptFloor) meshes.push(o); });
    const hit = ray.intersectObjects(meshes, false)[0];
    if (!hit) return;
    const d = hit.point.clone().sub(ctl.target);
    vp.camAnim = { t: 0, fromP: cam.position.clone(), toP: cam.position.clone().add(d), fromT: ctl.target.clone(), toT: hit.point.clone() };
  });

  // ------------------------------------------------------------ ViewCube
  const cv = root.querySelector('.navcube canvas');
  const r3 = new THREE.WebGLRenderer({ canvas: cv, antialias: true, alpha: true });
  r3.setPixelRatio(1);
  r3.setClearColor(0x000000, 0);
  const scene = new THREE.Scene();
  const ocam = new THREE.OrthographicCamera(-1.85, 1.85, 1.85, -1.85, 0.1, 20);
  // cube translucide, étiquettes sur les faces
  const HY = 1, HALF = new THREE.Vector3(1, HY, 1);
  const labels = () => fr() ? ['DROITE', 'GAUCHE', 'HAUT', 'BAS', 'FACE', 'DOS'] : ['RIGHT', 'LEFT', 'TOP', 'BOTTOM', 'FRONT', 'BACK'];
  function faceTex(txt, hot, side) {
    const c = document.createElement('canvas'); c.width = 160; c.height = side ? 104 : 160;
    const g = c.getContext('2d');
    g.fillStyle = hot ? accent() : 'rgba(150,156,168,.20)'; g.fillRect(0, 0, c.width, c.height);
    g.fillStyle = hot ? '#fff' : 'rgba(235,236,240,.92)';
    g.font = `600 ${txt.length > 6 ? 21 : 24}px system-ui, sans-serif`; g.textAlign = 'center'; g.textBaseline = 'middle';
    g.save(); g.translate(c.width / 2, c.height / 2 + 2);
    if (side) g.scale(1, 1.25);
    g.fillText(txt, 0, 0); g.restore();
    const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 4;
    return t;
  }
  let lastLang = null, lastAccent = null, hoverFace = -1;
  const mats = Array.from({ length: 6 }, () => new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.9, depthWrite: false }));
  const cube = new THREE.Mesh(new THREE.BoxGeometry(2, 2 * HY, 2), mats);
  const lineMat = new THREE.LineBasicMaterial({ color: 0xffffff, transparent: true, opacity: 0.85 });
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(cube.geometry), lineMat);
  cube.add(edges);
  scene.add(cube);
  // repère : petites lignes X / Y / Z au coin arrière-bas-gauche, comme Maya
  const axes = new THREE.Group();
  for (const [d, c] of [[[1, 0, 0], 0xff4a4a], [[0, 1, 0], 0x4fe04f], [[0, 0, 1], 0x4a8dff]]) {
    const g = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(...d).multiplyScalar(1.1)]);
    axes.add(new THREE.Line(g, new THREE.LineBasicMaterial({ color: c })));
  }
  axes.position.set(-1.02, -HY - 0.02, -1.02);
  scene.add(axes);
  // zone survolée (arête / coin) : petit carré qui suit le pointeur
  const hot = new THREE.Mesh(new THREE.SphereGeometry(0.28, 16, 12), new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.85, depthTest: false }));
  hot.visible = false; hot.renderOrder = 5;
  scene.add(hot);

  function refreshFaces() {
    const lang = fr(), ac = accent();
    if (lang === lastLang && ac === lastAccent && mats[0].userData.hot === hoverFace) return;
    if (lang !== lastLang) setTips();
    lastLang = lang; lastAccent = ac;
    labels().forEach((t, i) => { mats[i].map?.dispose(); mats[i].map = faceTex(t, i === hoverFace, false); mats[i].needsUpdate = true; });
    mats[0].userData.hot = hoverFace;
    hot.material.color.set(ac);
    lineMat.color.set(0xffffff);
  }

  // direction de vue sous le pointeur : face, arête ou coin selon la position sur la face
  const ray2 = new THREE.Raycaster(), n2 = new THREE.Vector2();
  function hitDir(e) {
    const r = cv.getBoundingClientRect();
    n2.set((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1);
    ray2.setFromCamera(n2, ocam);
    const h = ray2.intersectObject(cube, false)[0];
    if (!h) return null;
    const p = h.point, d = new THREE.Vector3();
    for (const a of ['x', 'y', 'z']) d[a] = Math.abs(p[a] / HALF[a]) > 0.62 ? Math.sign(p[a]) : 0;
    return { dir: d, face: h.face.materialIndex, point: d.clone().multiply(HALF) };
  }
  let cdown = null;
  cv.addEventListener('pointerdown', e => { if (e.button !== 0) return; cv.setPointerCapture(e.pointerId); cdown = { x: e.clientX, y: e.clientY, lx: e.clientX, ly: e.clientY, moved: 0 }; vp.camAnim = null; });
  cv.addEventListener('pointermove', e => {
    if (cdown) {
      const dx = e.clientX - cdown.lx, dy = e.clientY - cdown.ly; cdown.lx = e.clientX; cdown.ly = e.clientY;
      cdown.moved += Math.abs(dx) + Math.abs(dy);
      if (cdown.moved > 3) orbitBy(dx, dy);
      return;
    }
    const h = hitDir(e);
    const nz = h ? [h.dir.x, h.dir.y, h.dir.z].filter(v => v).length : 0;
    hoverFace = h && nz === 1 ? h.face : -1;
    hot.visible = !!h && nz > 1;
    if (hot.visible) hot.position.copy(h.point);
  });
  cv.addEventListener('pointerleave', () => { hoverFace = -1; hot.visible = false; });
  cv.addEventListener('pointerup', e => {
    if (!cdown) return;
    const click = cdown.moved <= 3;
    cdown = null;
    if (!click) return;
    const h = hitDir(e);
    if (h && h.dir.lengthSq()) vp.viewFrom(h.dir);
  });

  // chaque image : le cube tourne avec la caméra
  const q = new THREE.Quaternion();
  return {
    root,
    render: () => { render(); },
    update() {
      refreshFaces();
      q.copy(cam.quaternion);
      ocam.position.set(0, 0, 10).applyQuaternion(q);
      ocam.quaternion.copy(q);
      ocam.updateMatrixWorld();
      r3.render(scene, ocam);
    },
  };
}
