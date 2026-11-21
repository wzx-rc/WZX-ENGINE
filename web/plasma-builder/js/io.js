// Import de STL dans la bibliothèque, export de la figurine assemblée en STL (millimètres).
import * as THREE from 'three';
import { STLLoader } from 'three/addons/loaders/STLLoader.js';
import { STLExporter } from 'three/addons/exporters/STLExporter.js';
import { mergeGeometries, mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';

// Lit un STL, le pose sur son centre (base en y = 0 pour un socle) et le met à la taille de la catégorie.
export async function readSTL(file, targetHeight, sitOnFloor = false) {
  const buf = await file.arrayBuffer();
  let g = new STLLoader().parse(buf);
  g.deleteAttribute('normal');
  g = mergeVertices(g, 1e-5);
  g.computeBoundingBox();
  const b = g.boundingBox, size = b.getSize(new THREE.Vector3()), c = b.getCenter(new THREE.Vector3());
  // Beaucoup de STL sont en Z vers le haut : on bascule si la pièce est nettement plus profonde que haute.
  if (size.z > size.y * 1.4) { g.rotateX(-Math.PI / 2); g.computeBoundingBox(); }
  g.computeBoundingBox();
  const b2 = g.boundingBox, s2 = b2.getSize(new THREE.Vector3()), c2 = b2.getCenter(new THREE.Vector3());
  g.translate(-c2.x, sitOnFloor ? -b2.max.y : -c2.y, -c2.z);
  const k = targetHeight / Math.max(s2.y, 1e-6);
  g.scale(k, k, k);
  g.computeVertexNormals();
  g.computeBoundingBox();
  return { geometry: g, tris: g.index ? g.index.count / 3 : g.attributes.position.count / 3, sourceSize: size };
}

// ZIP sans compression (STORE) : suffit pour regrouper des STL binaires.
const CRC = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(u8) { let c = 0xffffffff; for (let i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 0xff] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
export function makeZip(files) {
  const enc = new TextEncoder(), parts = [], central = [];
  let offset = 0;
  for (const f of files) {
    const name = enc.encode(f.name), data = ArrayBuffer.isView(f.data) ? new Uint8Array(f.data.buffer, f.data.byteOffset, f.data.byteLength) : new Uint8Array(f.data), crc = crc32(data);
    const h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint32(14, crc, true);
    h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, name.length, true);
    parts.push(h, name, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint32(16, crc, true);
    c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, name.length, true); c.setUint32(42, offset, true);
    central.push(c, name);
    offset += 30 + name.length + data.length;
  }
  const size = central.reduce((n, x) => n + x.byteLength, 0);
  const e = new DataView(new ArrayBuffer(22));
  e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true); e.setUint32(12, size, true); e.setUint32(16, offset, true);
  return new Blob([...parts, ...central, e], { type: 'application/zip' });
}

// Dans le viewer claude.ai, les fichiers passent par la capacité « downloads » (STL emballés en ZIP) ;
// ailleurs, lien de téléchargement classique.
let downloads;
async function viewerDownloads() {
  if (downloads === undefined) downloads = window.claude?.use ? await window.claude.use('downloads').catch(() => null) : null;
  return downloads;
}
async function save(files, zipName) {
  const dl = await viewerDownloads();
  if (dl) {
    const one = files.length === 1 && !/\.stl$/i.test(files[0].name);
    const data = one ? files[0].data : makeZip(files);
    try { await dl.save({ filename: one ? files[0].name : zipName + '.zip', data }); return true; }
    catch (err) { if (err && err.code === 'declined') return false; throw err; }
  }
  const blob = files.length === 1 ? new Blob([files[0].data]) : makeZip(files);
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = files.length === 1 ? files[0].name : zipName + '.zip';
  document.body.appendChild(a);
  a.click();
  // délai long : dans l'app Windows, le blob doit rester valide tant que la fenêtre « Enregistrer sous » est ouverte
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 120000);
  return true;
}

// Géométrie monde (pose courante) de tous les meshes d'un objet, mise en millimètres.
function bakedGeometry(objects, mmPerUnit) {
  const list = [];
  for (const o of objects) o.traverseVisible(m => {
    if (!m.isMesh || m.userData.noExport) return;
    const g = m.geometry.clone();
    g.applyMatrix4(m.matrixWorld);
    if (m.matrixWorld.determinant() < 0 && g.index) {
      const idx = g.index;
      for (let i = 0; i < idx.count; i += 3) { const a = idx.getX(i + 1); idx.setX(i + 1, idx.getX(i + 2)); idx.setX(i + 2, a); }
    }
    for (const k of Object.keys(g.attributes)) if (k !== 'position') g.deleteAttribute(k);
    list.push(g.index ? g : mergeVertices(g));
  });
  if (!list.length) return null;
  const merged = mergeGeometries(list, false);
  merged.scale(mmPerUnit, mmPerUnit, mmPerUnit);
  // Le socle repose sur le plateau d'impression : z = 0 en bas, Z vers le haut.
  merged.rotateX(Math.PI / 2);
  merged.computeBoundingBox();
  merged.translate(0, 0, -merged.boundingBox.min.z);
  merged.computeVertexNormals();
  return merged;
}

const exporter = new STLExporter();
function toSTL(geometry) { return exporter.parse(new THREE.Mesh(geometry), { binary: true }); }

export async function exportMerged(objects, mmPerUnit, name) {
  const g = bakedGeometry(objects, mmPerUnit);
  if (!g) return 0;
  await save([{ name: name + '.stl', data: toSTL(g) }], name);
  return g.index ? g.index.count / 3 : g.attributes.position.count / 3;
}

// Une pièce par emplacement, toutes dans un même ZIP. Chaque pièce est posée sur le plateau ;
// les clés d'assemblage (petits plots à l'articulation) sont ajoutées si demandé.
export async function exportParts(groups, mmPerUnit, name, keys = false) {
  const files = [];
  for (const { id, objects, keyAt } of groups) {
    const objs = [...objects];
    if (keys && keyAt) {
      const peg = new THREE.Mesh(new THREE.CylinderGeometry(0.018, 0.018, 0.06, 16));
      peg.position.copy(keyAt);
      peg.updateMatrixWorld(true);
      objs.push(peg);
    }
    const g = bakedGeometry(objs, mmPerUnit);
    if (g) files.push({ name: `${name}_${id}.stl`, data: toSTL(g) });
  }
  if (files.length) await save(files, name + '_pieces');
  return files.length;
}

export function exportJSON(data, name) {
  return save([{ name: name + '.plasmaork.json', data: new TextEncoder().encode(JSON.stringify(data, null, 2)) }], name);
}

// Un fichier tel quel (bibliothèque de briques, etc.).
export function saveFile(name, data) { return save([{ name, data }], name.replace(/\.[^.]+$/, '')); }
