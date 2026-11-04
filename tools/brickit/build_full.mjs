// The whole official LDraw library for BrickIt: every official part of parts/*.dat (not the aliases, the moved or
// obsolete ones, nor the parts already in the base set or the Technic packs), converted by the converter of the web
// build (ldraw2brickit.mjs: geometry, LDCad shadow library connectors), in parallel workers, then packed like the
// Technic packs (quantized geometry + metas, deflated, about 1.6 MB of raw data per pack) grouped by LDraw category.
// Output: <out>/index.json + <out>/f<n>.bin, read by Library::load (assets/brickit/full). The raw library stays out of
// git (build/raw); the packs are generated (build.bat copies them when present).
// Geometry: LDraw parts library (ldraw.org, CC BY 4.0 / CC BY 2.0). Connectors: LDCad shadow library (Roland Melkert).
// Usage: node tools/brickit/build_full.mjs <ldraw dir> <shadow dir> <out dir> [workers]
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import zlib from "node:zlib";
import { fork } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CONVERTER = path.join(HERE, "..", "..", "docs", "brickit", "web", "ldraw2brickit.mjs");

function header(full) {
  const txt = fs.readFileSync(full, "latin1").split(/\r?\n/);
  const h = { title: txt[0].replace(/^0\s+/, "").trim(), history: 0, type: "" };
  for (const l of txt) {
    let m;
    if ((m = l.match(/^0\s+Author:\s*(.*)$/))) h.author = m[1].trim();
    else if ((m = l.match(/^0\s+!LICENSE\s+(.*)$/))) h.license = m[1].replace(/Redistributable under /, "").replace(/Licensed under /, "").replace(/\s*:.*$/, "").trim();
    else if ((m = l.match(/^0\s+!LDRAW_ORG\s+(.*)$/))) { h.release = m[1].trim(); h.type = m[1].trim().split(/\s+/)[0]; }
    else if ((m = l.match(/^0\s+!CATEGORY\s+(.*)$/))) h.category = m[1].trim();
    else if ((m = l.match(/^0\s+!KEYWORDS\s+(.*)$/))) h.keywords = (h.keywords ? h.keywords + ", " : "") + m[1].trim();
    else if (/^0\s+!HISTORY/.test(l)) h.history++;
    else if (/^[1-5]\s/.test(l)) break;
  }
  if (!h.category) h.category = h.title.replace(/^[~_=|]+/, "").split(/\s+/)[0];
  return h;
}

// ------------------------------------------------------------------------------------------------ worker
if (process.argv[2] === "--worker") {
  const [, , , ldraw, shadow, tmp] = process.argv;
  process.env.LDRAW = ldraw;
  process.env.SHADOW = shadow;
  const { convertPart } = await import(pathToFileURL(CONVERTER).href);
  console.warn = () => {};
  process.on("message", (ids) => {
    const res = [];
    for (const [id, sys] of ids) {
      try {
        const t0 = performance.now();
        const r = convertPart(id, { system: sys });   // (Technic: the pin / axle holes, as the Technic packs)
        if (!r.mesh.idx.length) { res.push({ id, fail: "no geometry" }); continue; }
        const vb = Buffer.from(new Float32Array(r.mesh.verts).buffer), ib = Buffer.from(new Uint32Array(r.mesh.idx).buffer);
        fs.writeFileSync(path.join(tmp, id + ".bin"), Buffer.concat([vb, ib]));
        const cnt = {};
        for (const t of r.types) cnt[t] = (cnt[t] || 0) + 1;
        const bb = [r.mesh.mx[0] - r.mesh.mn[0], r.mesh.mx[1] - r.mesh.mn[1], r.mesh.mx[2] - r.mesh.mn[2]];
        res.push({ id, ms: Math.round(performance.now() - t0), vertexCount: r.mesh.verts.length / 6, indexCount: r.mesh.idx.length, meta: r.meta, cnt, bb,
                   origin: r.mesh.mn.map((x) => +(-x).toFixed(4)), studs: r.studs.length });
      } catch (e) { res.push({ id, fail: String(e.message || e).slice(0, 120) }); }
    }
    process.send(res);
  });
} else {
  // ---------------------------------------------------------------------------------------------- main
  const [, , ldraw, shadow, out, nw] = process.argv;
  const workers = Number(nw || Math.max(2, os.cpus().length - 2));
  const tmp = path.join(out, "_bin");
  fs.mkdirSync(tmp, { recursive: true });
  const skip = new Set();
  const base = path.join(HERE, "..", "..", "assets", "brickit", "base.json");
  for (const p of JSON.parse(fs.readFileSync(base, "utf8")).parts) skip.add(p.name.toLowerCase());
  const tech = path.join(HERE, "..", "..", "docs", "brickit", "web", "technic", "index.json");
  if (fs.existsSync(tech)) for (const p of JSON.parse(fs.readFileSync(tech, "utf8")).parts) skip.add(p.name.toLowerCase());
  const all = fs.readdirSync(path.join(ldraw, "parts")).filter((f) => f.endsWith(".dat"));
  const heads = new Map(), list = [];
  for (const f of all) {
    const id = f.slice(0, -4).toLowerCase();
    if (skip.has(id)) continue;
    const h = header(path.join(ldraw, "parts", f));
    // official parts only: no aliases (=, resolved by aliases.txt) nor moved (~Moved); the obsolete ones are kept (old
    // and official models use them), filed under Obsolete
    if (h.type !== "Part" && h.type !== "Shortcut") continue;
    if (/^~Moved|^=/.test(h.title) || h.category === "Moved") continue;
    if (/\(Obsolete\)/.test(h.title)) h.category = "Obsolete";
    heads.set(id, h);
    list.push(id);
  }
  console.log("parts to convert", list.length, "skipped (base / technic)", skip.size, "workers", workers);
  const results = [];
  const t0 = Date.now();
  await new Promise((done) => {
    let next = 0, running = 0;
    const CH = 40;
    for (let w = 0; w < workers; ++w) {
      const ch = fork(fileURLToPath(import.meta.url), ["--worker", ldraw, shadow, tmp], { stdio: ["ignore", "ignore", "ignore", "ipc"] });
      const feed = () => {
        if (next >= list.length) { ch.kill(); if (--running === 0) done(); return; }
        ch.send(list.slice(next, next + CH).map((id) => [id, heads.get(id).category !== "Technic"]));
        next += CH;
      };
      ch.on("message", (r) => {
        results.push(...r);
        if (results.length % 1000 < CH) console.log(results.length, "/", list.length, ((Date.now() - t0) / 1000).toFixed(0) + " s");
        feed();
      });
      ch.on("exit", (c) => { if (c && next < list.length) console.log("worker exit", c); });
      ++running;
      feed();
    }
  });
  const ok = results.filter((r) => !r.fail), fails = results.filter((r) => r.fail);
  fs.writeFileSync(path.join(out, "fails.json"), JSON.stringify(fails, null, 1));
  console.log("converted", ok.length, "failed", fails.length, "in", ((Date.now() - t0) / 1000).toFixed(0), "s");

  // ------------------------------------------------------------------------------------------ packs by category
  const groups = new Map();
  for (const r of ok) { const c = heads.get(r.id).category; if (!groups.has(c)) groups.set(c, []); groups.get(c).push(r); }
  const index = [], packs = [];
  let packNo = 0, total = 0;
  const pad = (b) => { const k = b.length % 4; return k ? Buffer.concat([b, Buffer.alloc(4 - k)]) : b; };
  for (const [cat, items] of [...groups].sort((a, b) => a[0].localeCompare(b[0]))) {
    items.sort((a, b) => heads.get(a.id).title.localeCompare(heads.get(b.id).title, "en", { numeric: true }));
    let cur = null;
    const flush = () => {
      if (!cur || !cur.metas.length) return;
      const json = Buffer.from(JSON.stringify(cur.metas));
      const head = Buffer.alloc(4);
      head.writeUInt32LE(json.length);
      const raw = Buffer.concat([head, json, ...cur.bufs]);
      const z = zlib.deflateSync(raw, { level: 9 });
      fs.writeFileSync(path.join(out, `f${cur.no}.bin`), z);
      packs.push({ no: cur.no, sub: cat, n: cur.metas.length, raw: raw.length, z: z.length });
      total += z.length;
    };
    for (const r of items) {
      if (!cur || cur.size > 1.6e6) { flush(); cur = { no: packNo++, metas: [], bufs: [], size: 0, off: 0 }; }
      const bin = fs.readFileSync(path.join(tmp, r.id + ".bin"));
      const vc = r.vertexCount, ic = r.indexCount;
      const f = new Float32Array(bin.buffer, bin.byteOffset, vc * 6), idx = new Uint32Array(bin.buffer, bin.byteOffset + vc * 24, ic);
      const mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
      for (let i = 0; i < vc; i++) for (let k = 0; k < 3; k++) { mn[k] = Math.min(mn[k], f[i * 6 + k]); mx[k] = Math.max(mx[k], f[i * 6 + k]); }
      const sc = mx.map((v, k) => Math.max(v - mn[k], 1e-6) / 65535);
      const pos = new Uint16Array(vc * 3), nrm = new Int8Array(vc * 4);
      for (let i = 0; i < vc; i++) for (let k = 0; k < 3; k++) {
        pos[i * 3 + k] = Math.round((f[i * 6 + k] - mn[k]) / sc[k]);
        nrm[i * 4 + k] = Math.round(Math.max(-1, Math.min(1, f[i * 6 + 3 + k])) * 127);
      }
      const big = vc > 65535, ind = big ? new Uint32Array(idx) : new Uint16Array(idx);
      const bPos = pad(Buffer.from(pos.buffer)), bNrm = Buffer.from(nrm.buffer), bInd = pad(Buffer.from(ind.buffer));
      const h = heads.get(r.id);
      const label = h.title.replace(/\s+/g, " ").replace(/^[~_]/, "").trim();
      cur.metas.push({ name: r.id, label, ...r.meta, q: { off: cur.off, vc, ic, big, mn, sc } });
      cur.bufs.push(bPos, bNrm, bInd);
      cur.off += bPos.length + bNrm.length + bInd.length;
      cur.size += bPos.length + bNrm.length + bInd.length;
      index.push({ name: r.id, label, pack: cur.no, info: { ldraw: r.id, title: h.title, category: cat, family: "", sizeCm: r.bb.map((x) => +x.toFixed(2)),
        sizeStuds: [+(r.bb[0] / 0.8).toFixed(2), +(r.bb[2] / 0.8).toFixed(2)], triangles: ic / 3, vertices: vc, connectors: r.cnt, author: h.author,
        license: h.license, release: h.release, keywords: h.keywords, history: h.history, ldrawOrigin: r.origin }, voxN: r.meta.voxels.length,
        heightPlates: r.meta.heightPlates, topY: r.meta.topY, hasStuds: r.meta.hasStuds });
    }
    flush();
  }
  fs.writeFileSync(path.join(out, "index.json"), JSON.stringify({ format: "brickit-pack", version: 1, set: "ldraw-full", packs, parts: index }));
  fs.rmSync(tmp, { recursive: true, force: true });
  console.log("parts", index.length, "packs", packs.length, "total MB", (total / 1e6).toFixed(1), "index MB", (fs.statSync(path.join(out, "index.json")).size / 1e6).toFixed(1));
}
