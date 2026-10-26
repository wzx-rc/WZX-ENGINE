// Modèle de test du rendu : l'Ork Boy 32 mm sculpté (BOY_32mm.mb, extrait par tools/mb_mesh), corps en UDIM 1001,
// tête / dents / yeux en UDIM 1002. Version web allégée (~400 k triangles) ; l'app Windows charge la version HD si
// elle est livrée avec. Mis à l'échelle du rig : les yeux à la hauteur des yeux de la figurine.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const SRC_EYE = 28.15, SRC_FLOOR = -0.21;   // centimètres dans la scène Maya

export class TestModel {
  constructor(vp, eyeHeight) {
    this.vp = vp;
    this.eyeHeight = eyeHeight;
    this.root = null;
    this.maps = { 1001: null, 1002: null };
    this.mats = {};
  }

  async load(desktop) {
    if (this.root) return this.root;
    const loader = new GLTFLoader();
    let gltf = null;
    if (desktop) gltf = await loader.loadAsync('assets/test/boy_32mm_hd.glb').catch(() => null);
    if (!gltf) gltf = await loader.loadAsync('assets/test/boy_32mm.glb').catch(() => null);
    // Hébergeurs qui ne servent pas le .glb (démo artifact) : copie en base64.
    if (!gltf) {
      const b64 = await (await fetch('assets/test/boy_32mm.glb.b64.txt')).text();
      const bin = Uint8Array.from(atob(b64.trim()), c => c.charCodeAt(0));
      gltf = await loader.parseAsync(bin.buffer, '');
    }
    const root = this.root = gltf.scene;
    const s = this.eyeHeight / SRC_EYE;
    root.scale.setScalar(s);
    root.position.y = -0.1 - SRC_FLOOR * s;
    root.traverse(m => {
      if (!m.isMesh) return;
      m.castShadow = m.receiveShadow = true;
      m.userData.udim = m.material.name === 'udim1002' ? 1002 : 1001;
      m.userData.tag = m.name === 'eyes' ? 'eye' : 'skin';
    });
    // Texture livrée avec l'app (basecolor_4k.jpg = UDIM 1001, basecolor_1002_4k.jpg pour la tête si présente).
    for (const [udim, url] of [[1001, 'assets/test/basecolor_4k.jpg'], [1002, 'assets/test/basecolor_1002_4k.jpg']]) {
      const t = await new THREE.TextureLoader().loadAsync(url).catch(() => null);
      if (t) this.setMap(udim, t);
    }
    this.refresh();
    return root;
  }

  setMap(udim, tex) {
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.flipY = false;
    tex.anisotropy = 8;
    this.maps[udim] = tex;
    this.refresh();
  }

  async loadMapFile(file) {
    const url = URL.createObjectURL(file);
    try {
      const t = await new THREE.TextureLoader().loadAsync(url);
      const udim = /1002|head|tete|tête/i.test(file.name) ? 1002 : 1001;
      this.setMap(udim, t);
      return udim;
    } finally { URL.revokeObjectURL(url); }
  }

  // Résine : gris du builder ; Peint : texture basecolor si chargée ; Filaire.
  refresh() {
    if (!this.root) return;
    const mode = this.vp.mode;
    this.root.traverse(m => {
      if (!m.isMesh) return;
      const map = mode === 'paint' ? this.maps[m.userData.udim] : null;
      const key = mode + ':' + m.userData.udim + ':' + m.userData.tag + ':' + (map ? map.uuid : '');
      if (!this.mats[key]) {
        const base = this.vp.material(mode === 'paint' && !map ? m.userData.tag : 'imported');
        const mat = base.clone();
        if (map) { mat.map = map; mat.color.set(0xffffff); mat.roughness = 0.6; mat.sheen = 0; }
        this.mats[key] = mat;
      }
      m.material = this.mats[key];
    });
  }
}
