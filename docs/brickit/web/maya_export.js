/* MAYA EXPORT BEGIN */
// Scène -> script Python Maya : maillages des pièces, matrices des briques, shaders, rig (corps + liaisons + set driven keys).
// Les clés pilotées sont échantillonnées depuis le solveur cinématique BrickIt (boucles fermées comprises).
async function mayaExportScript(opts = {}) {
  const api = window.__mechApi(), M = window.__mech;
  const bricks = api.bricks(), parts = api.parts, pal = api.palette ? api.palette() : (window.__VP3D_LIB && window.__VP3D_LIB.palette) || [];
  if (!bricks.length) throw new Error("Scène vide");
  const wasOn = M.on;
  if (!wasOn) api.mechEnter();
  const m = M.model;
  if (!m) throw new Error("Mécanisme non construit");
  const saved = m.cmds.map((c) => c.value), savedQ = Float64Array.from(m.q);
  const r6 = (x) => Math.round(x * 1e6) / 1e6, r4 = (x) => Math.round(x * 1e4) / 1e4;
  // --- meshes (local space of the part, vertices merged by position + normal)
  const meshes = {};
  for (const name of new Set(bricks.map((b) => b.part))) {
    const g = parts[name].geometry, P = g.getAttribute("position"), N = g.getAttribute("normal"), I = g.getIndex();
    const map = new Map(), v = [], remap = new Int32Array(P.count);
    for (let k = 0; k < P.count; k++) {
      const key = [P.getX(k), P.getY(k), P.getZ(k)].map((x) => Math.round(x * 1e4)).join(",") + (N ? "|" + [N.getX(k), N.getY(k), N.getZ(k)].map((x) => Math.round(x * 100)).join(",") : "");
      let id = map.get(key);
      if (id === undefined) { id = v.length / 3; map.set(key, id); v.push(r4(P.getX(k)), r4(P.getY(k)), r4(P.getZ(k))); }
      remap[k] = id;
    }
    const idx = [], n = I ? I.count : P.count;
    for (let t = 0; t + 2 < n; t += 3) {
      const a = remap[I ? I.getX(t) : t], b = remap[I ? I.getX(t + 1) : t + 1], c = remap[I ? I.getX(t + 2) : t + 2];
      if (a !== b && b !== c && a !== c) idx.push(a, b, c);
    }
    meshes[name] = { v, i: idx };
  }
  // --- colours used
  const colors = {};
  for (const b of bricks) { const c = pal.find((x) => x.id === b.color) || pal[0]; colors[b.color] = { name: `${c.name || "c"}_${b.color}`, rgb: c.rgb, group: c.group || "solid" }; }
  // --- bricks (rest pose, world = body group local space)
  const one = new THREE.Vector3(1, 1, 1);
  const out = bricks.map((b, i) => {
    const B = new THREE.Matrix4().compose(b.pos, b.quat, one);
    const sp = m.spinMat && m.spinMat.get(i); if (sp) B.multiply(sp);
    return { part: b.part, color: b.color, body: m.brickBody[i], m: B.elements.map(r6), ldraw: String(parts[b.part].ldraw || parts[b.part].id || b.part) };
  });
  // --- joints: channel + alignment
  const X = new THREE.Vector3(1, 0, 0), AX = ["X", "Y", "Z"];
  const jdesc = m.tree.map((t, k) => {
    const u = t.u.clone().normalize(), comps = [u.x, u.y, u.z], ai = comps.findIndex((c) => Math.abs(Math.abs(c) - 1) < 1e-6);
    const d = { k, type: t.type, parent: t.parent, child: t.child, p: [t.p.x, t.p.y, t.p.z].map(r6), align: null, s: 1 };
    let axis = "X";
    if (ai >= 0) { axis = AX[ai]; d.s = Math.sign(comps[ai]); }
    else {
      const q = new THREE.Quaternion().setFromUnitVectors(X, u), e = new THREE.Euler().setFromQuaternion(q, "ZYX"), ei = new THREE.Euler().setFromQuaternion(q.clone().invert(), "ZYX");
      const deg = THREE.MathUtils.radToDeg;
      d.align = [deg(e.x), deg(e.y), deg(e.z)].map(r6); d.unalign = [deg(ei.x), deg(ei.y), deg(ei.z)].map(r6);
    }
    d.chan = (t.type === "rev" ? "rotate" : "translate") + axis;
    return d;
  });
  const chanVal = (k, q) => { const t = m.tree[k], d = jdesc[k]; return t.type === "rev" ? THREE.MathUtils.radToDeg(q) * d.s : t.sgn * q * d.s; };
  // --- solve helpers
  const setCmds = (vals) => m.cmds.forEach((c, i) => (c.value = vals[i]));
  const solveAt = (vals) => { setCmds(vals); return api.mechSolve(m) ? Float64Array.from(m.q) : null; };
  const zeros = m.cmds.map(() => 0);
  m.q.fill(0);
  let q0 = solveAt(zeros);
  if (!q0) { m.q.set(savedQ); q0 = solveAt(zeros) || Float64Array.from(m.q); }
  jdesc.forEach((d, k) => (d.base = r6(chanVal(k, q0[k]))));
  // --- sample each command alone, marching out from 0 so the solver keeps the same branch of the loops
  const DEG = Math.PI / 180, cmdsOut = [], fails = [];
  const samplesOf = (c) => {
    if (c.type === "motor") { const s = []; for (let a = -360; a <= 360; a += 30) s.push(a); return { list: s, toQ: (a) => a * DEG }; }
    const lo = Math.max(c.lo, -50), hi = Math.min(c.hi, 50), n = 16, s = new Set([0]);
    for (let k = 0; k <= n; k++) s.add(r6(lo + ((hi - lo) * k) / n));
    return { list: [...s].sort((a, b) => a - b), toQ: (x) => x };
  };
  m.cmds.forEach((c, ci) => {
    const { list, toQ } = samplesOf(c), res = new Map([[0, q0]]);
    for (const dir of [1, -1]) {
      m.q.set(q0);
      let last = q0;
      for (const v of list.filter((x) => (dir > 0 ? x > 0 : x < 0)).sort((a, b) => dir * (a - b))) {
        const vals = zeros.slice(); vals[ci] = toQ(v);
        const q = solveAt(vals);
        if (!q) { fails.push(`${c.name} = ${v}`); m.q.set(last); break; }
        res.set(v, q); last = q;
      }
    }
    const keys = [...res.keys()].sort((a, b) => a - b);
    jdesc.forEach((d, k) => {
      const vals = keys.map((v) => chanVal(k, res.get(v)[k]) - chanVal(k, q0[k]));
      if (Math.max(...vals.map(Math.abs)) < 1e-5) return;
      (d.sdk || (d.sdk = {}))[ci] = keys.map((v, i) => [v, r6(vals[i])]);
    });
    const attr = (c.type === "motor" ? "moteur" : "verin") + (cmdsOut.filter((o) => o.type === c.type).length + 1);
    cmdsOut.push({ name: c.name, attr, type: c.type, lo: c.type === "motor" ? -360 : r6(keys[0]), hi: c.type === "motor" ? 360 : r6(keys[keys.length - 1]) });
  });
  // --- coupling check: Maya adds the per-command curves; compare with the true solve for random combinations
  let coupling = 0;
  for (let trial = 0; trial < 6; trial++) {
    const vals = m.cmds.map((c, ci) => { const o = cmdsOut[ci]; return o.lo + Math.random() * (o.hi - o.lo); });
    m.q.set(q0);
    // march to the target in steps
    let q = null;
    for (let s = 1; s <= 8; s++) { q = solveAt(vals.map((v, ci) => (m.cmds[ci].type === "motor" ? v * DEG : v) * (s / 8))); if (!q) break; }
    if (!q) continue;
    jdesc.forEach((d, k) => {
      if (!d.sdk) return;
      let sum = 0;
      for (const [ci, keys] of Object.entries(d.sdk)) {
        const x = vals[ci];
        let j = 1; while (j < keys.length - 1 && keys[j][0] < x) j++;
        const [x0, y0] = keys[j - 1], [x1, y1] = keys[j];
        sum += y0 + ((y1 - y0) * (x - x0)) / (x1 - x0 || 1);
      }
      const truth = chanVal(k, q[k]) - chanVal(k, q0[k]);
      coupling = Math.max(coupling, Math.abs(truth - sum) * (m.tree[k].type === "rev" ? 1 : 10));
    });
  }
  // restore the web scene
  setCmds(saved); m.q.set(savedQ); api.mechSolve(m); if (api.mechApply) api.mechApply();
  if (!wasOn) api.mechExit();
  const name = (opts.name || "BrickIt_scene").replace(/[^A-Za-z0-9_]/g, "_");
  const DATA = { name, bevel: opts.bevel ?? 0.02, colors, meshes, bricks: out, roots: m.roots, joints: jdesc, cmds: cmdsOut };
  const json = new TextEncoder().encode(JSON.stringify(DATA));
  const cs = new Blob([json]).stream().pipeThrough(new CompressionStream("deflate"));
  const z = new Uint8Array(await new Response(cs).arrayBuffer());
  let bin = ""; for (let i = 0; i < z.length; i += 0x8000) bin += String.fromCharCode.apply(null, z.subarray(i, i + 0x8000));
  const b64 = btoa(bin).replace(/(.{1,120})/g, "$1\n");
  const py = MAYA_TEMPLATE.replace('"__DATA__"', '"""\n' + b64 + '"""');
  return { py, stats: { bricks: out.length, parts: Object.keys(meshes).length, bodies: m.bodies.length, joints: jdesc.length, cmds: cmdsOut.length, fails, coupling: r4(coupling), warnings: m.warnings || [] } };
}
/* MAYA EXPORT END */
