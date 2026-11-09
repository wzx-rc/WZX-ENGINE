// Simplification UV-préservante (meshoptimizer) puis écriture d'un GLB (positions, normales, uv, indices uint32).
import { MeshoptSimplifier } from 'meshoptimizer';
import fs from 'fs';
const [,, ratioArg, outFile] = process.argv;
const ratio = +ratioArg;
const meta = JSON.parse(fs.readFileSync('out/meta.json'));


await MeshoptSimplifier.ready;
MeshoptSimplifier.useExperimentalFeatures = true;
const prims = [];
for (const m of meta) {
  const pos = new Float32Array(fs.readFileSync(`out/${m.name}.pos`).buffer.slice(0));
  const uv = new Float32Array(fs.readFileSync(`out/${m.name}.uv`).buffer.slice(0));
  let idx = new Uint32Array(fs.readFileSync(`out/${m.name}.idx`).buffer.slice(0));
  const r = m.tris > 50000 ? ratio : Math.min(1, ratio * 4);
  if (r < 1) {
    const target = Math.floor(idx.length * r / 3) * 3;
    const [res, err] = MeshoptSimplifier.simplifyWithAttributes(idx, pos, 3, uv, 2, [0.5, 0.5], null, target, 0.05, []);
    console.error(m.name, idx.length / 3, '->', res.length / 3, 'err', err.toFixed(5));
    idx = res;
  }
  // compactage
  const remap = new Int32Array(pos.length / 3).fill(-1); let n = 0;
  for (let i = 0; i < idx.length; i++) if (remap[idx[i]] < 0) remap[idx[i]] = n++;
  const P = new Float32Array(n * 3), U = new Float32Array(n * 2), I = new Uint32Array(idx.length);
  const off = [0, 0, 0];
  for (let v = 0; v < remap.length; v++) { const k = remap[v]; if (k < 0) continue; for (let c = 0; c < 3; c++) P[k * 3 + c] = pos[v * 3 + c] + off[c]; U[k * 2] = uv[v * 2]; U[k * 2 + 1] = 1 - uv[v * 2 + 1]; }
  for (let i = 0; i < idx.length; i++) I[i] = remap[idx[i]];
  // normales lissées par position (les coutures UV ne cassent pas l'ombrage)
  const N = new Float32Array(n * 3), key = new Map(), group = new Int32Array(n);
  for (let v = 0; v < n; v++) { const k = P[v * 3] + ',' + P[v * 3 + 1] + ',' + P[v * 3 + 2]; if (!key.has(k)) key.set(k, v); group[v] = key.get(k); }
  for (let i = 0; i < I.length; i += 3) {
    const a = I[i] * 3, b = I[i + 1] * 3, c = I[i + 2] * 3;
    const ux = P[b] - P[a], uy = P[b + 1] - P[a + 1], uz = P[b + 2] - P[a + 2], vx = P[c] - P[a], vy = P[c + 1] - P[a + 1], vz = P[c + 2] - P[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    for (const q of [I[i], I[i + 1], I[i + 2]]) { const g = group[q] * 3; N[g] += nx; N[g + 1] += ny; N[g + 2] += nz; }
  }
  for (let v = 0; v < n; v++) { const g = group[v] * 3; const l = Math.hypot(N[g], N[g + 1], N[g + 2]) || 1; N[v * 3] = N[g] / l; N[v * 3 + 1] = N[g + 1] / l; N[v * 3 + 2] = N[g + 2] / l; }
  prims.push({ name: m.name, udim: m.udim, P, N, U, I });
}
// GLB
const bufs = []; let len = 0; const views = [], accs = [];
const add = (arr, target, type, ct, count, mm) => {
  const b = Buffer.from(arr.buffer, arr.byteOffset, arr.byteLength); const pad = (4 - (len % 4)) % 4;
  if (pad) { bufs.push(Buffer.alloc(pad)); len += pad; }
  views.push({ buffer: 0, byteOffset: len, byteLength: b.length, target }); bufs.push(b); len += b.length;
  accs.push({ bufferView: views.length - 1, componentType: ct, count, type, ...(mm || {}) }); return accs.length - 1;
};
const meshes = [], nodes = [];
const mats = [{ name: 'udim1001', pbrMetallicRoughness: { baseColorFactor: [0.6, 0.6, 0.6, 1], metallicFactor: 0, roughnessFactor: 0.6 } }, { name: 'udim1002', pbrMetallicRoughness: { baseColorFactor: [0.6, 0.6, 0.6, 1], metallicFactor: 0, roughnessFactor: 0.6 } }];
let tris = 0;
for (const p of prims) {
  const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < p.P.length; i += 3) for (let c = 0; c < 3; c++) { mn[c] = Math.min(mn[c], p.P[i + c]); mx[c] = Math.max(mx[c], p.P[i + c]); }
  const a = { POSITION: add(p.P, 34962, 'VEC3', 5126, p.P.length / 3, { min: mn, max: mx }), NORMAL: add(p.N, 34962, 'VEC3', 5126, p.N.length / 3), TEXCOORD_0: add(p.U, 34962, 'VEC2', 5126, p.U.length / 2) };
  const ind = add(p.I, 34963, 'SCALAR', 5125, p.I.length);
  meshes.push({ name: p.name, primitives: [{ attributes: a, indices: ind, material: p.udim === 1001 ? 0 : 1 }] });
  nodes.push({ name: p.name, mesh: meshes.length - 1 });
  tris += p.I.length / 3;
}
const bin = Buffer.concat(bufs); const binPad = Buffer.concat([bin, Buffer.alloc((4 - bin.length % 4) % 4)]);
const json = { asset: { version: '2.0', generator: 'PlasmaOrk mb_mesh' }, scene: 0, scenes: [{ nodes: nodes.map((_, i) => i) }], nodes, meshes, materials: mats, accessors: accs, bufferViews: views, buffers: [{ byteLength: binPad.length }] };
let js = Buffer.from(JSON.stringify(json)); js = Buffer.concat([js, Buffer.alloc((4 - js.length % 4) % 4, 0x20)]);
const head = Buffer.alloc(12); head.writeUInt32LE(0x46546c67, 0); head.writeUInt32LE(2, 4); head.writeUInt32LE(12 + 8 + js.length + 8 + binPad.length, 8);
const ch = (t, b) => { const h = Buffer.alloc(8); h.writeUInt32LE(b.length, 0); h.writeUInt32LE(t, 4); return Buffer.concat([h, b]); };
fs.writeFileSync(outFile, Buffer.concat([head, ch(0x4e4f534a, js), ch(0x004e4942, binPad)]));
console.error('écrit', outFile, (fs.statSync(outFile).size / 1e6).toFixed(1) + ' Mo', tris, 'triangles');
