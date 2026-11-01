import fs from "node:fs";
import { convertPart } from "./ldraw2brickit.mjs";
const OUT = "/home/claude/conv/out_technic";
const LIST = fs.readFileSync("/home/claude/dl/technic_list.txt", "utf8").trim().split("\n").map((l) => l.split("\t"));
const official = LIST.filter(([f]) => { const t = fs.readFileSync(`/home/claude/dl/ldraw-parts-library/complete/ldraw/parts/${f}`, "latin1"); return /0 !LDRAW_ORG Part/.test(t); });
console.log("official", official.length);
function headerOf(id) {
  const txt = fs.readFileSync(`/home/claude/dl/ldraw-parts-library/complete/ldraw/parts/${id}.dat`, "latin1").split(/\r?\n/);
  const h = { title: txt[0].replace(/^0\s+/, "").trim(), history: 0 };
  for (const l of txt) { let m;
    if ((m = l.match(/^0\s+Author:\s*(.*)$/))) h.author = m[1].trim();
    else if ((m = l.match(/^0\s+!LICENSE\s+(.*)$/))) h.license = m[1].replace(/Redistributable under /, "").replace(/\s*:.*$/, "").trim();
    else if ((m = l.match(/^0\s+!LDRAW_ORG\s+(.*)$/))) h.release = m[1].trim();
    else if ((m = l.match(/^0\s+!CATEGORY\s+(.*)$/))) h.category = m[1].trim();
    else if ((m = l.match(/^0\s+!KEYWORDS\s+(.*)$/))) h.keywords = (h.keywords ? h.keywords + ", " : "") + m[1].trim();
    else if (/^0\s+!HISTORY/.test(l)) h.history++; }
  if (!h.category) h.category = h.title.split(/\s+/)[0];
  return h;
}
const parts = [], fails = [];
let i = 0, bytes = 0;
for (const [f] of official) {
  const id = f.replace(/\.dat$/, "");
  i++;
  try {
    const t0 = performance.now();
    const r = convertPart(id, { system: false });
    const ms = performance.now() - t0;
    const vb = Buffer.from(new Float32Array(r.mesh.verts).buffer), ib = Buffer.from(new Uint32Array(r.mesh.idx).buffer);
    fs.writeFileSync(`${OUT}/${id}.bin`, Buffer.concat([vb, ib]));
    bytes += vb.length + ib.length;
    const h = headerOf(id);
    const cnt = {}; for (const t of r.types) cnt[t] = (cnt[t] || 0) + 1;
    const bb = [r.mesh.mx[0] - r.mesh.mn[0], r.mesh.mx[1] - r.mesh.mn[1], r.mesh.mx[2] - r.mesh.mn[2]];
    const info = { ldraw: id, title: h.title, family: "Technic", category: h.category, author: h.author, license: h.license, release: h.release, keywords: h.keywords, history: h.history,
      sizeCm: bb.map((x) => +x.toFixed(2)), sizeStuds: [+(bb[0] / 0.8).toFixed(2), +(bb[2] / 0.8).toFixed(2)], heightPlates: r.meta.heightPlates,
      triangles: r.mesh.idx.length / 3, vertices: r.mesh.verts.length / 6, connectors: cnt, studs: r.studs.length, convertMs: Math.round(ms) };
    info.ldrawOrigin = r.mesh.mn.map((x) => +(-x).toFixed(4));
    parts.push({ name: id, label: h.title.replace(/\s+/g, " ").replace(/^~/, "").trim(), info, vertexCount: r.mesh.verts.length / 6, indexCount: r.mesh.idx.length, ...r.meta, thumb: `thumbs/${id}.webp` });
  } catch (e) { fails.push([id, String(e.message || e).slice(0, 120)]); }
  if (i % 50 === 0) console.log(i, "parts", (bytes / 1e6).toFixed(1), "MB", "fails", fails.length);
}
fs.writeFileSync(OUT + "/lib_parts.json", JSON.stringify(parts));
fs.writeFileSync(OUT + "/fails.json", JSON.stringify(fails, null, 1));
console.log("done", parts.length, "MB", (bytes / 1e6).toFixed(1), "fails", fails.length);
