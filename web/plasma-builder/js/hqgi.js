// Mode « HQ BrickIt » : imitation WebGL du mode HQ de BrickIt natif (WZX-ENGINE, branche brickit-native).
// Le natif trace des rayons (DXR) ; WebGL n'en a pas, on reprend donc les mêmes idées avec du raster :
//
// - GI douce (DDGIUpdate / DDGIRaster.hlsl) : une grille de sondes bornée à la figurine (ork + briques + marge),
//   chaque sonde capture un petit cube de la scène éclairée (ombres, environnement, sol), projeté en harmoniques
//   sphériques L1 sur le GPU. Le raster lit l'éclairement interpolé entre les 8 sondes voisines à la place de
//   l'éclairement de l'environnement : le ciel est occulté dans les creux et la lumière rebondit d'une surface à
//   l'autre (les sondes voient la GI des captures précédentes, donc plusieurs rebonds au fil des passes).
//   Comme dans BrickIt : sondes progressives (quelques-unes par image), convergées puis laissées au repos (coût nul),
//   figées pendant qu'une pièce bouge et relancées à l'arrêt (réglage « Figer la GI pendant le déplacement »),
//   sondes enfermées dans un maillage écartées (test des faces arrière, comme la validité des sondes DDGI).
// - Occlusion HDRI : les reflets de l'environnement sont atténués par le rapport sondes / ciel libre.
// - SSAO (passe SSAO du HQ natif, flou 4 x 4) : occlusion de contact à l'échelle de l'écran, multipliée dans
//   l'image avant le tonemap.
import * as THREE from 'three';
import { FullScreenQuad } from 'three/addons/postprocessing/Pass.js';

const CH = THREE.ShaderChunk;
const MAX_PROBES = 640;

// Uniforms partagés par tous les matériaux branchés (une seule mise à jour pour toute la scène).
export const HQ_UNIFORMS = {
  uHqOn: { value: 0 },
  uHqAtlas: { value: null },
  uHqMin: { value: new THREE.Vector3() },
  uHqCell: { value: new THREE.Vector3(1, 1, 1) },
  uHqDims: { value: new THREE.Vector3(1, 1, 1) },
  uHqStrength: { value: 1 },
  uHqSpecOcc: { value: 0.85 },
};

const GI_PARS = /* glsl */`
uniform float uHqOn, uHqStrength, uHqSpecOcc;
uniform sampler2D uHqAtlas;
uniform vec3 uHqMin, uHqCell, uHqDims;
// Éclairement (π compris, comme getIBLIrradiance) interpolé entre les 8 sondes voisines.
// a = somme des poids : 0 si aucune sonde valide n'entoure le point.
vec4 hqIrradiance( vec3 p, vec3 n ) {
	vec3 g = clamp( ( p - uHqMin ) / uHqCell, vec3( 0.0 ), uHqDims - 1.0001 );
	vec3 b = floor( g ), f = g - b;
	vec3 sum = vec3( 0.0 );
	float wsum = 0.0;
	for ( int i = 0; i < 8; i++ ) {
		vec3 o = vec3( float( i & 1 ), float( ( i >> 1 ) & 1 ), float( ( i >> 2 ) & 1 ) );
		vec3 c = min( b + o, uHqDims - 1.0 );
		int idx = int( c.x + uHqDims.x * ( c.y + uHqDims.y * c.z ) );
		vec4 s0 = texelFetch( uHqAtlas, ivec2( idx, 0 ), 0 );
		if ( s0.a < 0.5 ) continue;
		vec3 tri = mix( 1.0 - f, f, o );
		float w = tri.x * tri.y * tri.z;
		// Poids « face à la sonde » de DDGI : une sonde derrière la surface compte peu (moins de fuites).
		vec3 toP = uHqMin + c * uHqCell - p;
		float d = length( toP );
		float facing = d > 1e-4 ? dot( toP / d, n ) * 0.5 + 0.5 : 1.0;
		w *= facing * facing + 0.03;
		vec3 s1 = texelFetch( uHqAtlas, ivec2( idx, 1 ), 0 ).rgb;
		vec3 s2 = texelFetch( uHqAtlas, ivec2( idx, 2 ), 0 ).rgb;
		vec3 s3 = texelFetch( uHqAtlas, ivec2( idx, 3 ), 0 ).rgb;
		sum += w * max( s0.rgb + s1 * n.y + s2 * n.z + s3 * n.x, vec3( 0.0 ) );
		wsum += w;
	}
	return vec4( wsum > 1e-5 ? sum / wsum : vec3( 0.0 ), wsum );
}
`;

const GI_APPLY = /* glsl */`
#if defined( RE_IndirectSpecular ) && defined( STANDARD )
if ( uHqOn > 0.5 ) {
	vec3 hqW = cameraPosition + ( vec4( geometryPosition, 0.0 ) * viewMatrix ).xyz;
	vec3 hqN = normalize( ( vec4( geometryNormal, 0.0 ) * viewMatrix ).xyz );
	vec4 hq = hqIrradiance( hqW, hqN );
	if ( hq.a > 1e-5 ) {
		const vec3 LUM = vec3( 0.2126, 0.7152, 0.0722 );
		float sky = dot( iblIrradiance, LUM );
		float occ = sky > 1e-4 ? clamp( dot( hq.rgb, LUM ) / sky, 0.0, 1.0 ) : 1.0;
		iblIrradiance = hq.rgb * uHqStrength;
		radiance *= mix( 1.0, occ, uHqSpecOcc );
		#ifdef USE_CLEARCOAT
			clearcoatRadiance *= mix( 1.0, occ, uHqSpecOcc );
		#endif
	}
}
#endif
`;

const hooked = new WeakSet();
// Branche la GI des sondes dans un matériau standard / physique (une fois, réglée ensuite par uHqOn).
export function hookMaterial(m) {
  if (!m || hooked.has(m) || !m.isMeshStandardMaterial) return;
  hooked.add(m);
  const prev = m.onBeforeCompile;
  m.onBeforeCompile = (shader, renderer) => {
    if (prev) prev(shader, renderer);
    Object.assign(shader.uniforms, HQ_UNIFORMS);
    // Le terme de cavité du temps réel (render.js) est remplacé par l'occlusion des sondes quand la GI est active.
    const end = CH.lights_fragment_end.replace('float cavity = mix(', 'float cavity = uHqOn > 0.5 ? 1.0 : mix(');
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', '#include <common>\n' + GI_PARS)
      .replace('#include <lights_fragment_end>', GI_APPLY + end);
  };
  const key = m.customProgramCacheKey.bind(m);
  m.customProgramCacheKey = () => key() + '|hqgi';
  m.needsUpdate = true;
}

// ---------- capture et projection des sondes ----------

// Faces avant / arrière : r = 1 face avant, g = 1 surface touchée (sondes enfermées dans un maillage).
const VIS_MAT = new THREE.ShaderMaterial({
  side: THREE.DoubleSide,
  vertexShader: /* glsl */`
#include <common>
#include <skinning_pars_vertex>
void main() {
	#include <skinbase_vertex>
	#include <begin_vertex>
	#include <skinning_vertex>
	#include <project_vertex>
}`,
  fragmentShader: 'void main() { gl_FragColor = vec4( gl_FrontFacing ? 1.0 : 0.0, 1.0, 0.0, 1.0 ); }',
});

// Projection d'un cube de radiance en L1, stockée déjà convoluée en éclairement :
// E(n) = s0 + s1 n.y + s2 n.z + s3 n.x, s0 = 1/4 ∫L, s1..3 = 1/2 ∫L ω.
const PROJ_FRAG = /* glsl */`
uniform samplerCube tCube;
uniform samplerCube tVis;
uniform float uRes, uVisRes, uBackMax;
vec3 faceDir( int f, vec2 uv ) {
	if ( f == 0 ) return vec3( 1.0, -uv.y, -uv.x );
	if ( f == 1 ) return vec3( -1.0, -uv.y, uv.x );
	if ( f == 2 ) return vec3( uv.x, 1.0, uv.y );
	if ( f == 3 ) return vec3( uv.x, -1.0, -uv.y );
	if ( f == 4 ) return vec3( uv.x, -uv.y, 1.0 );
	return vec3( -uv.x, -uv.y, -1.0 );
}
void main() {
	int k = int( gl_FragCoord.y );
	vec3 acc = vec3( 0.0 );
	float wsum = 0.0;
	int n = int( uRes );
	for ( int f = 0; f < 6; f++ )
		for ( int y = 0; y < 32; y++ ) {
			if ( y >= n ) break;
			for ( int x = 0; x < 32; x++ ) {
				if ( x >= n ) break;
				vec2 uv = ( vec2( x, y ) + 0.5 ) / uRes * 2.0 - 1.0;
				float t = 1.0 + dot( uv, uv );
				float dw = 1.0 / ( t * sqrt( t ) );
				vec3 d = normalize( faceDir( f, uv ) );
				vec3 L = textureLod( tCube, d, 0.0 ).rgb;
				float yk = k == 0 ? 0.5 : k == 1 ? d.y : k == 2 ? d.z : d.x;
				acc += L * yk * dw;
				wsum += dw;
			}
		}
	// wsum = somme des angles solides relatifs ; ∫ dω = 4π.
	acc *= 4.0 * HQ_PI / wsum * 0.5;
	float valid = 1.0;
	if ( k == 0 ) {
		float back = 0.0, tot = 0.0;
		int m = int( uVisRes );
		for ( int f = 0; f < 6; f++ )
			for ( int y = 0; y < 16; y++ ) {
				if ( y >= m ) break;
				for ( int x = 0; x < 16; x++ ) {
					if ( x >= m ) break;
					vec2 uv = ( vec2( x, y ) + 0.5 ) / uVisRes * 2.0 - 1.0;
					vec4 v = textureLod( tVis, normalize( faceDir( f, uv ) ), 0.0 );
					back += v.g * ( 1.0 - v.r );
					tot += 1.0;
				}
			}
		valid = back / tot > uBackMax ? 0.0 : 1.0;
	}
	gl_FragColor = vec4( acc, valid );
}`;

export class SoftGI {
  constructor(renderer) {
    this.r = renderer;
    this.res = 16;
    this.visRes = 8;
    this.cubeRT = new THREE.WebGLCubeRenderTarget(this.res, { type: THREE.HalfFloatType });
    this.cubeCam = new THREE.CubeCamera(0.005, 60, this.cubeRT);
    this.visRT = new THREE.WebGLCubeRenderTarget(this.visRes, { type: THREE.UnsignedByteType });
    this.visCam = new THREE.CubeCamera(0.005, 60, this.visRT);
    this.atlas = new THREE.WebGLRenderTarget(MAX_PROBES, 4, {
      type: THREE.HalfFloatType, minFilter: THREE.NearestFilter, magFilter: THREE.NearestFilter, depthBuffer: false,
    });
    this.proj = new FullScreenQuad(new THREE.ShaderMaterial({
      uniforms: { tCube: { value: this.cubeRT.texture }, tVis: { value: this.visRT.texture }, uRes: { value: this.res }, uVisRes: { value: this.visRes }, uBackMax: { value: 0.12 } },
      vertexShader: 'void main() { gl_Position = vec4( position.xy, 0.0, 1.0 ); }',
      fragmentShader: '#define HQ_PI 3.141592653589793\n' + PROJ_FRAG,
      depthTest: false, depthWrite: false,
    }));
    HQ_UNIFORMS.uHqAtlas.value = this.atlas.texture;
    this.dims = new THREE.Vector3(1, 1, 1);
    this.min = new THREE.Vector3();
    this.cell = new THREE.Vector3(1, 1, 1);
    this.count = 0;
    this.queue = [];           // sondes à capturer, dans l'ordre
    this.passes = 0;           // passes complètes restantes (rebonds)
    this.sig = null;
    this.changedAt = 0;
    this.freeze = true;
    this.perFrame = 6;
    this.cleared = false;
    this.stats = { probes: 0, valid: 0, state: 'vide' };
  }

  // Bornes de la GI : meshes visibles de la figurine + marge (comme les bornes aux briques du natif).
  bounds(scene) {
    const box = new THREE.Box3(), b = new THREE.Box3(), v = new THREE.Vector3();
    scene.traverseVisible(o => {
      if (!o.isMesh || o.userData.helper || o.userData.rasterOnly || o.userData.giIgnore) return;
      if (!o.geometry.boundingBox) o.geometry.computeBoundingBox();
      // Maillage skinné : boîte de la pose de liaison + positions des os (le skinning CPU de 1,7 M sommets serait
      // trop lent), la marge de la grille couvre le reste.
      b.copy(o.geometry.boundingBox).applyMatrix4(o.matrixWorld);
      box.union(b);
      if (o.isSkinnedMesh) for (const bone of o.skeleton.bones) box.expandByPoint(bone.getWorldPosition(v));
    });
    return box;
  }

  layout(box) {
    const size = box.getSize(new THREE.Vector3());
    const m = Math.max(size.x, size.y, size.z) * 0.12 + 0.02;
    box.expandByScalar(m);
    box.getSize(size);
    let cell = Math.max(size.x, size.y, size.z) / 9;
    let dims;
    for (;;) {
      dims = new THREE.Vector3(...['x', 'y', 'z'].map(k => Math.min(16, Math.max(2, Math.ceil(size[k] / cell) + 1))));
      if (dims.x * dims.y * dims.z <= MAX_PROBES) break;
      cell *= 1.15;
    }
    const cellV = new THREE.Vector3(size.x / (dims.x - 1), size.y / (dims.y - 1), size.z / (dims.z - 1));
    const same = dims.equals(this.dims) && box.min.distanceTo(this.min) < cell * 0.25 && cellV.distanceTo(this.cell) < cell * 0.1;
    if (same) return false;
    this.dims.copy(dims); this.min.copy(box.min); this.cell.copy(cellV);
    this.count = dims.x * dims.y * dims.z;
    HQ_UNIFORMS.uHqMin.value.copy(this.min);
    HQ_UNIFORMS.uHqCell.value.copy(this.cell);
    HQ_UNIFORMS.uHqDims.value.copy(this.dims);
    return true;
  }

  restart(scene, full) {
    const moved = this.layout(this.bounds(scene));
    if (moved || full) {
      // Nouvelle grille : l'atlas est remis à zéro (sondes invalides = environnement seul jusqu'à leur capture).
      if (moved) this.clear();
      this.queue = Array.from({ length: this.count }, (_, i) => i);
      this.passes = 3;
    }
  }

  clear() {
    const r = this.r, prev = r.getRenderTarget(), cc = r.getClearColor(new THREE.Color()), ca = r.getClearAlpha();
    r.setRenderTarget(this.atlas);
    r.setClearColor(0x000000, 0);
    r.clear(true, false, false);
    r.setRenderTarget(prev);
    r.setClearColor(cc, ca);
  }

  // Une image : relance si la scène a changé (après un temps de repos si la GI est figée), puis quelques sondes.
  // capture(fn) : le viewport prépare la scène pour les captures (sol, aides masquées, LOD bas…).
  update(scene, sig, withCapture) {
    const now = performance.now();
    if (sig !== this.sig) {
      const first = this.sig === null;
      this.sig = sig;
      this.changedAt = first ? -1e9 : now;
      this.pending = true;
    }
    const resting = now - this.changedAt > 300;
    if (this.pending && (resting || !this.freeze)) {
      this.pending = false;
      this.restart(scene, true);
    }
    if (!this.queue.length && this.passes > 1) { this.passes--; this.queue = Array.from({ length: this.count }, (_, i) => i); }
    if (!this.queue.length) { this.passes = 0; this.stats.state = 'au repos'; return; }
    if (this.freeze && !resting) { this.stats.state = 'figée'; return; }
    this.stats.state = `mise à jour · passe ${4 - this.passes}/3`;
    const batch = this.queue.splice(0, this.perFrame);
    withCapture(() => { for (const i of batch) this.captureProbe(scene, i); });
  }

  captureProbe(scene, i) {
    const d = this.dims, x = i % d.x, y = Math.floor(i / d.x) % d.y, z = Math.floor(i / (d.x * d.y));
    const p = new THREE.Vector3(x, y, z).multiply(this.cell).add(this.min);
    const r = this.r;
    this.cubeCam.position.copy(p);
    this.cubeCam.updateMatrixWorld();
    this.cubeCam.update(r, scene);
    const bg = scene.background;
    scene.background = null;
    scene.overrideMaterial = VIS_MAT;
    this.visCam.position.copy(p);
    this.visCam.updateMatrixWorld();
    this.visCam.update(r, scene);
    scene.overrideMaterial = null;
    scene.background = bg;
    const a = this.atlas;
    a.viewport.set(i, 0, 1, 4);
    a.scissor.set(i, 0, 1, 4);
    a.scissorTest = true;
    r.setRenderTarget(a);
    this.proj.render(r);
    a.scissorTest = false;
    a.viewport.set(0, 0, MAX_PROBES, 4);
    a.scissor.set(0, 0, MAX_PROBES, 4);
  }

  dispose() {
    this.cubeRT.dispose(); this.visRT.dispose(); this.atlas.dispose(); this.proj.dispose();
  }
}

// ---------- SSAO (demi-résolution, 12 prises, flou 4 x 4 guidé par la profondeur) ----------
const AO_FRAG = /* glsl */`
uniform sampler2D tDepth;
uniform mat4 uProj, uProjInv;
uniform vec2 uTexel;
uniform float uRadius;
varying vec2 vUv;
vec3 viewPos( vec2 uv ) {
	float z = texture2D( tDepth, uv ).r;
	vec4 p = uProjInv * vec4( uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0 );
	return p.xyz / p.w;
}
float ign( vec2 p ) { return fract( 52.9829189 * fract( dot( p, vec2( 0.06711056, 0.00583715 ) ) ) ); }
void main() {
	float z = texture2D( tDepth, vUv ).r;
	if ( z >= 1.0 ) { gl_FragColor = vec4( 1.0 ); return; }
	vec3 P = viewPos( vUv );
	// Normale par les plus petites différences (pas de halo sur les silhouettes).
	vec3 l = viewPos( vUv - vec2( uTexel.x, 0.0 ) ), r = viewPos( vUv + vec2( uTexel.x, 0.0 ) );
	vec3 d = viewPos( vUv - vec2( 0.0, uTexel.y ) ), u = viewPos( vUv + vec2( 0.0, uTexel.y ) );
	vec3 dx = abs( l.z - P.z ) < abs( r.z - P.z ) ? P - l : r - P;
	vec3 dy = abs( d.z - P.z ) < abs( u.z - P.z ) ? P - d : u - P;
	vec3 N = normalize( cross( dx, dy ) );
	float a0 = ign( gl_FragCoord.xy ) * 6.2831853;
	vec3 T = normalize( abs( N.z ) < 0.9 ? cross( N, vec3( 0.0, 0.0, 1.0 ) ) : cross( N, vec3( 1.0, 0.0, 0.0 ) ) );
	vec3 B = cross( N, T );
	float occ = 0.0;
	for ( int i = 0; i < 12; i++ ) {
		float fi = float( i );
		float ang = a0 + fi * 2.39996323;
		float rr = sqrt( ( fi + 0.5 ) / 12.0 );
		float h = sqrt( max( 0.0, 1.0 - rr * rr ) );
		vec3 s = P + ( T * cos( ang ) * rr + B * sin( ang ) * rr + N * h ) * uRadius * mix( 0.3, 1.0, fract( fi * 0.618 + a0 ) );
		vec4 c = uProj * vec4( s, 1.0 );
		vec2 suv = c.xy / c.w * 0.5 + 0.5;
		if ( any( lessThan( suv, vec2( 0.0 ) ) ) || any( greaterThan( suv, vec2( 1.0 ) ) ) ) continue;
		float sz = viewPos( suv ).z;
		float range = smoothstep( 0.0, 1.0, uRadius / max( abs( P.z - sz ), 1e-5 ) );
		occ += ( sz >= s.z + uRadius * 0.03 ? 1.0 : 0.0 ) * range;
	}
	gl_FragColor = vec4( vec3( 1.0 - occ / 12.0 ), 1.0 );
}`;

const BLUR_FRAG = /* glsl */`
uniform sampler2D tAO, tDepth;
uniform mat4 uProjInv;
uniform vec2 uTexel;
varying vec2 vUv;
float vz( vec2 uv ) {
	vec4 p = uProjInv * vec4( uv * 2.0 - 1.0, texture2D( tDepth, uv ).r * 2.0 - 1.0, 1.0 );
	return p.z / p.w;
}
void main() {
	float z0 = vz( vUv ), sum = 0.0, w = 0.0;
	for ( int y = -2; y < 2; y++ )
		for ( int x = -2; x < 2; x++ ) {
			vec2 uv = vUv + ( vec2( x, y ) + 0.5 ) * uTexel;
			float wi = 1.0 / ( 1.0 + abs( vz( uv ) - z0 ) * 40.0 );
			sum += texture2D( tAO, uv ).r * wi;
			w += wi;
		}
	gl_FragColor = vec4( vec3( sum / w ), 1.0 );
}`;

export class SSAO {
  constructor() {
    const opt = { type: THREE.HalfFloatType, depthBuffer: false };
    this.rt = new THREE.WebGLRenderTarget(1, 1, opt);
    this.blur = new THREE.WebGLRenderTarget(1, 1, opt);
    const vs = 'varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4( position.xy, 0.0, 1.0 ); }';
    this.ao = new FullScreenQuad(new THREE.ShaderMaterial({
      uniforms: { tDepth: { value: null }, uProj: { value: new THREE.Matrix4() }, uProjInv: { value: new THREE.Matrix4() }, uTexel: { value: new THREE.Vector2() }, uRadius: { value: 0.08 } },
      vertexShader: vs, fragmentShader: AO_FRAG, depthTest: false, depthWrite: false,
    }));
    this.bl = new FullScreenQuad(new THREE.ShaderMaterial({
      uniforms: { tAO: { value: this.rt.texture }, tDepth: { value: null }, uProjInv: { value: new THREE.Matrix4() }, uTexel: { value: new THREE.Vector2() } },
      vertexShader: vs, fragmentShader: BLUR_FRAG, depthTest: false, depthWrite: false,
    }));
  }

  setSize(w, h) {
    const hw = Math.max(1, w >> 1), hh = Math.max(1, h >> 1);
    this.rt.setSize(hw, hh);
    this.blur.setSize(hw, hh);
    this.ao.material.uniforms.uTexel.value.set(1 / w, 1 / h);
    this.bl.material.uniforms.uTexel.value.set(1 / hw, 1 / hh);
  }

  render(r, depth, camera, radius) {
    const a = this.ao.material.uniforms, b = this.bl.material.uniforms;
    a.tDepth.value = depth; b.tDepth.value = depth;
    a.uProj.value.copy(camera.projectionMatrix);
    a.uProjInv.value.copy(camera.projectionMatrixInverse);
    b.uProjInv.value.copy(camera.projectionMatrixInverse);
    a.uRadius.value = radius;
    r.setRenderTarget(this.rt);
    this.ao.render(r);
    r.setRenderTarget(this.blur);
    this.bl.render(r);
    return this.blur.texture;
  }

  dispose() { this.rt.dispose(); this.blur.dispose(); this.ao.dispose(); this.bl.dispose(); }
}
