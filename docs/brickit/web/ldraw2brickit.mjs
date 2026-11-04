// LDraw -> BrickIt library converter (prototype).
// Reads LDraw .dat parts + LDCad shadow library snap metas, outputs BrickIt's __VP3D_LIB parts + binary buffer.
// Units: 1 LDU = 0.04 cm (stud pitch 20 LDU = 0.8 cm, plate 8 LDU = 0.32 cm). Axes: BrickIt (x, y, z) = (x, -y, -z) * 0.04.
import fs from "node:fs";
import path from "node:path";

const LDRAW = process.env.LDRAW || "/home/claude/dl/ldraw-parts-library/complete/ldraw";
const SHADOW = process.env.SHADOW || "/home/claude/dl/LDCadShadowLibrary";
const LDU = 0.04;
const HIRES = process.env.HIRES === "1";   // use p/48 primitives when they exist

// ---------------------------------------------------------------- file lookup
const fileCache = new Map();
function findFile(name) {
  const n = name.replace(/\\/g, "/").toLowerCase();
  const cands = [];
  if (HIRES && !n.includes("/")) cands.push("p/48/" + n);
  cands.push("parts/" + n, "p/" + n, n);
  for (const c of cands) {
    const full = path.join(LDRAW, c);
    if (fs.existsSync(full)) return { rel: c, full };
  }
  return null;
}
function readLines(full) {
  return fs.readFileSync(full, "latin1").split(/\r?\n/);
}

// ---------------------------------------------------------------- shadow snaps
function parseBrackets(s) {
  const o = {};
  for (const m of s.matchAll(/\[([^=\]]+)=([^\]]*)\]/g)) o[m[1].trim().toLowerCase()] = m[2].trim();
  return o;
}
const shadowCache = new Map();
function shadowOf(rel) {
  if (shadowCache.has(rel)) return shadowCache.get(rel);
  let rel2 = rel.replace(/^p\/48\//, "p/");
  const full = path.join(SHADOW, rel2);
  const res = { snaps: [], incl: [], clear: [] };
  if (fs.existsSync(full)) {
    for (const line of readLines(full)) {
      const m = line.match(/^\s*0\s+!LDCAD\s+(SNAP_\w+)\s*(.*)$/);
      if (!m) continue;
      const kind = m[1], a = parseBrackets(m[2]);
      if (kind === "SNAP_CLEAR") res.clear.push(a.id || "*");
      else if (kind === "SNAP_INCL") res.incl.push(a);
      else res.snaps.push({ kind, a });
    }
  }
  shadowCache.set(rel, res);
  return res;
}

// ---------------------------------------------------------------- math (3x4 matrices as [a b c d e f g h i x y z])
const IDENT = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0];
function mul(A, B) {   // A * B (apply B first)
  const r = new Array(12);
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) r[i * 3 + j] = A[i * 3] * B[j] + A[i * 3 + 1] * B[3 + j] + A[i * 3 + 2] * B[6 + j];
  for (let i = 0; i < 3; i++) r[9 + i] = A[i * 3] * B[9] + A[i * 3 + 1] * B[10] + A[i * 3 + 2] * B[11] + A[9 + i];
  return r;
}
const tp = (M, p) => [M[0] * p[0] + M[1] * p[1] + M[2] * p[2] + M[9], M[3] * p[0] + M[4] * p[1] + M[5] * p[2] + M[10], M[6] * p[0] + M[7] * p[1] + M[8] * p[2] + M[11]];
const det = (M) => M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) + M[2] * (M[3] * M[7] - M[4] * M[6]);
function matFrom(ori, pos) {
  const o = ori ? ori.split(/\s+/).map(Number) : [1, 0, 0, 0, 1, 0, 0, 0, 1];
  const p = pos ? pos.split(/\s+/).map(Number) : [0, 0, 0];
  return [...o, ...p];
}

// ---------------------------------------------------------------- snap expansion
function gridOffsets(g) {
  if (!g) return [[0, 0]];
  const t = g.split(/\s+/);
  let i = 0;
  const axis = () => { let c = false; if (t[i] === "C") { c = true; i++; } const n = Number(t[i++]); return { c, n }; };
  const ax = axis(), az = axis();
  const sx = Number(t[i++]), sz = Number(t[i++]);
  const offs = [];
  for (let a = 0; a < ax.n; a++) for (let b = 0; b < az.n; b++) {
    const x = (ax.c ? a - (ax.n - 1) / 2 : a) * sx, z = (az.c ? b - (az.n - 1) / 2 : b) * sz;
    offs.push([x, z]);
  }
  return offs;
}
function snapCylinders(snap, M, out, srcName) {
  const a = snap.a;
  if (snap.kind !== "SNAP_CYL") return;   // clips / fingers / generic: listed only
  const secs = (a.secs || "").split(/\s+/).filter(Boolean);
  const shapes = [], radii = [];
  let L = 0;
  for (let i = 0; i + 2 < secs.length; i += 3) {
    shapes.push(secs[i]); radii.push(Number(secs[i + 1])); L += Number(secs[i + 2]) || 0;
  }
  const local = matFrom(a.ori, a.pos);
  for (const [gx, gz] of gridOffsets(a.grid)) {
    const G = mul(local, [1, 0, 0, 0, 1, 0, 0, 0, 1, gx, 0, gz]);
    const W = mul(M, G);
    const centered = a.center === "true";
    const p0 = tp(W, [0, centered ? L / 2 : 0, 0]), p1 = tp(W, [0, centered ? -L / 2 : -L, 0]);   // LDCad cylinders run along -Y from pos
    out.push({ id: a.id || "", gender: (a.gender || "M").toUpperCase(), shapes, rmax: Math.max(...radii, 0), L, p0, p1, src: srcName });
  }
}
function collectInclSnaps(refName, M, out, depth = 0) {
  if (depth > 8) return;
  const f = findFile(refName);
  const rel = f ? f.rel : "p/" + refName.toLowerCase();
  const sh = shadowOf(rel);
  for (const s of sh.snaps) snapCylinders(s, M, out, refName);
  for (const inc of sh.incl) {
    const L = matFrom(inc.ori, inc.pos);
    for (const [gx, gz] of gridOffsets(inc.grid)) collectInclSnaps(inc.ref, mul(M, mul(L, [1, 0, 0, 0, 1, 0, 0, 0, 1, gx, 0, gz])), out, depth + 1);
  }
}

// ---------------------------------------------------------------- bevelled studs
// LDraw studs have sharp edges; with BEVEL=1 the stud primitives are replaced by a lathe with a rounded top edge
// (same size and snap data). Profiles in LDraw stud space: (radius, y), base y = 0, top y = -4.
const BEVEL_STUDS = process.env.BEVEL === "1";
const BR = Number(process.env.BEVEL_R || 0.6);   // fillet radius in LDU (0.6 LDU = 0.24 mm)
function fillet(cx, cy, a0, a1, n = 4) { const o = []; for (let i = 0; i <= n; i++) { const a = a0 + ((a1 - a0) * i) / n; o.push([cx + BR * Math.cos(a), cy + BR * Math.sin(a)]); } return o; }
const STUD_PROFILES = {
  "p/stud.dat": [[6, 0], ...fillet(6 - BR, -4 + BR, 0, -Math.PI / 2), [0, -4]],
  "p/stud2.dat": [[6, 0], ...fillet(6 - BR, -4 + BR, 0, -Math.PI / 2), ...fillet(4 + BR, -4 + BR, -Math.PI / 2, -Math.PI), [4, 0]],
};
function latheStud(profile, M, seg = 24) {
  const out = [];
  const flip = det(M) < 0;
  for (let i = 0; i + 1 < profile.length; i++) {
    const [r0, y0] = profile[i], [r1, y1] = profile[i + 1];
    const n2 = [-(y1 - y0), r1 - r0];   // outward normal in (r, y)
    for (let k = 0; k < seg; k++) {
      const a0 = (2 * Math.PI * k) / seg, a1 = (2 * Math.PI * (k + 1)) / seg;
      const P = (r, y, a) => [r * Math.cos(a), y, r * Math.sin(a)];
      const q = [P(r0, y0, a0), P(r0, y0, a1), P(r1, y1, a1), P(r1, y1, a0)];
      for (const tri of [[q[0], q[1], q[2]], [q[0], q[2], q[3]]]) {
        const u = tri[1].map((x, j) => x - tri[0][j]), v = tri[2].map((x, j) => x - tri[0][j]);
        const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
        if (Math.hypot(...n) < 1e-9) continue;
        const am = (a0 + a1) / 2, want = [n2[0] * Math.cos(am), n2[1], n2[0] * Math.sin(am)];
        const ok = n[0] * want[0] + n[1] * want[1] + n[2] * want[2] > 0;
        const t = ok !== flip ? tri : [tri[0], tri[2], tri[1]];
        out.push(t.map((p) => tp(M, p)));
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------- recursive load
// returns triangles in part space (LDU, LDraw axes), hard edges (type 2), snaps, primitive instances
// LDraw colours (LDConfig.ldr): code -> 0xRRGGBB, for the prints (triangles of another colour than 16)
let LDCOLORS = null;
function ldColor(code, parent) {
  if (code === 16) return parent;
  if (code === 24) return parent;
  if (code >= 0x2000000) return code & 0xffffff;   // direct colour 0x2RRGGBB
  if (!LDCOLORS) {
    LDCOLORS = new Map();
    const f = path.join(LDRAW, "LDConfig.ldr");
    if (fs.existsSync(f)) for (const l of readLines(f)) {
      const m = l.match(/!COLOUR\s+\S+\s+CODE\s+(\d+)\s+VALUE\s+#([0-9A-Fa-f]{6})/);
      if (m) LDCOLORS.set(Number(m[1]), parseInt(m[2], 16));
    }
  }
  return LDCOLORS.has(code) ? LDCOLORS.get(code) : parent;
}
function loadPart(name) {
  const tris = [], snaps = [], prims = [];
  // colour: -1 = the colour of the brick (16), else 0xRRGGBB of the print
  function walk(fname, M, invert, certifiedParent, depth, colour = -1) {
    const f = findFile(fname);
    if (!f) { console.warn("missing", fname); return; }
    const rel = f.rel;
    const lines = readLines(f.full);
    const sh = shadowOf(rel);
    // snaps of this file (shadow) + its includes
    const mySnaps = [];
    for (const s of sh.snaps) snapCylinders(s, M, mySnaps, fname);
    for (const inc of sh.incl) {
      const L = matFrom(inc.ori, inc.pos);
      for (const [gx, gz] of gridOffsets(inc.grid)) collectInclSnaps(inc.ref, mul(M, mul(L, [1, 0, 0, 0, 1, 0, 0, 0, 1, gx, 0, gz])), mySnaps, 0);
    }
    if (BEVEL_STUDS && STUD_PROFILES[rel]) { tris.push(...latheStud(STUD_PROFILES[rel], M)); return mySnaps; }
    const childSnaps = [];
    let winding = "CCW", certified = false, invertNext = false;
    for (const raw of lines) {
      const t = raw.trim().split(/\s+/);
      if (!t[0]) continue;
      if (t[0] === "0") {
        if (t[1] === "BFC") {
          const rest = t.slice(2);
          if (rest.includes("CERTIFY")) certified = true;
          if (rest.includes("NOCERTIFY")) certified = false;
          if (rest.includes("CW")) winding = "CW";
          if (rest.includes("CCW")) winding = "CCW";
          if (rest.includes("INVERTNEXT")) invertNext = true;
        }
        continue;
      }
      const cert = certifiedParent && certified;
      if (t[0] === "1") {
        const v = t.slice(2, 14).map(Number);
        const C = [v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[0], v[1], v[2]];
        const child = t.slice(14).join(" ");
        const W = mul(M, C);
        const inv = invert !== (det(C) < 0) !== invertNext;
        prims.push({ name: child.toLowerCase().replace(/\\/g, "/"), M: W });
        const before = childSnaps.length;
        const sub = walkCollect(child, W, inv, cert, depth + 1, ldColor(Number(t[1]), colour));
        childSnaps.push(...sub);
        invertNext = false;
      } else if (t[0] === "3" || t[0] === "4") {
        const n = t[0] === "3" ? 3 : 4;
        const pts = [];
        for (let i = 0; i < n; i++) pts.push(tp(M, t.slice(2 + i * 3, 5 + i * 3).map(Number)));
        const quads = n === 3 ? [[0, 1, 2]] : [[0, 1, 2], [0, 2, 3]];
        const c = ldColor(Number(t[1]), colour);
        for (const q of quads) {
          const tri = q.map((i) => pts[i]);
          const rev = [tri[0], tri[2], tri[1]];
          tri.c = c; rev.c = c;
          if (!cert) { tris.push(tri); tris.push(rev); continue; }
          let ccw = winding === "CCW";
          if (invert) ccw = !ccw;
          tris.push(ccw ? tri : rev);
        }
        invertNext = false;
      }
    }
    // SNAP_CLEAR in this file's shadow drops inherited snaps
    let inherited = childSnaps;
    for (const c of sh.clear) inherited = c === "*" ? [] : inherited.filter((s) => s.id.toLowerCase() !== c.toLowerCase());
    return [...mySnaps, ...inherited];
  }
  function walkCollect(child, W, inv, cert, depth, colour) { return walk(child, W, inv, cert, depth, colour) || []; }
  const top = walk(name, IDENT, false, true, 0) || [];
  snaps.push(...top);
  return { tris, snaps, prims };
}

// ---------------------------------------------------------------- geometry: to BrickIt space, weld, crease normals
const toB = (p) => [p[0] * LDU, -p[1] * LDU, -p[2] * LDU];
function buildMesh(trisL, crease = 35) {
  const tris = trisL.map((t) => { const b = t.map(toB); b.c = t.c === undefined ? -1 : t.c; return b; });
  // drop degenerate
  const faces = [];
  for (const t of tris) {
    const [a, b, c] = t;
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    const l = Math.hypot(...n);
    if (l < 1e-10) continue;
    faces.push({ t, n: n.map((x) => x / l), area: l / 2, c: t.c });
  }
  // bbox -> origin at min corner
  const mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
  for (const f of faces) for (const p of f.t) for (let i = 0; i < 3; i++) { mn[i] = Math.min(mn[i], p[i]); mx[i] = Math.max(mx[i], p[i]); }
  // position welding
  const key = (p) => p.map((x) => Math.round(x * 2000)).join(",");
  const incident = new Map();
  faces.forEach((f, fi) => f.t.forEach((p) => { const k = key(p); if (!incident.has(k)) incident.set(k, []); incident.get(k).push(fi); }));
  const cosT = Math.cos((crease * Math.PI) / 180);
  const verts = [], idx = [], vmap = new Map(), vcol = [];
  let printed = false;
  for (let fi = 0; fi < faces.length; fi++) {
    const f = faces[fi];
    for (const p of f.t) {
      const k = key(p);
      const nn = [0, 0, 0];
      for (const gj of incident.get(k)) {
        const g = faces[gj];
        const d = g.n[0] * f.n[0] + g.n[1] * f.n[1] + g.n[2] * f.n[2];
        if (d >= cosT) for (let i = 0; i < 3; i++) nn[i] += g.n[i] * g.area;
      }
      const l = Math.hypot(...nn) || 1;
      const n = nn.map((x) => x / l);
      const P = [p[0] - mn[0], p[1] - mn[1], p[2] - mn[2]];
      const vk = k + "|" + n.map((x) => Math.round(x * 500)).join(",") + "|" + f.c;   // (a print: its own vertices)
      let vi = vmap.get(vk);
      if (vi === undefined) { vi = verts.length / 6; verts.push(...P, ...n); vcol.push(f.c); vmap.set(vk, vi); if (f.c >= 0) printed = true; }
      idx.push(vi);
    }
  }
  return { verts, idx, mn, mx, faces, vcol: printed ? vcol : null };
}

// ---------------------------------------------------------------- connection voxels
const r4 = (x) => Math.round(x * 10000) / 10000;
function buildVoxels(part, mesh, opts) {
  const { snaps, prims } = part;
  const off = mesh.mn;
  const B = (p) => { const q = toB(p); return [r4(q[0] - off[0]), r4(q[1] - off[1]), r4(q[2] - off[2])]; };
  const vox = [], studs = [], types = [];
  const add = (p, type) => { if (!vox.some((v) => Math.hypot(v[0] - p[0], v[1] - p[1], v[2] - p[2]) < 0.02)) { vox.push(p); types.push(type); } };
  for (const s of snaps) {
    const d = [s.p1[0] - s.p0[0], s.p1[1] - s.p0[1], s.p1[2] - s.p0[2]];
    const len = Math.hypot(...d) || 1;
    const vertical = Math.abs(d[1] / len) > 0.99;
    const axle = s.shapes.includes("A");
    if (vertical && !axle && s.L <= 8 && s.rmax >= 5.5 && s.rmax <= 6.5) {
      const p = B(s.p0);
      if (s.gender === "M") { studs.push(p); add(p, "stud"); }
      else add(p, "antistud");
      continue;
    }
    if (vertical && !axle && s.rmax <= 4.5 && s.L <= 8) continue;   // small tubes under 1xN: covered by footprint rule
    if (vertical && !axle && s.gender === "F" && s.L >= 16 && s.rmax <= 8 && Math.abs(s.p0[1] - part.maxY) < 1) {
      add(B(s.p0), "antistud"); continue;   // deep anti-stud (round bricks)
    }
    // holes, pins, axles: sample along the axis every half stud
    const type = axle ? (s.gender === "M" ? "axle" : "axlehole") : (s.gender === "M" ? "pin" : "pinhole");
    if (opts.system && vertical && (type === "pinhole" || type === "pin")) continue;   // pin hole inside the 2x2 tubes: real but confusing in a stud builder
    const n = Math.max(1, Math.round(len / 10));
    for (let i = 0; i <= n; i++) {
      const t = i / n;
      add(B([s.p0[0] + d[0] * t, s.p0[1] + d[1] * t, s.p0[2] + d[2] * t]), type);
    }
  }
  // fallback studs from primitive names when the shadow library knows nothing
  if (!studs.length) {
    for (const pr of prims) {
      if (/^(stud|stud2|stud2a|studa|stud10|stud15|stud17a|stud18a)\.dat$/.test(pr.name)) {
        const p = B([pr.M[9], pr.M[10], pr.M[11]]);
        studs.push(p); add(p, "stud");
      }
    }
  }
  // footprint rule for System parts: anti-stud at every occupied 20 LDU cell on the bottom face
  if (opts.system) {
    const bottomY = 0;
    const cellsX = Math.round((mesh.mx[0] - mesh.mn[0]) / 0.8), cellsZ = Math.round((mesh.mx[2] - mesh.mn[2]) / 0.8);
    const verts = [];   // any vertex strictly inside a cell (any height) marks it as occupied
    for (const f of mesh.faces) for (const p of f.t) verts.push([p[0] - mesh.mn[0], p[2] - mesh.mn[2]]);
    for (let i = 0; i < cellsX; i++) for (let k = 0; k < cellsZ; k++) {
      const cx = 0.4 + i * 0.8, cz = 0.4 + k * 0.8;
      const occ = verts.some((v) => Math.abs(v[0] - cx) < 0.38 && Math.abs(v[1] - cz) < 0.38);
      const already = vox.some((v, j) => types[j] === "antistud" && Math.abs(v[0] - cx) < 0.05 && Math.abs(v[2] - cz) < 0.05);   // raised anti-stud from LDCad
      if (occ && !already) add([r4(cx), bottomY, r4(cz)], "antistud");
    }
  }
  if (opts.system) {   // System parts: anti-studs only on the stud grid
    const onGrid = (x) => Math.abs(((x - 0.4) / 0.8) - Math.round((x - 0.4) / 0.8)) < 0.03;
    for (let i = vox.length - 1; i >= 0; i--) if (types[i] === "antistud" && !(onGrid(vox[i][0]) && onGrid(vox[i][2]))) { vox.splice(i, 1); types.splice(i, 1); }
  }
  return { vox, studs, types };
}

// ---------------------------------------------------------------- main
export function convertPart(id, opts = {}) {
  const part = loadPart(id + ".dat");
  let maxY = -1e9; for (const t of part.tris) for (const p of t) maxY = Math.max(maxY, p[1]);
  part.maxY = maxY;
  const mesh = buildMesh(part.tris, opts.crease ?? 35);
  const { vox, studs, types } = buildVoxels(part, mesh, opts);
  const size = [mesh.mx[0] - mesh.mn[0], mesh.mx[1] - mesh.mn[1], mesh.mx[2] - mesh.mn[2]];
  const topY = studs.length ? Math.max(...studs.map((s) => s[1])) : r4(size[1]);
  const columns = studs.map((s) => [s[0], s[2]]);
  const grabPoints = (columns.length ? columns : vox.map((v) => [v[0], v[2]]));
  const center = [size[0] / 2, size[2] / 2];
  let grab = grabPoints[0] || center, best = 1e9;
  for (const g of grabPoints) { const d = Math.hypot(g[0] - center[0], g[1] - center[1]); if (d < best) { best = d; grab = g; } }
  return {
    mesh, vox, types, studs,
    meta: {
      heightPlates: Math.max(1, Math.round((studs.length ? topY : size[1]) / 0.32)),
      hasStuds: studs.length > 0, topY: r4(topY), grab, columns, grabPoints, voxels: vox,
      connectorTypes: types, ldraw: id + ".dat",
      snapCount: part.snaps.length,
    },
  };
}

if (process.argv[1] && process.argv[1].endsWith("ldraw2brickit.mjs") && process.argv[2]) {
  const r = convertPart(process.argv[2], { system: process.argv[3] !== "technic" });
  console.log(JSON.stringify({ verts: r.mesh.verts.length / 6, tris: r.mesh.idx.length / 3, ...r.meta }, null, 0).slice(0, 2000));
}
