// Rendu repris des dernières recherches du renderer Viewport3D (WZX-ENGINE src/vp3d) :
// - shaders/ToneCurves.hlsli + Tonemap.hlsl : exposition, vignettage, ACES (fit de Hill), AgX, AgX Punchy, Standard,
//   contour de sélection jaune ;
// - shaders/Raster.hlsl : ombre en 12 prises de Poisson, lobe spéculaire élargi par la taille des lumières de surface,
//   terme de cavité sur l'ambiant ;
// - app/MaterialPreview.cpp (Material Studio) : environnements dégradés Studio / Coucher de soleil / Entrepôt et
//   lumières key / fill / rim tournant avec l'environnement ;
// - EnvImage.cpp : ciel procédural ; RenderSettings::rasterEnvSun : le soleil d'un HDRI devient une lumière
//   directionnelle ombrée en temps réel (le path tracer garde l'image entière).
import * as THREE from 'three';
import { FullScreenQuad } from 'three/addons/postprocessing/Pass.js';
import { HDRLoader } from 'three/addons/loaders/HDRLoader.js';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';

// ---------- parité du shading raster ----------
const CH = THREE.ShaderChunk;

// Lumières de surface : aL = saturate(a + size / (3 d)) dans Raster.hlsl. Les lumières du studio ont une taille
// de 1.6 à 2.8 à 5 de distance, soit un élargissement moyen de 0.14 de l'alpha GGX.
{
  const head = 'void RE_Direct_Physical(';
  const [a, b] = CH.lights_physical_pars_fragment.split(head);
  CH.lights_physical_pars_fragment = a + `
#define VP3D_LOBE 0.14
PhysicalMaterial vp3dWiden( PhysicalMaterial m ) {
	m.roughness = sqrt( saturate( m.roughness * m.roughness + VP3D_LOBE ) );
	return m;
}
` + head + b.replace('BRDF_GGX( directLight.direction, geometryViewDir, geometryNormal, material )', 'BRDF_GGX( directLight.direction, geometryViewDir, geometryNormal, vp3dWiden( material ) )');
}

// Cavité : l'ambiant (irradiance et reflets de l'environnement) s'assombrit vers le bas et dans les creux.
CH.lights_fragment_end = `
#ifdef STANDARD
{
	vec3 vpW = cameraPosition + ( vec4( geometryPosition, 0.0 ) * viewMatrix ).xyz;
	vec3 vpN = ( vec4( geometryNormal, 0.0 ) * viewMatrix ).xyz;
	float cavity = mix( 0.55, 1.0, saturate( vpW.y * 0.6 + 0.5 + vpN.y * 0.3 ) );
	irradiance *= cavity;
	iblIrradiance *= cavity;
	#if defined( RE_IndirectSpecular )
		radiance *= cavity;
		#ifdef USE_CLEARCOAT
			clearcoatRadiance *= cavity;
		#endif
	#endif
}
#endif
` + CH.lights_fragment_end;

// Ombre : 12 prises de Poisson, rayon shadow.radius texels (2.5 dans le moteur), comparaison filtrée bilinéaire
// comme SampleCmpLevelZero.
{
  const s = CH.shadowmap_pars_fragment;
  const i0 = s.indexOf('#if defined( SHADOWMAP_TYPE_PCF )'), i1 = s.indexOf('#elif defined( SHADOWMAP_TYPE_PCF_SOFT )');
  const fn = s.indexOf('float getShadow(');
  if (i0 > 0 && i1 > i0 && fn > 0 && fn < i0) {
    CH.shadowmap_pars_fragment = s.slice(0, fn) + `
	float vp3dCmp( sampler2D depths, vec2 uv, float compare, vec2 texel ) {
		vec2 p = uv / texel - 0.5;
		vec2 f = fract( p );
		vec2 b = ( floor( p ) + 0.5 ) * texel;
		float a = texture2DCompare( depths, b, compare );
		float c = texture2DCompare( depths, b + vec2( texel.x, 0.0 ), compare );
		float d = texture2DCompare( depths, b + vec2( 0.0, texel.y ), compare );
		float e = texture2DCompare( depths, b + texel, compare );
		return mix( mix( a, c, f.x ), mix( d, e, f.x ), f.y );
	}
	` + s.slice(fn, i0) + `#if defined( SHADOWMAP_TYPE_PCF )
			vec2 texelSize = vec2( 1.0 ) / shadowMapSize;
			vec2 rad = texelSize * shadowRadius;
			shadow = (
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.326, -0.406 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.840, -0.074 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.696, 0.457 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.203, 0.621 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.962, -0.195 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.473, -0.480 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.519, 0.767 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.185, -0.893 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.507, 0.064 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( 0.896, 0.412 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.322, -0.933 ) * rad, shadowCoord.z, texelSize ) +
				vp3dCmp( shadowMap, shadowCoord.xy + vec2( -0.792, -0.598 ) * rad, shadowCoord.z, texelSize )
			) * ( 1.0 / 12.0 );
		` + s.slice(i1);
  }
}

// ---------- passe finale : Tonemap.hlsl ----------
export const TONEMAPPERS = [
  { id: 0, name: 'ACES' }, { id: 1, name: 'AgX' }, { id: 2, name: 'AgX Punchy' }, { id: 3, name: 'Standard' },
];

const POST_FRAG = /* glsl */`
uniform sampler2D tColor;
uniform sampler2D tDenoised;
uniform sampler2D tDepth;
uniform sampler2D tMask;
uniform float uExposure, uVignette, uAspect, uKeepRaw;
uniform bool uUseDenoise, uUseDepth, uUseMask, uPremult;
uniform int uMode;
uniform vec3 uBg0, uBg1;
uniform vec2 uMaskTexel;
varying vec2 vUv;

// ToneCurves.hlsli. Les float3x3 HLSL sont remplis par lignes, les mat3 GLSL par colonnes :
// mul(M, v) devient v * M et mul(v, M) devient M * v.
vec3 ACESFitted( vec3 x ) {
	const mat3 inM = mat3( 0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777 );
	const mat3 outM = mat3( 1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602 );
	x = x * inM;
	vec3 a = x * ( x + 0.0245786 ) - 0.000090537;
	vec3 b = x * ( 0.983729 * x + 0.4329510 ) + 0.238081;
	return clamp( ( a / b ) * outM, 0.0, 1.0 );
}
vec3 AgXContrast( vec3 x ) {
	vec3 x2 = x * x, x4 = x2 * x2;
	return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}
vec3 AgX( vec3 v, int look ) {
	const mat3 agxIn = mat3( 0.842479062253094, 0.0423282422610123, 0.0423756549057051,
		0.0784335999999992, 0.878468636469772, 0.0784336,
		0.0792237451477643, 0.0791661274605434, 0.879142973793104 );
	const mat3 agxOut = mat3( 1.19687900512017, -0.0528968517574562, -0.0529716355144438,
		-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
		-0.0990297440797205, -0.0989611768448433, 1.15107367264116 );
	const float minEv = -12.47393, maxEv = 4.026069;
	v = agxIn * max( v, 1e-10 );
	v = clamp( log2( v ), minEv, maxEv );
	v = ( v - minEv ) / ( maxEv - minEv );
	v = AgXContrast( v );
	if ( look == 1 ) {
		float luma = dot( v, vec3( 0.2126, 0.7152, 0.0722 ) );
		v = pow( max( v, 0.0 ), vec3( 1.35 ) );
		v = luma + 1.4 * ( v - luma );
	}
	v = agxOut * v;
	return clamp( pow( max( v, 0.0 ), vec3( 2.2 ) ), 0.0, 1.0 );
}
float ToSRGB1( float c ) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow( max( c, 0.0 ), 1.0 / 2.4 ) - 0.055; }
vec3 ToSRGB( vec3 c ) { return vec3( ToSRGB1( c.x ), ToSRGB1( c.y ), ToSRGB1( c.z ) ); }
bool sel( vec2 uv ) { return texture2D( tMask, uv ).r > 0.5; }

void main() {
	vec4 src = texture2D( tColor, vUv );
	if ( uUseDenoise ) src = mix( texture2D( tDenoised, vUv ), src, uKeepRaw );
	float a = clamp( src.a, 0.0, 1.0 );
	vec3 color = uPremult ? ( a > 1e-4 ? src.rgb / a : vec3( 0.0 ) ) : src.rgb;
	color *= uExposure;
	vec2 vc = vUv - 0.5;
	vc.x *= uAspect;
	float vig = 1.0 - uVignette * clamp( dot( vc, vc ) * 1.4, 0.0, 1.0 );
	color *= vig;
	vec3 mapped;
	if ( uMode == 1 ) mapped = AgX( color, 0 );
	else if ( uMode == 2 ) mapped = AgX( color, 1 );
	else if ( uMode == 3 ) mapped = clamp( color, 0.0, 1.0 );
	else mapped = ACESFitted( color );
	vec3 c = ToSRGB( mapped );
	// Fond de l'interface là où rien n'est rendu (environnement caché), déjà en sRGB.
	vec2 bc = vUv - vec2( 0.5, 0.4 );
	bc.x *= uAspect;
	vec3 bg = mix( uBg0, uBg1, clamp( length( bc ) * 1.3, 0.0, 1.0 ) );
	c = mix( bg, c, a );
	// Contour de sélection jaune, 2 px.
	if ( uUseMask ) {
		bool centerSel = sel( vUv );
		int hits = 0;
		for ( int dy = -2; dy <= 2; ++dy )
			for ( int dx = -2; dx <= 2; ++dx )
				if ( sel( vUv + vec2( dx, dy ) * uMaskTexel ) ) hits++;
		if ( !centerSel && hits > 0 ) c = mix( c, vec3( 1.0, 0.82, 0.1 ), 0.95 );
		else if ( centerSel && hits < 25 ) c = mix( c, vec3( 1.0, 0.86, 0.2 ), 0.6 );
		else if ( centerSel ) c = mix( c, vec3( 1.0, 0.9, 0.4 ), 0.08 );
	}
	gl_FragColor = vec4( c, 1.0 );
	gl_FragDepth = uUseDepth ? texture2D( tDepth, vUv ).r : 1.0;
}`;

const srgb = hex => new THREE.Color(hex).convertLinearToSRGB();

export class Post {
  constructor(bg0 = 0x1b1c21, bg1 = 0x101113) {
    this.mat = new THREE.ShaderMaterial({
      uniforms: {
        tColor: { value: null }, tDenoised: { value: null }, tDepth: { value: null }, tMask: { value: null },
        uExposure: { value: 1 }, uVignette: { value: 0.12 }, uAspect: { value: 1 }, uKeepRaw: { value: 1 },
        uUseDenoise: { value: false }, uUseDepth: { value: false }, uUseMask: { value: false }, uPremult: { value: true },
        uMode: { value: 0 }, uBg0: { value: srgb(bg0) }, uBg1: { value: srgb(bg1) }, uMaskTexel: { value: new THREE.Vector2() },
      },
      vertexShader: 'varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4( position.xy, 0.0, 1.0 ); }',
      fragmentShader: POST_FRAG,
      depthTest: true, depthWrite: true, depthFunc: THREE.AlwaysDepth,
    });
    this.quad = new FullScreenQuad(this.mat);
  }

  setBackground(bg0, bg1) { this.mat.uniforms.uBg0.value = srgb(bg0); this.mat.uniforms.uBg1.value = srgb(bg1); }

  // o : { color, premult, denoised, keepRaw, depth, mask, maskTexel, exposure, vignette, mode, aspect }
  render(renderer, o) {
    const u = this.mat.uniforms;
    u.tColor.value = o.color;
    u.uPremult.value = o.premult !== false;
    u.uUseDenoise.value = !!o.denoised;
    u.tDenoised.value = o.denoised || null;
    u.uKeepRaw.value = o.keepRaw ?? 1;
    u.uUseDepth.value = !!o.depth;
    u.tDepth.value = o.depth || null;
    u.uUseMask.value = !!o.mask;
    u.tMask.value = o.mask || null;
    if (o.maskTexel) u.uMaskTexel.value.copy(o.maskTexel);
    u.uExposure.value = o.exposure;
    u.uVignette.value = o.vignette;
    u.uMode.value = o.mode;
    u.uAspect.value = o.aspect;
    this.quad.render(renderer);
  }
}

// ---------- environnements ----------
const lin = (r, g, b) => new THREE.Vector3(r, g, b);
export const ENVIRONMENTS = [
  { id: 'studio', name: 'Studio', top: lin(0.32, 0.34, 0.38), horizon: lin(0.18, 0.19, 0.21), bottom: lin(0.04, 0.04, 0.045), lights: true },
  { id: 'sunset', name: 'Coucher de soleil', top: lin(0.25, 0.30, 0.45), horizon: lin(0.95, 0.55, 0.30), bottom: lin(0.10, 0.07, 0.05), lights: true },
  { id: 'warehouse', name: 'Entrepôt', top: lin(0.45, 0.45, 0.42), horizon: lin(0.30, 0.29, 0.27), bottom: lin(0.08, 0.08, 0.08), lights: true },
  { id: 'sky', name: 'Ciel procédural', lights: false },
  { id: 'hdri', name: 'HDRI…', lights: false },
];

// Image équirectangulaire RGBA float, rangée comme un fichier HDR (première ligne = zénith, flipY).
function equirect(w, h, fn) {
  const data = new Float32Array(w * h * 4), d = new THREE.Vector3();
  for (let y = 0; y < h; y++) {
    const lat = (0.5 - (y + 0.5) / h) * Math.PI;
    for (let x = 0; x < w; x++) {
      const phi = ((x + 0.5) / w - 0.5) * 2 * Math.PI;
      d.set(Math.cos(lat) * Math.cos(phi), Math.sin(lat), Math.cos(lat) * Math.sin(phi));
      const c = fn(d), o = (y * w + x) * 4;
      data[o] = c.x; data[o + 1] = c.y; data[o + 2] = c.z; data[o + 3] = 1;
    }
  }
  return texture(data, w, h);
}
function texture(data, w, h) {
  const t = new THREE.DataTexture(data, w, h, THREE.RGBAFormat, THREE.FloatType);
  t.mapping = THREE.EquirectangularReflectionMapping;
  t.colorSpace = THREE.LinearSRGBColorSpace;
  t.minFilter = t.magFilter = THREE.LinearFilter;
  t.flipY = true;
  t.generateMipmaps = false;
  t.needsUpdate = true;
  return t;
}

// Dégradé de Raster.hlsl (EnvGradient) : vers le haut pow 0.6, vers le bas pow 0.4.
function gradientEnv(p) {
  const c = new THREE.Vector3();
  return equirect(256, 128, d => d.y > 0
    ? c.lerpVectors(p.horizon, p.top, Math.pow(d.y, 0.6))
    : c.lerpVectors(p.horizon, p.bottom, Math.pow(-d.y, 0.4)));
}

// makeProceduralSky (EnvImage.cpp) : soleil à 40° de haut, disque de 1° de rayon, ~3 d'irradiance.
function skyEnv() {
  const se = THREE.MathUtils.degToRad(40), sa = THREE.MathUtils.degToRad(145);
  const sun = new THREE.Vector3(Math.cos(se) * Math.sin(sa), Math.sin(se), -Math.cos(se) * Math.cos(sa));
  const cosSun = Math.cos(THREE.MathUtils.degToRad(1));
  const sunL = new THREE.Vector3(1, 0.93, 0.82).multiplyScalar(3200);
  const zenith = lin(0.18, 0.32, 0.72), horizon = lin(0.85, 0.82, 0.78), ground = lin(0.10, 0.09, 0.08);
  const c = new THREE.Vector3(), glow = lin(1, 0.8, 0.55);
  return equirect(1024, 512, d => {
    if (d.y >= 0) {
      c.lerpVectors(horizon, zenith, Math.pow(d.y, 0.45)).multiplyScalar(0.9);
      const cs = d.dot(sun);
      c.addScaledVector(glow, 0.6 * Math.pow(Math.max(cs, 0), 48));
      if (cs > cosSun) c.copy(sunL);
    } else {
      c.copy(horizon).multiplyScalar(0.35).lerp(ground, Math.min(1, -d.y * 4));
    }
    return c;
  });
}

export async function loadHDRI(file) {
  const url = URL.createObjectURL(file);
  try {
    const loader = /\.exr$/i.test(file.name) ? new EXRLoader() : new HDRLoader();
    loader.setDataType(THREE.FloatType);
    const t = await loader.loadAsync(url);
    t.mapping = THREE.EquirectangularReflectionMapping;
    t.name = file.name;
    return t;
  } finally { URL.revokeObjectURL(url); }
}

// rasterEnvSun : isole le soleil d'une image (disque très au-dessus de la moyenne), renvoie sa direction (repère
// de l'image) et son irradiance, et une copie de l'image sans le disque pour l'éclairage ambiant temps réel.
function extractSun(tex) {
  const { data, width: w, height: h } = tex.image;
  if (!data || !(data instanceof Float32Array)) return { env: tex, sun: null };
  const ch = data.length / (w * h);
  const lum = i => 0.2126 * data[i] + 0.7152 * data[i + 1] + 0.0722 * data[i + 2];
  let max = 0, maxAt = 0, sum = 0, wsum = 0;
  for (let y = 0; y < h; y++) {
    const cl = Math.cos((0.5 - (y + 0.5) / h) * Math.PI);
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * ch, l = lum(i);
      sum += l * cl; wsum += cl;
      if (l > max) { max = l; maxAt = y * w + x; }
    }
  }
  const mean = sum / wsum;
  if (!(max > 50 * mean && max > 8)) return { env: tex, sun: null };
  const cut = Math.max(max * 0.05, mean * 20);
  const dir = (x, y, v) => { const lat = (0.5 - (y + 0.5) / h) * Math.PI, phi = ((x + 0.5) / w - 0.5) * 2 * Math.PI; return v.set(Math.cos(lat) * Math.cos(phi), Math.sin(lat), Math.cos(lat) * Math.sin(phi)); };
  const center = dir(maxAt % w, Math.floor(maxAt / w), new THREE.Vector3());
  const out = new Float32Array(data), E = new THREE.Vector3(), D = new THREE.Vector3(), v = new THREE.Vector3();
  const dA = (2 * Math.PI / w) * (Math.PI / h), cosMax = Math.cos(THREE.MathUtils.degToRad(6));
  const fill = [0, 0, 0];
  for (let y = 0; y < h; y++) {
    const cl = Math.cos((0.5 - (y + 0.5) / h) * Math.PI);
    for (let x = 0; x < w; x++) {
      if (dir(x, y, v).dot(center) < cosMax) continue;
      const i = (y * w + x) * ch;
      if (lum(i) < cut) { fill[0] = data[i]; fill[1] = data[i + 1]; fill[2] = data[i + 2]; continue; }
      const k = dA * cl;
      E.x += data[i] * k; E.y += data[i + 1] * k; E.z += data[i + 2] * k;
      D.addScaledVector(v, lum(i) * k);
      out[i] = -1;  // marqué, rempli ensuite
    }
  }
  for (let i = 0; i < out.length; i += ch) if (out[i] === -1) { out[i] = fill[0]; out[i + 1] = fill[1]; out[i + 2] = fill[2]; }
  const env = texture(ch === 4 ? out : toRGBA(out, w, h, ch), w, h);
  const irr = 0.2126 * E.x + 0.7152 * E.y + 0.0722 * E.z;
  return { env, sun: { dir: D.normalize(), irradiance: irr, color: new THREE.Color(E.x / irr, E.y / irr, E.z / irr) } };
}
function toRGBA(src, w, h, ch) {
  const o = new Float32Array(w * h * 4);
  for (let i = 0; i < w * h; i++) { o[i * 4] = src[i * ch]; o[i * 4 + 1] = src[i * ch + 1]; o[i * 4 + 2] = src[i * ch + 2]; o[i * 4 + 3] = 1; }
  return o;
}

// ---------- studio : environnement + lumières ----------
const TARGET = new THREE.Vector3(0, 0.9, 0);
// Material Studio : azimut, élévation, taille, couleur, intensité (radiance) ; distance 5 de la cible.
const STUDIO_LIGHTS = [
  { name: 'key', az: -40, el: 35, size: 2.2, color: [1.0, 0.95, 0.88], k: 7, shadow: true },
  { name: 'fill', az: 55, el: 15, size: 2.8, color: [0.80, 0.88, 1.0], k: 4 * 0.65 },  // fillIntensity 0.65 par défaut
  { name: 'rim', az: 170, el: 30, size: 1.6, color: [0.90, 0.95, 1.0], k: 9 },
];
const DIST = 5;

export class Studio {
  constructor(scene, opts = {}) {
    this.scene = scene;
    this.envs = {};
    this.hdri = null;
    this.state = { env: 'studio', intensity: 1, rotation: 0, visible: false, lights: true, lightScale: 1 };
    // Temps réel : une directionnelle par lumière (irradiance E = L·A / d², ce que fait Raster.hlsl pour une lumière
    // de surface vue de la cible) ; la key porte l'ombre. Le soleil d'un ciel / HDRI prend la place de la key.
    this.dir = STUDIO_LIGHTS.map(l => {
      const d = new THREE.DirectionalLight(new THREE.Color().setRGB(...l.color), l.k * l.size * l.size / (DIST * DIST));
      d.target.position.copy(TARGET);
      d.userData.def = l;
      if (l.shadow) {
        d.castShadow = true;
        const s = d.shadow;
        s.mapSize.set(opts.shadowSize || 2048, opts.shadowSize || 2048);
        s.camera.left = s.camera.bottom = -2.6;
        s.camera.right = s.camera.top = 2.6;
        s.camera.near = 1;
        s.camera.far = 14;
        s.bias = -0.0003;
        s.normalBias = 0.03;
        s.radius = 2.5;
      }
      scene.add(d, d.target);
      return d;
    });
    this.sun = new THREE.DirectionalLight(0xffffff, 3);
    this.sun.castShadow = true;
    Object.assign(this.sun.shadow, { bias: -0.0003, normalBias: 0.03, radius: 2.5 });
    this.sun.shadow.mapSize.set(opts.shadowSize || 2048, opts.shadowSize || 2048);
    Object.assign(this.sun.shadow.camera, { left: -2.6, bottom: -2.6, right: 2.6, top: 2.6, near: 1, far: 14 });
    this.sun.target.position.copy(TARGET);
    scene.add(this.sun, this.sun.target);
    // Path tracing : vraies lumières de surface (invisibles à la caméra, comme visibleToCamera = false).
    this.rect = STUDIO_LIGHTS.map(l => new THREE.RectAreaLight(new THREE.Color().setRGB(...l.color), l.k, l.size, l.size));
    this.apply();
  }

  envData() {
    const id = this.state.env;
    if (id === 'hdri') return this.hdri;
    if (!this.envs[id]) {
      const p = ENVIRONMENTS.find(e => e.id === id);
      const src = p.top ? gradientEnv(p) : skyEnv();
      this.envs[id] = { ...extractSun(src), full: src };
    }
    return this.envs[id];
  }

  async setHDRI(file) {
    const t = await loadHDRI(file);
    const r = extractSun(t);
    if (this.hdri) { this.hdri.env.dispose(); if (this.hdri.full !== this.hdri.env) this.hdri.full.dispose(); }
    this.hdri = { ...r, full: t, name: file.name };
    this.state.env = 'hdri';
    this.state.lights = false;
    this.apply();
  }

  set(patch) {
    if (patch.env && patch.env !== this.state.env) {
      const p = ENVIRONMENTS.find(e => e.id === patch.env);
      if (p && !('lights' in patch)) patch.lights = p.lights;
    }
    Object.assign(this.state, patch);
    this.apply();
  }

  // Environnement pour le path tracer : l'image complète, soleil compris.
  fullEnv() { return this.envData()?.full || null; }

  apply() {
    const st = this.state, scene = this.scene, d = this.envData();
    const rot = st.rotation;
    scene.environment = d ? d.env : null;
    scene.environmentIntensity = st.intensity * (st.env === 'hdri' || st.env === 'sky' ? 1 : 0.6);
    scene.environmentRotation.set(0, rot, 0);
    scene.backgroundRotation.set(0, rot, 0);
    scene.backgroundIntensity = scene.environmentIntensity;
    scene.background = st.visible && d ? this.fullEnv() : null;
    const Y = new THREE.Vector3(0, 1, 0);
    STUDIO_LIGHTS.forEach((l, i) => {
      const az = THREE.MathUtils.degToRad(l.az), el = THREE.MathUtils.degToRad(l.el);
      const p = new THREE.Vector3(Math.sin(az) * Math.cos(el), Math.sin(el), Math.cos(az) * Math.cos(el)).applyAxisAngle(Y, rot).multiplyScalar(DIST).add(TARGET);
      const dl = this.dir[i];
      dl.position.copy(p);
      dl.intensity = l.k * l.size * l.size / (DIST * DIST) * st.lightScale;
      dl.visible = st.lights;
      const rl = this.rect[i];
      rl.position.copy(p);
      rl.lookAt(TARGET);
      rl.intensity = l.k * st.lightScale;
    });
    const sun = d && d.sun;
    this.sun.visible = !!sun;
    if (sun) {
      this.sun.position.copy(sun.dir).applyAxisAngle(Y, rot).multiplyScalar(6).add(TARGET);
      this.sun.color.copy(sun.color);
      this.sun.intensity = sun.irradiance * scene.environmentIntensity;
    }
    this.version = (this.version || 0) + 1;
  }

  // Bascule de la scène pour la construction du path tracer, puis retour au temps réel.
  beginPT() {
    const s = this.scene;
    this._saved = { env: s.environment, dir: this.dir.map(d => d.visible), sun: this.sun.visible };
    s.environment = this.fullEnv();
    for (const d of this.dir) d.visible = false;
    this.sun.visible = false;
    if (this.state.lights) for (const r of this.rect) s.add(r);
  }
  endPT() {
    const s = this.scene, sv = this._saved;
    s.environment = sv.env;
    this.dir.forEach((d, i) => { d.visible = sv.dir[i]; });
    this.sun.visible = sv.sun;
    for (const r of this.rect) r.removeFromParent();
  }

  setShadowSize(n) {
    for (const l of [...this.dir, this.sun]) {
      if (!l.castShadow) continue;
      l.shadow.mapSize.set(n, n);
      if (l.shadow.map) { l.shadow.map.dispose(); l.shadow.map = null; }
    }
  }
}
