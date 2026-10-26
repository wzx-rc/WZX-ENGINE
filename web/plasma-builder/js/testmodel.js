// Modèles de test du rendu (sculpts extraits par tools/mb_mesh) : version web allégée (~400 k triangles), l'app
// Windows charge la version HD et la texture 8k si elles sont livrées avec. Même échelle pour tous : celle qui met
// les yeux de l'Ork Boy à la hauteur des yeux de la figurine, pour garder les tailles relatives (un Grot est petit).
// - boy : BOY_32mm.mb, corps en UDIM 1001, tête / dents / yeux en UDIM 1002.
// - grot : GROT_UV.obj (Grot Bomber), une seule UDIM, diffuse MARI.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const SRC_EYE = 28.15;   // hauteur des yeux de l'Ork Boy, centimètres dans la scène Maya

export const TEST_MODELS = {
  boy: { label: 'Ork Boy', floor: -0.21, web: 'boy_32mm.glb', hd: 'boy_32mm_hd.glb',
    maps: [[1001, ['basecolor_4k.jpg']], [1002, ['basecolor_1002_4k.jpg']]] },
  grot: { label: 'Grot Bomber', floor: -0.088, web: 'grot.glb', hd: 'grot_hd.glb',
    maps: [[1001, ['grot_basecolor_8k.jpg', 'grot_basecolor_4k.jpg']]], desktopOnly: ['grot_basecolor_8k.jpg'] },
};

export class TestModel {
  constructor(vp, eyeHeight, id = 'boy') {
    this.vp = vp;
    this.id = id;
    this.def = TEST_MODELS[id];
    this.eyeHeight = eyeHeight;
    this.root = null;
    this.maps = { 1001: null, 1002: null };
    this.mats = {};
  }

  async load(desktop) {
    if (this.root) return this.root;
    const loader = new GLTFLoader();
    let gltf = null;
    const D = this.def, dir = 'assets/test/';
    if (desktop) gltf = await loader.loadAsync(dir + D.hd).catch(() => null);
    if (!gltf) gltf = await loader.loadAsync(dir + D.web).catch(() => null);
    // Hébergeurs qui ne servent pas le .glb (démo artifact) : copie en base64.
    if (!gltf) {
      const b64 = await (await fetch(dir + D.web + '.b64.txt')).text();
      const bin = Uint8Array.from(atob(b64.trim()), c => c.charCodeAt(0));
      gltf = await loader.parseAsync(bin.buffer, '');
    }
    const root = this.root = gltf.scene;
    const s = this.eyeHeight / SRC_EYE;
    root.scale.setScalar(s);
    root.position.y = -0.1 - D.floor * s;
    root.traverse(m => {
      if (!m.isMesh) return;
      m.castShadow = m.receiveShadow = true;
      m.userData.udim = m.material.name === 'udim1002' ? 1002 : 1001;
      m.userData.tag = m.name === 'eyes' ? 'eye' : 'skin';
    });
    // Textures livrées : la première trouvée par UDIM (8k réservée à l'app Windows).
    for (const [udim, files] of D.maps) {
      for (const f of files) {
        if (!desktop && D.desktopOnly?.includes(f)) continue;
        const t = await new THREE.TextureLoader().loadAsync(dir + f).catch(() => null);
        if (t) { this.setMap(udim, t); break; }
      }
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
