// Viewport réaliste, repris du renderer Viewport3D (voir render.js) :
// - Temps réel : PBR raster, ombres Poisson, MSAA 4x, rendu HDR puis passe Tonemap (exposition, vignettage,
//   ACES / AgX / AgX Punchy / Standard, contour de sélection) ;
// - Haute qualité : même chose en plus fin (résolution, carte d'ombre) ;
// - Path tracing : rendu progressif (pathtrace.js), le raster sert d'aperçu pendant les changements.
// Les aides (gizmo, grille, visor de la pièce en main) sont dessinées après le tonemapping, testées contre la
// profondeur de la scène.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { Post, Studio } from './render.js';
import { PathTrace } from './pathtrace.js';

const RESIN = { color: 0x8f9297, roughness: 0.56, metalness: 0, clearcoat: 0.12, clearcoatRoughness: 0.55 };
const PAINT = {
  skin: { color: 0x5b7a37, roughness: 0.62, metalness: 0, sheen: 0.3, sheenColor: 0x9fbf6a },
  metal: { color: 0x7d7f84, roughness: 0.42, metalness: 0.85 },
  leather: { color: 0x4a3122, roughness: 0.7, metalness: 0 },
  cloth: { color: 0x4f4b3b, roughness: 0.85, metalness: 0 },
  wood: { color: 0x5e3f25, roughness: 0.75, metalness: 0 },
  bone: { color: 0xd9cfb3, roughness: 0.5, metalness: 0 },
  stone: { color: 0x6f6c66, roughness: 0.9, metalness: 0 },
  eye: { color: 0xd8322a, roughness: 0.3, metalness: 0, emissive: 0x9a1a14 },
  imported: { color: 0x8f9297, roughness: 0.56, metalness: 0 },
};

export const RENDER_DEFAULTS = {
  tonemapper: 0, ev: 0, vignette: 0.12,
  env: 'studio', envIntensity: 1, envRotation: 0, envVisible: false, studioLights: true,
  bounces: 6, filterGlossy: 0, denoise: true, dof: false, fStop: 11,
};

const BLACK = new THREE.MeshBasicMaterial({ color: 0x000000 });
const WHITE = new THREE.MeshBasicMaterial({ color: 0xffffff });

export class Viewport {
  constructor(canvas) {
    this.canvas = canvas;
    const r = this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
    r.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    r.shadowMap.enabled = true;
    r.shadowMap.type = THREE.PCFShadowMap;  // remplacé par le filtre de Poisson de Raster.hlsl (render.js)
    r.toneMapping = THREE.NoToneMapping;
    r.outputColorSpace = THREE.SRGBColorSpace;
    r.setClearColor(0x000000, 0);

    this.scene = new THREE.Scene();
    this.overlay = new THREE.Scene();
    this.studio = new Studio(this.scene);
    this.settings = { ...RENDER_DEFAULTS };

    // Ombre portée sur le plateau en temps réel ; vrai sol pour le path tracer.
    const shadow = new THREE.Mesh(new THREE.PlaneGeometry(8, 8), new THREE.ShadowMaterial({ opacity: 0.6, depthWrite: false }));
    shadow.rotation.x = -Math.PI / 2;
    shadow.position.y = -0.1;
    shadow.receiveShadow = true;
    shadow.userData.rasterOnly = true;
    this.scene.add(shadow);
    this.ptFloor = new THREE.Mesh(new THREE.CircleGeometry(30, 96), new THREE.MeshStandardMaterial({ color: 0x2a2c31, roughness: 0.6 }));
    this.ptFloor.rotation.x = -Math.PI / 2;
    this.ptFloor.position.y = -0.1005;
    this.ptFloor.visible = false;
    this.scene.add(this.ptFloor);
    this.grid = new THREE.GridHelper(4, 40, 0x3a3c42, 0x26282d);
    this.grid.position.y = -0.099;
    this.grid.visible = false;
    this.overlay.add(this.grid);

    this.camera = new THREE.PerspectiveCamera(32, 1, 0.05, 100);
    this.camera.position.set(2.4, 2.0, 4.6);
    this.controls = new OrbitControls(this.camera, canvas);
    this.controls.target.set(0, 1.05, 0);
    this.controls.enableDamping = true;
    this.controls.minDistance = 1;
    this.controls.maxDistance = 12;
    this.controls.mouseButtons = { LEFT: null, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.ROTATE };
    this.controls.touches = { ONE: THREE.TOUCH.ROTATE, TWO: THREE.TOUCH.DOLLY_PAN };

    this.gizmo = new TransformControls(this.camera, canvas);
    this.gizmo.setSize(0.8);
    this.gizmo.setSpace('local');
    this.gizmo.addEventListener('dragging-changed', e => { this.controls.enabled = !e.value; });
    this.overlay.add(this.gizmo.getHelper());

    // Cibles : couleur HDR + profondeur (MSAA 4x comme le temps réel du moteur), masque de sélection.
    this.hdr = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType, samples: 4, depthTexture: new THREE.DepthTexture(1, 1) });
    this.mask = new THREE.WebGLRenderTarget(1, 1);
    this.post = new Post();
    this.maskTexel = new THREE.Vector2();

    this.mode = 'resin';
    this.materials = new Map();
    this.quality = 'rt';
    this.clock = new THREE.Clock();
    this.onFrame = null;
    this.camAnim = null;
    this.pt = null;
    this.ptState = { sig: 0, changedAt: 0, dirty: true, studio: -1, spp: 0 };
    this.unitsPerMeter = 61;

    new ResizeObserver(() => this.resize()).observe(canvas.parentElement);
    this.resize();
    r.setAnimationLoop(() => this.frame());
  }

  material(tag) {
    const key = this.mode + ':' + tag;
    if (!this.materials.has(key)) {
      const base = this.mode === 'paint' ? (PAINT[tag] || PAINT.imported) : RESIN;
      const m = new THREE.MeshPhysicalMaterial(base);
      if (this.mode === 'wire') m.wireframe = true;
      this.materials.set(key, m);
    }
    return this.materials.get(key);
  }

  setMode(mode) { this.mode = mode; }

  // Réglages de rendu (panneau Rendu). Ceux de l'environnement passent au studio.
  setRender(patch) {
    const envChanged = 'env' in patch && patch.env !== this.studio.state.env;
    Object.assign(this.settings, patch);
    const s = this.settings;
    const st = { env: s.env, intensity: s.envIntensity, rotation: s.envRotation, visible: s.envVisible };
    // Changer d'environnement remet ses lumières par défaut (studio pour les dégradés, soleil pour ciel / HDRI).
    if ('studioLights' in patch || !envChanged) st.lights = s.studioLights;
    this.studio.set(st);
    s.studioLights = this.studio.state.lights;
    this.configurePT();
  }

  async setHDRI(file) {
    await this.studio.setHDRI(file);
    Object.assign(this.settings, { env: 'hdri', studioLights: this.studio.state.lights });
  }

  configurePT() {
    if (!this.pt) return;
    const s = this.settings;
    const focus = this.camera.position.distanceTo(this.controls.target);
    this.pt.configure({ bounces: s.bounces, filterGlossy: s.filterGlossy, denoise: s.denoise, dof: s.dof, fStop: s.fStop, focus, unitsPerMeter: this.unitsPerMeter });
    this.ptState.dirty = true;
    this.ptState.changedAt = performance.now();
  }

  async setQuality(q) {
    this.quality = q;
    const hq = q !== 'rt';
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio * (hq ? 1.5 : 1), hq ? 3 : 2));
    this.studio.setShadowSize(hq ? 4096 : 2048);
    this.resize();
    if (q === 'pt' && !this.pt) {
      this.ptLoading = true;
      try { this.pt = await PathTrace.create(this.renderer); }
      finally { this.ptLoading = false; }
      this.configurePT();
    }
  }

  rotateLight(step = Math.PI / 4) {
    this.setRender({ envRotation: (this.settings.envRotation + step) % (Math.PI * 2) });
  }

  resize() {
    const p = this.canvas.parentElement;
    const w = Math.max(1, p.clientWidth), h = Math.max(1, p.clientHeight);
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
    const b = this.renderer.getDrawingBufferSize(new THREE.Vector2());
    this.hdr.setSize(b.x, b.y);
    this.mask.setSize(b.x, b.y);
    this.maskTexel.set(1 / b.x, 1 / b.y);
    if (this.pt) this.pt.pt.renderScale = this.quality === 'pt' ? Math.min(1, 1.25 / this.renderer.getPixelRatio()) : 1;
  }

  // Cadre l'objet depuis une direction (vues caméra), avec une transition douce.
  frameObject(obj, dir = null, instant = false) {
    const box = new THREE.Box3().setFromObject(obj);
    if (box.isEmpty()) return;
    const c = box.getCenter(new THREE.Vector3()), s = box.getSize(new THREE.Vector3());
    const d = dir ? dir.clone().normalize() : this.camera.position.clone().sub(this.controls.target).normalize();
    const fov = THREE.MathUtils.degToRad(this.camera.fov);
    const dist = Math.max(s.y / 2 / Math.tan(fov / 2), s.x / 2 / Math.tan(fov / 2) / this.camera.aspect) * 1.25 + s.z / 2;
    const to = c.clone().addScaledVector(d, dist);
    if (instant) { this.camera.position.copy(to); this.controls.target.copy(c); return; }
    this.camAnim = { t: 0, fromP: this.camera.position.clone(), toP: to, fromT: this.controls.target.clone(), toT: c };
  }

  // Signature de ce que voit le path tracer : si elle change, la scène est reconstruite quand elle se stabilise.
  signature() {
    let s = 0, n = 0;
    this.scene.traverseVisible(o => {
      if (!o.isMesh) return;
      n++;
      const e = o.matrixWorld.elements;
      let k = 0;
      for (let i = 0; i < 15; i++) k += e[i] * (i + 1.37);
      s += k * ((n % 13) + 1) + o.material.id * 0.013 + o.geometry.id * 0.007;
    });
    return s + n * 1000.5;
  }

  buildPT() {
    const hidden = [];
    this.scene.traverse(o => {
      if (o.isMesh && o.visible && (o.userData.helper || o.userData.rasterOnly)) { o.visible = false; hidden.push(o); }
    });
    this.ptFloor.visible = true;
    this.studio.beginPT();
    try { this.pt.build(this.scene, this.camera); }
    finally {
      this.studio.endPT();
      this.ptFloor.visible = false;
      for (const o of hidden) o.visible = true;
    }
  }

  // Path tracing : reconstruit après 0.25 s sans changement, sinon un échantillon de plus.
  stepPT() {
    const P = this.ptState, now = performance.now();
    const sig = this.signature() + this.studio.version * 0.5;
    if (sig !== P.sig) { P.sig = sig; P.changedAt = now; P.dirty = true; }
    if (P.dirty) {
      if (now - P.changedAt < 250 || this.camAnim) return null;
      this.configurePT();
      P.dirty = false;
      this.buildPT();
    }
    this.pt.settings.focus = this.camera.position.distanceTo(this.controls.target);
    this.pt.syncCamera(this.camera);
    return this.pt.sample();
  }

  // Masque de sélection : les meshes marqués outline en blanc, le reste en noir (occultation comprise).
  renderMask() {
    const swapped = [];
    let any = false;
    this.scene.traverseVisible(o => {
      if (!o.isMesh || o.userData.rasterOnly) return;
      swapped.push([o, o.material]);
      if (o.userData.helper) o.visible = false;
      else o.material = o.userData.outline ? WHITE : BLACK;
      any ||= !!o.userData.outline;
    });
    if (any) {
      const bg = this.scene.background, sm = this.renderer.shadowMap;
      sm.autoUpdate = false;
      this.scene.background = null;
      this.renderer.setRenderTarget(this.mask);
      this.renderer.setClearColor(0x000000, 1);
      this.renderer.clear();
      this.renderer.render(this.scene, this.camera);
      this.renderer.setClearColor(0x000000, 0);
      this.scene.background = bg;
      sm.autoUpdate = true;
    }
    for (const [o, m] of swapped) { o.material = m; o.visible = true; }
    return any;
  }

  frame() {
    const dt = Math.min(this.clock.getDelta(), 0.1);
    if (this.camAnim) {
      const a = this.camAnim;
      a.t = Math.min(1, a.t + dt * 2.5);
      const k = 1 - Math.pow(1 - a.t, 3);
      this.camera.position.lerpVectors(a.fromP, a.toP, k);
      this.controls.target.lerpVectors(a.fromT, a.toT, k);
      if (a.t >= 1) this.camAnim = null;
    }
    this.controls.update();
    if (this.onFrame) this.onFrame(dt);
    const r = this.renderer, s = this.settings;
    this.scene.updateMatrixWorld();

    r.setRenderTarget(this.hdr);
    r.render(this.scene, this.camera);
    const sel = this.renderMask();

    let out = null;
    if (this.quality === 'pt' && this.pt) out = this.stepPT();
    this.spp = out ? out.spp : 0;

    r.setRenderTarget(null);
    this.post.render(r, {
      color: out ? out.tex : this.hdr.texture,
      premult: !out,
      denoised: out?.denoised,
      keepRaw: out?.keepRaw,
      depth: this.hdr.depthTexture,
      mask: sel ? this.mask.texture : null,
      maskTexel: this.maskTexel,
      exposure: Math.pow(2, s.ev),
      vignette: s.vignette,
      mode: s.tonemapper,
      aspect: this.camera.aspect,
    });
    r.autoClear = false;
    r.render(this.overlay, this.camera);
    r.autoClear = true;
  }
}

// Petit moteur de vignettes : même studio et même passe finale, rendu hors écran en PNG.
export class Thumbs {
  constructor(w = 160, h = 200) {
    this.canvas = document.createElement('canvas');
    this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: false, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(1);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFShadowMap;
    this.renderer.toneMapping = THREE.NoToneMapping;
    this.renderer.setClearColor(0x000000, 0);
    this.scene = new THREE.Scene();
    this.studio = new Studio(this.scene, { shadowSize: 1024 });
    this.camera = new THREE.PerspectiveCamera(28, w / h, 0.01, 50);
    this.mat = new THREE.MeshPhysicalMaterial(RESIN);
    this.rt = new THREE.WebGLRenderTarget(w, h, { type: THREE.HalfFloatType, samples: 4 });
    this.post = new Post(0x232429, 0x18191c);
    this.mode = RENDER_DEFAULTS.tonemapper;
    this.setSize(w, h);
  }

  setSize(w, h) {
    this.w = w; this.h = h;
    this.renderer.setSize(w, h, false);
    this.rt.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  // obj : un Object3D déjà construit (les meshes peuvent être partagés, il n'est pas modifié).
  render(obj, dir = new THREE.Vector3(0.45, 0.25, 1), pad = 1.12) {
    const parent = obj.parent;
    this.scene.add(obj);
    obj.updateMatrixWorld(true);
    const box = new THREE.Box3().setFromObject(obj);
    const c = box.getCenter(new THREE.Vector3()), s = box.getSize(new THREE.Vector3());
    const fov = THREE.MathUtils.degToRad(this.camera.fov);
    const r = Math.max(s.y, s.x / this.camera.aspect, s.z * 0.8) / 2;
    const dist = r / Math.tan(fov / 2) * pad + Math.max(s.x, s.z) / 2;
    this.camera.position.copy(c).addScaledVector(dir.clone().normalize(), dist);
    this.camera.lookAt(c);
    // Les lumières visent toujours la pièce, centrée ou non sur la figurine.
    const off = c.clone().sub(new THREE.Vector3(0, 0.9, 0));
    const lights = [...this.studio.dir, this.studio.sun];
    for (const l of lights) { l.position.add(off); l.target.position.add(off); }
    this.renderer.setRenderTarget(this.rt);
    this.renderer.render(this.scene, this.camera);
    this.renderer.setRenderTarget(null);
    this.post.render(this.renderer, { color: this.rt.texture, exposure: 1, vignette: 0, mode: this.mode, aspect: this.camera.aspect });
    for (const l of lights) { l.position.sub(off); l.target.position.sub(off); }
    const url = this.canvas.toDataURL('image/png');
    this.scene.remove(obj);
    if (parent) parent.add(obj);
    return url;
  }
}
