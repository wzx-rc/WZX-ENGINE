// Packs the converted Technic parts for lazy loading: quantized geometry + meta, deflated, grouped by sub-family.
import fs from "node:fs";
import zlib from "node:zlib";
const SRC = "/home/claude/conv/out_technic", OUT = "/home/claude/conv/pack/technic";
fs.mkdirSync(OUT, { recursive: true });
const SKIP = new Set(["3705", "3707", "59443", "3647", "94925", "3743", "2780", "6541", "32064a", "60483", "41677"]);   // already in the base library
export const SUB = [
  ["Poutres", ["Beam", "Liftarm"]], ["Axes", ["Axle"]], ["Pins", ["Pin"]], ["Connecteurs", ["Connector", "Cross", "Angle", "Bush", "Link"]],
  ["Engrenages", ["Gear", "Worm", "Differential", "Gearbox", "Sprocket", "Clutch", "Changeover", "Transmission", "Cam"]],
  ["Briques", ["Brick", "Plate", "Tile"]], ["Panneaux", ["Panel", "Fairing", "Dome", "Wedge", "Bumper", "Grille", "Windscreen", "Wing"]],
  ["Direction", ["Steering", "Suspension", "Shock", "Universal", "Ball"]], ["Pneumatique", ["Pneumatic"]],
  ["Chenilles", ["Chain", "Tread", "Belt", "Rubber", "Track", "Wheel", "Pulley"]], ["Moteurs", ["Engine", "Linear", "Piston", "Cylinder"]],
  ["Rotation", ["Turntable", "Rotor", "Rotation"]], ["Flex", ["Flex-System", "Cable"]], ["Bionicle", ["Bionicle", "Disc", "Sword"]],
];
export function subOf(label) { const w = (label.split(/\s+/)[1] || ""); const s = SUB.find(([, l]) => l.includes(w)); return s ? s[0] : "Divers"; }
const parts = JSON.parse(fs.readFileSync(SRC + "/lib_parts.json")).filter((p) => !SKIP.has(p.name));
const groups = new Map();
for (const p of parts) { const s = subOf(p.label); if (!groups.has(s)) groups.set(s, []); groups.get(s).push(p); }
const index = [], packs = [];
let packNo = 0, total = 0;
for (const [sub, list] of groups) {
  list.sort((a, b) => a.label.localeCompare(b.label, "en", { numeric: true }));
  let cur = null;
  const flush = () => {
    if (!cur || !cur.parts.length) return;
    const json = Buffer.from(JSON.stringify(cur.parts.map((x) => x.meta)));
    const head = Buffer.alloc(4); head.writeUInt32LE(json.length);
    const raw = Buffer.concat([head, json, ...cur.bufs]);
    const z = zlib.deflateSync(raw, { level: 9 });
    fs.writeFileSync(`${OUT}/g${cur.no}.bin`, z);
    packs.push({ no: cur.no, sub, n: cur.parts.length, raw: raw.length, z: z.length });
    total += z.length;
  };
  for (const p of list) {
    if (!cur || cur.size > 1.6e6) { flush(); cur = { no: packNo++, parts: [], bufs: [], size: 0, off: 0 }; }
    const bin = fs.readFileSync(`${SRC}/${p.name}.bin`);
    const vc = p.vertexCount, ic = p.indexCount;
    const f = new Float32Array(bin.buffer, bin.byteOffset, vc * 6), idx = new Uint32Array(bin.buffer, bin.byteOffset + vc * 24, ic);
    const mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
    for (let i = 0; i < vc; i++) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], f[i * 6 + k]); mx[k] = Math.max(mx[k], f[i * 6 + k]); }
    const sc = mx.map((v, k) => Math.max(v - mn[k], 1e-6) / 65535);
    const pos = new Uint16Array(vc * 3), nrm = new Int8Array(vc * 4);
    for (let i = 0; i < vc; i++) for (let k = 0; k < 3; k++) { pos[i * 3 + k] = Math.round((f[i * 6 + k] - mn[k]) / sc[k]); nrm[i * 4 + k] = Math.round(Math.max(-1, Math.min(1, f[i * 6 + 3 + k])) * 127); }
    const big = vc > 65535, ind = big ? new Uint32Array(idx) : new Uint16Array(idx);
    const pad = (b) => { const r = b.length % 4; return r ? Buffer.concat([b, Buffer.alloc(4 - r)]) : b; };
    const bPos = pad(Buffer.from(pos.buffer)), bNrm = Buffer.from(nrm.buffer), bInd = pad(Buffer.from(ind.buffer));
    const meta = { ...p, q: { off: cur.off, vc, ic, big, mn, sc } };
    delete meta.vertexOffset; delete meta.indexOffset;
    cur.parts.push({ meta }); cur.bufs.push(bPos, bNrm, bInd); cur.off += bPos.length + bNrm.length + bInd.length; cur.size += bPos.length + bNrm.length + bInd.length;
    index.push({ name: p.name, label: p.label, sub, pack: cur.no, info: { ldraw: p.info.ldraw, title: p.info.title, category: "Technic", sub, sizeCm: p.info.sizeCm, triangles: p.info.triangles, connectors: p.info.connectors, author: p.info.author, license: p.info.license, release: p.info.release, keywords: p.info.keywords, history: p.info.history, sizeStuds: p.info.sizeStuds, vertices: p.info.vertices, ldrawOrigin: p.info.ldrawOrigin, family: "Technic" }, voxN: p.voxels.length, heightPlates: p.heightPlates });
  }
  flush();
}
fs.writeFileSync(OUT + "/index.json", JSON.stringify({ format: "brickit-pack", version: 1, set: "technic", packs, parts: index }));
console.log("parts", index.length, "packs", packs.length, "total MB", (total / 1e6).toFixed(2), "index KB", (fs.statSync(OUT + "/index.json").size / 1e3).toFixed(0));
console.log(packs.map((p) => `${p.no}:${p.sub}:${p.n}:${(p.z / 1e3).toFixed(0)}k`).join("  "));
