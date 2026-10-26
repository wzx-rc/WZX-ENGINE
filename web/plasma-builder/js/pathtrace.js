// Mode « Path tracing » : rendu progressif avec three-gpu-pathtracer, réglé comme le PathTrace de Viewport3D
// (rebonds, filtre glossy, profondeur de champ en lentille mince, débruitage mélangé à l'image brute selon
// keepRaw = saturate((spp - 64) / 768) de Tonemap.hlsl). Chargé à la demande : le temps réel n'en dépend pas.
import * as THREE from 'three';
import { FullScreenQuad } from 'three/addons/postprocessing/Pass.js';

export class PathTrace {
  static async create(renderer) {
    const m = await import('three-gpu-pathtracer');
    return new PathTrace(renderer, m);
  }

  constructor(renderer, m) {
    this.renderer = renderer;
    const pt = this.pt = new m.WebGLPathTracer(renderer);
    pt.renderToCanvas = false;
    pt.rasterizeScene = false;
    pt.minSamples = 1;
    pt.renderDelay = 0;
    pt.fadeDuration = 0;
    pt.tiles.set(1, 1);
    pt.multipleImportanceSampling = true;
    this.cam = new m.PhysicalCamera();
    this.dnMat = new m.DenoiseMaterial({ map: null, blending: THREE.NoBlending });
    this.dnQuad = new FullScreenQuad(this.dnMat);
    this.dnRT = new THREE.WebGLRenderTarget(1, 1, { type: THREE.HalfFloatType });
    this.maxSamples = 2048;
    this.ready = false;
    this.camKey = '';
  }

  // settings : bounces, filterGlossy, dof, fStop, unitsPerMeter, focus (distance)
  configure(s) {
    this.pt.bounces = s.bounces;
    this.pt.filterGlossyFactor = s.filterGlossy;
    this.denoise = s.denoise;
    this.settings = s;
  }

  build(scene, camera) {
    this.syncCamera(camera, true);
    this.pt.setScene(scene, this.cam);
    this.ready = true;
  }

  syncCamera(camera, force = false) {
    const s = this.settings || {};
    const key = camera.matrixWorld.elements.join(',') + camera.projectionMatrix.elements[0] + camera.projectionMatrix.elements[5] + [s.dof, s.fStop, s.focus, s.unitsPerMeter].join(',');
    if (!force && key === this.camKey) return;
    this.camKey = key;
    const c = this.cam;
    c.copy(camera);
    // PhysicalCamera : bokehSize = focale (mm) / f, converti en mètres par le path tracer ; la scène est une
    // figurine (unitsPerMeter unités pour un mètre), comme Camera::apertureRadius avec sensorScale.
    c.fStop = s.fStop || 8;
    c.apertureBlades = 6;
    c.apertureRotation = 0;
    c.anamorphicRatio = 1;
    c.focusDistance = s.focus || 5;
    c.bokehSize = s.dof ? c.getFocalLength() / c.fStop * (s.unitsPerMeter || 1) : 0;
    if (this.ready) this.pt.updateCamera();
  }

  reset() { this.pt.reset(); }

  get samples() { return this.pt.samples; }

  // Un échantillon de plus ; renvoie la texture à afficher (et la version débruitée) ou null.
  sample() {
    if (!this.ready) return null;
    this.pt.pausePathTracing = this.pt.samples >= this.maxSamples;
    this.pt.renderSample();
    this.renderer.setRenderTarget(null);
    const spp = this.pt.samples;
    if (spp < 1) return null;
    const tex = this.pt.target.texture;
    let denoised = null;
    if (this.denoise && spp < 832) {
      const t = this.pt.target;
      if (this.dnRT.width !== t.width || this.dnRT.height !== t.height) this.dnRT.setSize(t.width, t.height);
      this.dnMat.map = tex;
      this.renderer.setRenderTarget(this.dnRT);
      this.dnQuad.render(this.renderer);
      this.renderer.setRenderTarget(null);
      denoised = this.dnRT.texture;
    }
    return { tex, denoised, keepRaw: Math.min(Math.max((spp - 64) / 768, 0), 1), spp };
  }
}
