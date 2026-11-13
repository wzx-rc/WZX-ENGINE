// Meshes de l'ork remplaçables (Rig Lab > Meshes) : on garde le rig et ses sockets, on change le maillage.
// Un OBJ exporté de Maya (espace monde, cm, Y en haut, l'ork à sa place d'origine) remplace un slot :
// corps low / high, tête low / high, yeux, dents. Ses poids viennent du maillage d'origine du même morceau
// (corps : le BODY_low riggé dans Maya ; tête, yeux, dents : leur maillage rigide sur Head) par le point le plus
// proche sur la surface et le mélange barycentrique des poids des trois sommets du triangle (la technique du
// transfert low → high de mb2glb). Le résultat (géométrie + poids) est enregistré sur l'appareil (IndexedDB) et
// appliqué au chargement de l'ork dans toutes les pages (OrkModel.load).
import * as THREE from 'three';
import { MeshBVH } from 'three-mesh-bvh';

export const SLOTS = [
  { id: 'BODY_low', label: 'Corps low', from: 'BODY_low' },
  { id: 'BODY_high', label: 'Corps high', from: 'BODY_low' },
  { id: 'HEAD_low', label: 'Tête low', from: 'HEAD_low' },
  { id: 'HEAD_high', label: 'Tête high', from: 'HEAD_low' },
  { id: 'EYES', label: 'Yeux', from: 'EYES' },
  { id: 'TEETH', label: 'Dents', from: 'TEETH' },
];

// ------------------------------------------------------------ stockage
const DB = 'plasmaork-rig', STORE = 'meshes';
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
async function tx(mode, fn) {
  const d = await db();
  return new Promise((res, rej) => {
    const t = d.transaction(STORE, mode), s = t.objectStore(STORE);
    const r = fn(s);
    t.oncomplete = () => res(r?.result);
    t.onerror = () => rej(t.error);
  });
}
export const slotStore = {
  all: () => tx('readonly', s => s.getAll()).catch(() => []),
  put: rec => tx('readwrite', s => s.put(rec)),
  delete: id => tx('readwrite', s => s.delete(id)),
};

// ------------------------------------------------------------ application au modèle
// Garde une copie des géométries d'origine (source des poids, et pour revenir en arrière).
function keepOriginals(model) {
  if (model.originals) return;
  model.originals = {};
  for (const [n, m] of Object.entries(model.meshes)) model.originals[n] = m.geometry;
}
function geometryFrom(rec) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(rec.position, 3));
  if (rec.normal) g.setAttribute('normal', new THREE.BufferAttribute(rec.normal, 3));
  if (rec.uv) g.setAttribute('uv', new THREE.BufferAttribute(rec.uv, 2));
  g.setAttribute('skinIndex', new THREE.BufferAttribute(rec.skinIndex, 4));
  g.setAttribute('skinWeight', new THREE.BufferAttribute(rec.skinWeight, 4));
  if (rec.index) g.setIndex(new THREE.BufferAttribute(rec.index, 1));
  if (!rec.normal) g.computeVertexNormals();
  g.computeBoundingBox(); g.computeBoundingSphere();
  return g;
}
export function setSlotGeometry(model, id, geo) {
  keepOriginals(model);
  const m = model.meshes[id];
  if (!m) return;
  m.geometry = geo || model.originals[id];
  if (id === 'BODY_low') {
    const lw = m.geometry;
    model.lowSkin = { index: lw.attributes.skinIndex, weight: lw.attributes.skinWeight };
  }
}
// Appelé par OrkModel.load : applique les meshes enregistrés. Sans IndexedDB, l'ork d'origine reste.
export async function applySlots(model) {
  keepOriginals(model);
  let list = [];
  try { list = await slotStore.all(); } catch { /* stockage indisponible */ }
  model.slots = {};
  for (const rec of list) {
    if (!model.meshes[rec.id]) continue;
    try { setSlotGeometry(model, rec.id, geometryFrom(rec)); model.slots[rec.id] = { file: rec.file, date: rec.date, verts: rec.position.length / 3 }; }
    catch (err) { console.error('mesh ' + rec.id, err); }
  }
}

// ------------------------------------------------------------ import d'un OBJ dans un slot
// Matrice espace Maya (= espace de la scène GLB, cm) -> espace local de la géométrie du mesh.
function mayaToMesh(model, mesh) {
  model.root.updateMatrixWorld(true);
  const rel = model.root.matrixWorld.clone().invert().multiply(mesh.matrixWorld);
  return rel.invert();
}
// Unités : si le maillage est 10, 100 ou 2,54 fois trop grand ou petit par rapport à l'original, on corrige.
function unitFix(newH, oldH) {
  const r = newH / oldH;
  if (!(r > 0) || Math.abs(Math.log(r)) < Math.log(3)) return 1;
  let best = 1, bd = Infinity;
  for (const f of [100, 10, 0.1, 0.01, 2.54, 1 / 2.54]) { const d = Math.abs(Math.log(r / f)); if (d < bd) { bd = d; best = f; } }
  return 1 / best;
}

// geo : BufferGeometry de l'OBJ (positions en cm, espace monde Maya). Rend { rec, geo, scaled, maxDist }.
export function transferToSlot(model, slotId, srcGeo, file) {
  keepOriginals(model);
  const slot = SLOTS.find(s => s.id === slotId);
  const mesh = model.meshes[slotId];
  const src = model.originals[slot.from];
  let g = srcGeo.index ? srcGeo.toNonIndexed() : srcGeo.clone();
  // soudure des sommets (l'OBJ arrive par faces) pour un maillage lisse et des poids continus
  g.deleteAttribute('normal');
  g = mergeVertices(g);
  g.applyMatrix4(mayaToMesh(model, mesh));
  // unités : comparaison avec le maillage d'origine du slot
  g.computeBoundingBox();
  const ob = new THREE.Box3().setFromBufferAttribute(model.originals[slotId].attributes.position);
  const fix = unitFix(g.boundingBox.max.y - g.boundingBox.min.y, ob.max.y - ob.min.y);
  if (fix !== 1) g.scale(fix, fix, fix);
  // poids : point le plus proche sur la source (espace local de la source), mélange barycentrique
  const srcMesh = model.meshes[slot.from];
  const toSrc = mayaToMesh(model, srcMesh).multiply(mayaToMesh(model, mesh).invert());
  const sg = new THREE.BufferGeometry();
  sg.setAttribute('position', src.attributes.position);
  sg.setIndex(src.index ? src.index.clone() : new THREE.BufferAttribute(Uint32Array.from({ length: src.attributes.position.count }, (_, i) => i), 1));
  const bvh = new MeshBVH(sg);
  const idx = sg.index.array, sp = src.attributes.position, si = src.attributes.skinIndex, sw = src.attributes.skinWeight;
  const pos = g.attributes.position, n = pos.count;
  const skinIndex = new Uint16Array(n * 4), skinWeight = new Float32Array(n * 4);
  const p = new THREE.Vector3(), a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), w = new THREE.Vector3(), hit = {};
  const acc = new Map();
  let maxDist = 0;
  for (let i = 0; i < n; i++) {
    p.fromBufferAttribute(pos, i).applyMatrix4(toSrc);
    if (!bvh.closestPointToPoint(p, hit)) continue;
    maxDist = Math.max(maxDist, hit.distance);
    const f = hit.faceIndex * 3, tri = [idx[f], idx[f + 1], idx[f + 2]];
    a.fromBufferAttribute(sp, tri[0]); b.fromBufferAttribute(sp, tri[1]); c.fromBufferAttribute(sp, tri[2]);
    THREE.Triangle.getBarycoord(hit.point, a, b, c, w);
    acc.clear();
    tri.forEach((v, k) => {
      const bw = Math.max(0, w.getComponent(k));
      for (let j = 0; j < 4; j++) {
        const wt = sw.getComponent(v, j);
        if (wt > 0) { const bi = si.getComponent(v, j); acc.set(bi, (acc.get(bi) || 0) + wt * bw); }
      }
    });
    const top = [...acc].sort((x, y) => y[1] - x[1]).slice(0, 4);
    const sum = top.reduce((s, x) => s + x[1], 0) || 1;
    top.forEach(([bi, wt], j) => { skinIndex[i * 4 + j] = bi; skinWeight[i * 4 + j] = wt / sum; });
    if (!top.length) skinWeight[i * 4] = 1;
  }
  g.computeVertexNormals();
  const rec = {
    id: slotId, file, date: Date.now(),
    position: new Float32Array(pos.array),
    normal: new Float32Array(g.attributes.normal.array),
    uv: g.attributes.uv ? new Float32Array(g.attributes.uv.array) : null,
    index: g.index ? new Uint32Array(g.index.array) : null,
    skinIndex, skinWeight,
  };
  return { rec, geo: geometryFrom(rec), scaled: fix, maxDist };
}

// Soudure simple par position + uv (BufferGeometryUtils.mergeVertices sans dépendance de plus).
function mergeVertices(g, tol = 1e-4) {
  const pos = g.attributes.position, uv = g.attributes.uv;
  const map = new Map(), out = [], outUV = [], index = [];
  const q = 1 / tol;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
    const key = Math.round(x * q) + ',' + Math.round(y * q) + ',' + Math.round(z * q) + (uv ? ',' + Math.round(uv.getX(i) * 1e4) + ',' + Math.round(uv.getY(i) * 1e4) : '');
    let k = map.get(key);
    if (k === undefined) { k = out.length / 3; map.set(key, k); out.push(x, y, z); if (uv) outUV.push(uv.getX(i), uv.getY(i)); }
    index.push(k);
  }
  const r = new THREE.BufferGeometry();
  r.setAttribute('position', new THREE.Float32BufferAttribute(out, 3));
  if (uv) r.setAttribute('uv', new THREE.Float32BufferAttribute(outUV, 2));
  r.setIndex(out.length / 3 > 65535 ? new THREE.Uint32BufferAttribute(index, 1) : new THREE.Uint16BufferAttribute(index, 1));
  return r;
}

// OBJ du slot tel qu'il est (espace monde Maya, cm) : la référence à reprendre dans Maya.
export function slotToOBJ(model, slotId) {
  const mesh = model.meshes[slotId], g = mesh.geometry;
  const m = mayaToMesh(model, mesh).invert();
  const pos = g.attributes.position, uv = g.attributes.uv, v = new THREE.Vector3();
  const lines = [`# Plasma Orks Builder · ${slotId} (cm, Y en haut, repère de la scène Maya)`, `o ${slotId}`];
  for (let i = 0; i < pos.count; i++) { v.fromBufferAttribute(pos, i).applyMatrix4(m); lines.push(`v ${v.x.toFixed(4)} ${v.y.toFixed(4)} ${v.z.toFixed(4)}`); }
  if (uv) for (let i = 0; i < uv.count; i++) lines.push(`vt ${uv.getX(i).toFixed(5)} ${uv.getY(i).toFixed(5)}`);
  const idx = g.index ? g.index.array : [...Array(pos.count).keys()];
  for (let i = 0; i < idx.length; i += 3) {
    const f = [idx[i] + 1, idx[i + 1] + 1, idx[i + 2] + 1];
    lines.push('f ' + f.map(k => (uv ? `${k}/${k}` : k)).join(' '));
  }
  return lines.join('\n') + '\n';
}
