// Viewport réaliste : PBR, environnement studio, ombres douces, tonemapping AgX.
// Reprend les réglages du viewport WZX (raster temps réel + mode haute qualité).
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';

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

function radialTexture() {
  const c = document.createElement('canvas');
  c.width = c.height = 256;
  const g = c.getContext('2d');
  const grd = g.createRadialGradient(128, 128, 0, 128, 128, 128);
  grd.addColorStop(0, 'rgba(46,48,53,1)');
  grd.addColorStop(0.55, 'rgba(30,31,35,0.9)');
  grd.addColorStop(1, 'rgba(17,18,20,0)');
  g.fillStyle = grd;
  g.fillRect(0, 0, 256, 256);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

function lights(scene) {
  const key = new THREE.DirectionalLight(0xfff3e6, 2.6);
  key.position.set(2.2, 4.2, 2.6);
  key.castShadow = true;
  key.shadow.mapSize.set(2048, 2048);
  key.shadow.camera.left = key.shadow.camera.bottom = -1.8;
  key.shadow.camera.right = key.shadow.camera.top = 1.8;
  key.shadow.camera.near = 0.5;
  key.shadow.camera.far = 12;
  key.shadow.bias = -0.0004;
  key.shadow.normalBias = 0.02;
  key.shadow.radius = 4;
  scene.add(key, key.target);
  const rim = new THREE.DirectionalLight(0xcfe0ff, 1.4);
  rim.position.set(-2.5, 3, -3);
  scene.add(rim);
  scene.add(new THREE.HemisphereLight(0xdfe6ee, 0x15161a, 0.35));
  return key;
}

export class Viewport {
  constructor(canvas) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.AgXToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x111214);
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.55;
    this.key = lights(this.scene);
    this.lightAngle = Math.atan2(2.6, 2.2);

    const floor = new THREE.Mesh(new THREE.CircleGeometry(3.2, 64), new THREE.MeshBasicMaterial({ map: radialTexture(), transparent: true, depthWrite: false }));
    floor.rotation.x = -Math.PI / 2;
    floor.position.y = -0.101;
    floor.renderOrder = -1;
    this.scene.add(floor);
    const shadow = new THREE.Mesh(new THREE.PlaneGeometry(6, 6), new THREE.ShadowMaterial({ opacity: 0.55 }));
    shadow.rotation.x = -Math.PI / 2;
    shadow.position.y = -0.1;
    shadow.receiveShadow = true;
    this.scene.add(shadow);
    this.grid = new THREE.GridHelper(4, 40, 0x3a3c42, 0x26282d);
    this.grid.position.y = -0.099;
    this.grid.visible = false;
    this.scene.add(this.grid);

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
    this.scene.add(this.gizmo.getHelper ? this.gizmo.getHelper() : this.gizmo);

    this.mode = 'resin';
    this.materials = new Map();
    this.quality = 'rt';
    this.clock = new THREE.Clock();
    this.onFrame = null;
    this.camAnim = null;

    new ResizeObserver(() => this.resize()).observe(canvas.parentElement);
    this.resize();
    this.renderer.setAnimationLoop(() => this.frame());
  }

  material(tag, selected) {
    const key = this.mode + ':' + tag + ':' + (selected ? 1 : 0);
    if (!this.materials.has(key)) {
      const base = this.mode === 'paint' ? (PAINT[tag] || PAINT.imported) : (tag === 'eye' ? { ...RESIN } : RESIN);
      const m = new THREE.MeshPhysicalMaterial(base);
      if (this.mode === 'wire') m.wireframe = true;
      if (selected) { m.emissive = new THREE.Color(0xf0435a); m.emissiveIntensity = 0.16; }
      this.materials.set(key, m);
    }
    return this.materials.get(key);
  }

  setMode(mode) { this.mode = mode; }

  setQuality(q) {
    this.quality = q;
    const hq = q === 'hq';
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio * (hq ? 1.5 : 1), hq ? 3 : 2));
    this.key.shadow.mapSize.set(hq ? 4096 : 2048, hq ? 4096 : 2048);
    this.key.shadow.radius = hq ? 7 : 4;
    if (this.key.shadow.map) { this.key.shadow.map.dispose(); this.key.shadow.map = null; }
    this.scene.environmentIntensity = hq ? 0.7 : 0.55;
    this.resize();
  }

  rotateLight(step = Math.PI / 4) {
    this.lightAngle += step;
    const r = Math.hypot(2.2, 2.6);
    this.key.position.set(Math.cos(this.lightAngle) * r, 4.2, Math.sin(this.lightAngle) * r);
  }

  resize() {
    const p = this.canvas.parentElement;
    const w = Math.max(1, p.clientWidth), h = Math.max(1, p.clientHeight);
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
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
    this.renderer.render(this.scene, this.camera);
  }
}

// Petit moteur de vignettes : même éclairage, rendu hors écran en PNG.
export class Thumbs {
  constructor(w = 160, h = 200) {
    this.canvas = document.createElement('canvas');
    this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: true, preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(1);
    this.renderer.setSize(w, h, false);
    this.renderer.toneMapping = THREE.AgXToneMapping;
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x1b1c20);
    const pmrem = new THREE.PMREMGenerator(this.renderer);
    this.scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
    this.scene.environmentIntensity = 0.6;
    const key = new THREE.DirectionalLight(0xfff3e6, 2.4);
    key.position.set(2, 3, 3);
    const rim = new THREE.DirectionalLight(0xcfe0ff, 1.2);
    rim.position.set(-2, 2, -3);
    this.scene.add(key, rim, new THREE.HemisphereLight(0xdfe6ee, 0x15161a, 0.3));
    this.camera = new THREE.PerspectiveCamera(28, w / h, 0.01, 50);
    this.mat = new THREE.MeshPhysicalMaterial(RESIN);
    this.w = w; this.h = h;
  }

  setSize(w, h) { this.w = w; this.h = h; this.renderer.setSize(w, h, false); this.camera.aspect = w / h; this.camera.updateProjectionMatrix(); }

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
    this.renderer.render(this.scene, this.camera);
    const url = this.canvas.toDataURL('image/png');
    this.scene.remove(obj);
    if (parent) parent.add(obj);
    return url;
  }
}
