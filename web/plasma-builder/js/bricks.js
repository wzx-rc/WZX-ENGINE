// Briques de la bibliothèque : format, stockage (IndexedDB du navigateur) et construction des objets 3D.
//
// Une brique est rangée telle qu'elle sort de Maya, dans l'espace de la scène (cm, Y en haut) :
//   { id, name, cat, tags, created, updated,
//     lods: { low: [geo], high: [geo] },        géométries (high facultatif : le low sert pour les deux)
//     texture, mask : Blob | null, flipY,        basecolor et masque de couleur de faction (réservé)
//     plug: { type, m },                         point de la brique qui vient sur le socket (matrice 4x4)
//     home,                                      socket de l'ork par défaut (ex. grip_R)
//     placement: 'inplace' | 'plug' | 'origin',  d'où vient le plug (voir importer.js)
//     sockets: [{ name, type, m }],              sockets offerts par la brique (ex. un dos qui porte un étendard)
//     thumb, stats, source }
// geo = { name, position, normal, uv, index } (tableaux typés).
import * as THREE from 'three';
import { makeZip } from './io.js';

export const CATEGORIES = [
  { id: 'head', label: 'Têtes', type: 'head', home: 'head', icon: 'M12 3a8 8 0 0 0-8 8v3l2 2v4h12v-4l2-2v-3a8 8 0 0 0-8-8zM9 12h.01M15 12h.01M10 20v-3M14 20v-3' },
  { id: 'weapon', label: 'Armes', type: 'grip', home: 'grip_R', icon: 'M4 20l6-6M8 14l2 2M14 4l6 6-8 8-6-6z' },
  { id: 'back', label: 'Dos', type: 'back', home: 'back', icon: 'M7 4h10v16H7zM7 9h10M10 4V2h4v2' },
  { id: 'shoulder', label: 'Épaulières', type: 'shoulder', home: 'shoulder_L', icon: 'M4 14c0-5 4-9 9-9h3l4 4v3c0 5-4 8-9 8H8z' },
  { id: 'chest', label: 'Torse', type: 'chest', home: 'chest', icon: 'M12 3l8 3v6c0 5-4 8-8 9-4-1-8-4-8-9V6z' },
  { id: 'belt', label: 'Ceinture', type: 'belt', home: 'belt_R', icon: 'M6 8h12v12H6zM9 8V5h6v3M6 12h12' },
  { id: 'base', label: 'Socles', type: 'base', home: 'base', icon: 'M3 17l9 4 9-4M3 17l9-4 9 4M3 17v-3l9-4 9 4v3' },
];
export const catById = id => CATEGORIES.find(c => c.id === id) || CATEGORIES[0];

// ---------------------------------------------------------------- IndexedDB
const DB = 'plasmaork', STORE = 'bricks';
let dbp = null;
function db() {
  dbp ||= new Promise((res, rej) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE, { keyPath: 'id' });
    r.onsuccess = () => res(r.result);
    r.onerror = () => rej(r.error);
  });
  return dbp;
}
function tx(mode, fn) {
  return db().then(d => new Promise((res, rej) => {
    const t = d.transaction(STORE, mode), s = t.objectStore(STORE);
    const req = fn(s);
    t.oncomplete = () => res(req?.result);
    t.onerror = () => rej(t.error);
  }));
}
export const Store = {
  all: () => tx('readonly', s => s.getAll()).then(l => (l || []).sort((a, b) => a.created - b.created)),
  get: id => tx('readonly', s => s.get(id)),
  put: rec => tx('readwrite', s => s.put(rec)),
  delete: id => tx('readwrite', s => s.delete(id)),
};
export const newId = () => 'b_' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);

// ---------------------------------------------------------------- géométries
export function geoToRec(g, name = '') {
  const a = g.attributes;
  return {
    name,
    position: new Float32Array(a.position.array),
    normal: a.normal ? new Float32Array(a.normal.array) : null,
    uv: a.uv ? new Float32Array(a.uv.array) : null,
    index: g.index ? (g.index.array.length > 65535 ? new Uint32Array(g.index.array) : new Uint16Array(g.index.array)) : null,
  };
}
export function recToGeo(r) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(r.position, 3));
  if (r.normal) g.setAttribute('normal', new THREE.BufferAttribute(r.normal, 3));
  if (r.uv) g.setAttribute('uv', new THREE.BufferAttribute(r.uv, 2));
  if (r.index) g.setIndex(new THREE.BufferAttribute(r.index, 1));
  if (!r.normal) g.computeVertexNormals();
  g.computeBoundingSphere();
  return g;
}
export const triCount = geos => geos.reduce((n, r) => n + (r.index ? r.index.length : r.position.length / 3) / 3, 0);
export const mat4 = a => new THREE.Matrix4().fromArray(a);

// ---------------------------------------------------------------- objets 3D
// Géométries et textures construites une fois par brique (et par version de la brique).
const cache = new Map();
function built(rec) {
  const key = rec.id + ':' + (rec.updated || rec.created);
  if (!cache.has(key)) {
    let texture = null;
    if (rec.texture) {
      const img = new Image();
      texture = new THREE.Texture(img);
      img.onload = () => { texture.needsUpdate = true; };
      img.src = URL.createObjectURL(rec.texture);
      texture.colorSpace = THREE.SRGBColorSpace;
      texture.flipY = rec.flipY !== false;
      texture.anisotropy = 8;
    }
    cache.set(key, { low: rec.lods.low.map(recToGeo), high: rec.lods.high?.length ? rec.lods.high.map(recToGeo) : null, texture, mats: new Map() });
  }
  return cache.get(key);
}

// Matériau d'une brique : celui du viewport (résine, filaire), ou la basecolor en mode Peint.
function brickMaterial(b, vp) {
  const base = vp.material('imported');
  if (vp.mode !== 'paint' || !b.texture) return base;
  const key = base.uuid;
  if (!b.mats.has(key)) {
    const m = base.clone();
    m.map = b.texture; m.color.set(0xffffff); m.roughness = 0.6;
    b.mats.set(key, m);
  }
  return b.mats.get(key);
}

// Groupe des meshes de la brique, dans l'espace de la brique (cm). lod : 'low' | 'high'.
export function brickMeshes(rec, lod, vp, tag = {}) {
  const b = built(rec);
  const geos = lod === 'high' && b.high ? b.high : b.low;
  const g = new THREE.Group();
  for (const geo of geos) {
    const m = new THREE.Mesh(geo, vp ? brickMaterial(b, vp) : undefined);
    m.castShadow = m.receiveShadow = true;
    m.userData = { ...tag };
    g.add(m);
  }
  return g;
}
export function refreshBrickMaterials(root, vp, recOf) {
  root.traverse(m => { if (m.isMesh && m.userData.brick && !m.userData.helper) m.material = brickMaterial(built(recOf(m.userData.brick)), vp); });
}

// ---------------------------------------------------------------- export / import de la bibliothèque
// Un ZIP : library.json (les briques sans les données binaires) + un fichier par tableau / image.
// Sert à passer les briques du navigateur à l'app Windows (chacun a son propre stockage) ou à les sauvegarder.
export async function packLibrary(recs) {
  const files = [], list = [];
  const add = (name, data) => { files.push({ name, data }); return name; };
  for (const r of recs) {
    const dir = `bricks/${r.id}/`;
    const lods = {};
    for (const [lod, geos] of Object.entries(r.lods)) {
      lods[lod] = (geos || []).map((g, i) => {
        const o = { name: g.name };
        for (const k of ['position', 'normal', 'uv', 'index']) if (g[k]) o[k] = { file: add(`${dir}${lod}${i}_${k}.bin`, g[k]), type: g[k].constructor.name };
        return o;
      });
    }
    const blob = async (b, n) => b ? { file: add(dir + n, new Uint8Array(await b.arrayBuffer())), type: b.type } : null;
    list.push({ ...r, lods, texture: await blob(r.texture, 'basecolor'), mask: await blob(r.mask, 'mask') });
  }
  files.unshift({ name: 'library.json', data: new TextEncoder().encode(JSON.stringify({ app: 'plasma-ork-builder', kind: 'bricks', version: 1, bricks: list })) });
  return makeZip(files);
}

// Lecture d'un ZIP non compressé (celui de packLibrary).
function readZip(buf) {
  const v = new DataView(buf), out = {};
  let end = buf.byteLength - 22;
  while (end >= 0 && v.getUint32(end, true) !== 0x06054b50) end--;
  if (end < 0) throw new Error('ZIP illisible');
  let p = v.getUint32(end + 16, true);
  const n = v.getUint16(end + 10, true), dec = new TextDecoder();
  for (let i = 0; i < n; i++) {
    const method = v.getUint16(p + 10, true), size = v.getUint32(p + 20, true), nl = v.getUint16(p + 28, true);
    const xl = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true), off = v.getUint32(p + 42, true);
    const name = dec.decode(new Uint8Array(buf, p + 46, nl));
    if (method !== 0) throw new Error('ZIP compressé non pris en charge : ' + name);
    const start = off + 30 + v.getUint16(off + 26, true) + v.getUint16(off + 28, true);
    out[name] = buf.slice(start, start + size);
    p += 46 + nl + xl + cl;
  }
  return out;
}
const TYPED = { Float32Array, Uint32Array, Uint16Array };
export function unpackLibrary(buf) {
  const files = readZip(buf);
  const meta = JSON.parse(new TextDecoder().decode(files['library.json']));
  if (meta.kind !== 'bricks') throw new Error('pas une bibliothèque de briques');
  return meta.bricks.map(r => {
    const lods = {};
    for (const [lod, geos] of Object.entries(r.lods)) {
      lods[lod] = geos.map(g => {
        const o = { name: g.name, position: null, normal: null, uv: null, index: null };
        for (const k of ['position', 'normal', 'uv', 'index']) if (g[k]) o[k] = new TYPED[g[k].type](files[g[k].file]);
        return o;
      });
    }
    const blob = b => b ? new Blob([files[b.file]], { type: b.type }) : null;
    return { ...r, lods, texture: blob(r.texture), mask: blob(r.mask) };
  });
}
