// Mode Pose du builder : le contrôleur de rig du Rig Lab sur la figurine équipée, sans changer de page.
// - os en rayons X, clic sur la figurine = l'os qui domine sous la souris ; Rotation (E) en FK, IK (W) pour mains,
//   pieds et bassin ; symétrie ; pieds ancrés au sol pendant un déplacement du bassin ;
// - à gauche, la bibliothèque de poses (Neutre, Pose A, Pose T, poses de départ, mes poses) avec « + » pour
//   enregistrer la pose actuelle, clic droit pour renommer ou supprimer ;
// - à droite, l'os sélectionné et des presets qui dépendent de la sélection (main : ouvrir / fermer, etc.).
// Les briques restent accrochées à leurs os : elles suivent la pose.
import * as THREE from 'three';
import { CHAINS, GROUPS, label } from './riglab/model.js';
import { PRESETS, MY_POSES, loadMyPoses, armTo, ARM } from './riglab/poses.js';

const $ = id => document.getElementById(id);
const DEG = THREE.MathUtils.RAD2DEG;
const HIPS = 'QuickRigCharacter_Hips';
const Q = n => 'QuickRigCharacter_' + n;
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const ICON = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const cssAccent = () => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#f0435a';

// Os du bras / de la jambe de chaque côté (côté gauche de l'ork = noms QuickRig « Left… », droit = « Left…1 »).
const SIDE_BONES = {
  L: { arm: ['joint40', Q('LeftArm'), Q('LeftForeArm'), Q('LeftHand')], leg: [Q('LeftUpLeg'), Q('LeftLeg'), Q('LeftFoot'), Q('LeftToeBase')], s: '' },
  R: { arm: ['joint41', Q('LeftArm1'), Q('LeftForeArm1'), Q('LeftHand1')], leg: [Q('LeftUpLeg1'), Q('LeftLeg1'), Q('LeftFoot1'), Q('LeftToeBase1')], s: '1' },
};

// opts : { vp, model, thumbs, state() (état du builder), edited(final) (pose modifiée), apply(entry) (charge une
// pose de la liste), list() (liste des poses), toast(msg) }
export function createPoseMode(opts) {
  const { vp, model, toast } = opts;
  const gizmo = vp.gizmo;
  const ui = { active: false, sel: null, hover: null, tool: 'fk', sym: false, pins: true, saving: false };
  try { Object.assign(ui, JSON.parse(localStorage.getItem('plasmaork.builder.posemode') || '{}'), { active: false, sel: null, hover: null, saving: false }); } catch { /* défaut */ }
  const keep = () => { try { localStorage.setItem('plasmaork.builder.posemode', JSON.stringify({ tool: ui.tool, sym: ui.sym, pins: ui.pins })); } catch { /* ignoré */ } };

  // ------------------------------------------------------------ surbrillance de l'influence (comme le Rig Lab)
  const ACCENT = new THREE.Color(cssAccent());
  const HL = { uSel: { value: -1 }, uHov: { value: -1 }, uAccent: { value: ACCENT.clone().convertSRGBToLinear() } };
  addEventListener('plasma-faction', () => { ACCENT.set(cssAccent()); HL.uAccent.value.copy(ACCENT).convertSRGBToLinear(); });
  function patch(mat) {
    if (!mat || mat.userData.poseHL) return;
    mat.userData.poseHL = true;
    const prev = mat.onBeforeCompile;
    mat.onBeforeCompile = (sh, r) => {
      prev?.call(mat, sh, r);
      Object.assign(sh.uniforms, HL);
      sh.vertexShader = sh.vertexShader
        .replace('#include <common>', '#include <common>\nuniform float uSel;\nuniform float uHov;\nvarying float vSelW;\nvarying float vHovW;')
        .replace('#include <skinning_vertex>', `#include <skinning_vertex>
          #ifdef USE_SKINNING
            vSelW = dot(skinWeight, vec4(equal(skinIndex, vec4(uSel))));
            vHovW = dot(skinWeight, vec4(equal(skinIndex, vec4(uHov))));
          #else
            vSelW = 0.0; vHovW = 0.0;
          #endif`);
      sh.fragmentShader = sh.fragmentShader
        .replace('#include <common>', '#include <common>\nuniform vec3 uAccent;\nvarying float vSelW;\nvarying float vHovW;')
        .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
          totalEmissiveRadiance += uAccent * 0.55 * vSelW + vec3(0.18) * vHovW * (1.0 - vSelW);`);
    };
    const key = mat.customProgramCacheKey?.bind(mat);
    mat.customProgramCacheKey = () => (key ? key() : '') + '|poseHL';
    mat.needsUpdate = true;
  }
  function patchOrk() { for (const m of Object.values(model.meshes)) patch(m.material); }
  function highlight(on) {
    HL.uSel.value = on && ui.sel ? model.bones.indexOf(ui.sel) : -1;
    HL.uHov.value = on && ui.hover ? model.bones.indexOf(ui.hover) : -1;
  }

  // ------------------------------------------------------------ os affichés
  const viz = new THREE.Group();
  viz.visible = false;
  viz.userData.helper = true;
  vp.overlay.add(viz);
  const sphereGeo = new THREE.SphereGeometry(1, 12, 8);
  const spheres = model.bones.map(b => {
    const s = new THREE.Mesh(sphereGeo, new THREE.MeshBasicMaterial({ color: 0xb5b7bd, depthTest: false, transparent: true, opacity: 0.85 }));
    s.userData = { bone: b, helper: true };
    s.renderOrder = 10;
    viz.add(s);
    return s;
  });
  const lineGeo = new THREE.BufferGeometry();
  lineGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(model.bones.length * 6), 3));
  const lines = new THREE.LineSegments(lineGeo, new THREE.LineBasicMaterial({ color: 0x8e9097, depthTest: false, transparent: true, opacity: 0.7 }));
  lines.renderOrder = 9;
  lines.userData.helper = true;
  viz.add(lines);
  const _p = new THREE.Vector3();
  const posed = b => b.quaternion.angleTo(b.userData.restQ) > 1e-3 || (b.name === HIPS && b.position.distanceToSquared(b.userData.restP) > 1e-6);
  function updateViz() {
    if (!ui.active) return;
    const pos = lineGeo.attributes.position;
    model.bones.forEach((b, i) => {
      const s = spheres[i];
      b.getWorldPosition(s.position);
      const finger = /^joint\d+$/.test(b.name) && !['joint40', 'joint41'].includes(b.name);
      s.scale.setScalar((finger ? 0.008 : 0.018) * (b === ui.sel ? 1.5 : b === ui.hover ? 1.3 : 1));
      s.material.color.set(b === ui.sel ? ACCENT : b === ui.hover ? 0xffffff : posed(b) ? 0x7bc67e : 0xb5b7bd);
      const p = b.parent?.isBone ? b.parent.getWorldPosition(_p) : s.position;
      pos.setXYZ(i * 2, p.x, p.y, p.z);
      pos.setXYZ(i * 2 + 1, s.position.x, s.position.y, s.position.z);
    });
    pos.needsUpdate = true;
  }

  // ------------------------------------------------------------ sélection, gizmo (FK / IK)
  const ikTarget = new THREE.Object3D();
  ikTarget.userData.helper = true;
  model.ork.add(ikTarget);
  let pins = null;
  const chainOf = b => Object.entries(CHAINS).find(([, n]) => n.includes(b.name))?.[0] || null;
  function select(b) {
    ui.sel = b || null;
    highlight(true);
    attach();
    renderPanel();
  }
  function attach() {
    gizmo.detach();
    const b = ui.sel;
    if (!ui.active || !b) return;
    const chain = chainOf(b);
    if (ui.tool === 'ik' && (chain || b.name === HIPS)) {
      gizmo.setMode('translate'); gizmo.setSpace('world');
      if (b.name === HIPS) { gizmo.attach(b); return; }
      model.figurePos(model.bone(CHAINS[chain][2]), ikTarget.position);
      ikTarget.userData.chain = chain;
      gizmo.attach(ikTarget);
      return;
    }
    gizmo.setMode('rotate'); gizmo.setSpace('local'); gizmo.attach(b);
  }
  const fingerBone = b => /^joint\d+$/.test(b.name) && !['joint40', 'joint41'].includes(b.name);
  gizmo.addEventListener('mouseDown', () => {
    if (!ui.active) return;
    pins = ui.pins ? { footL: model.figurePos(model.bone(CHAINS.footL[2])), footR: model.figurePos(model.bone(CHAINS.footR[2])) } : null;
  });
  gizmo.addEventListener('objectChange', () => {
    const b = ui.sel;
    if (!ui.active || !b) return;
    if (gizmo.object === ikTarget) {
      model.solveIK(ikTarget.userData.chain, ikTarget.position);
      if (ui.sym) model.mirrorBones(CHAINS[ikTarget.userData.chain].map(n => model.bone(n)));
    } else {
      if (ui.sym) model.mirrorBones([b]);
      if (pins && b.name === HIPS) for (const id of ['footL', 'footR']) model.solveIK(id, pins[id]);
      if (fingerBone(b)) opts.state().fingers = 'pose';
    }
    changed(false);
  });
  gizmo.addEventListener('mouseUp', () => { if (!ui.active) return; pins = null; changed(true); });

  // une modification de la pose : final = fin du geste (historique, vignettes)
  function changed(final) {
    model.root.updateMatrixWorld(true);
    if (gizmo.object === ikTarget && !gizmo.dragging && ikTarget.userData.chain) model.figurePos(model.bone(CHAINS[ikTarget.userData.chain][2]), ikTarget.position);
    opts.edited(final);
    if (final) renderPanel(); else syncSliders();
    renderLib();
  }

  // ------------------------------------------------------------ picking
  const ray = new THREE.Raycaster();
  const ndc = new THREE.Vector2();
  function pick(ev) {
    const r = vp.canvas.getBoundingClientRect();
    ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(ndc, vp.camera);
    let best = null, bd = Infinity;
    for (const s of spheres) {
      const d = ray.ray.distanceSqToPoint(s.position), rad = s.scale.x * 1.6;
      if (d < rad * rad && d < bd) { bd = d; best = s.userData.bone; }
    }
    if (best) return best;
    const targets = [model.meshes.BODY_low, model.meshes.HEAD_low].filter(Boolean);
    for (const m of targets) { m.boundingSphere = null; m.boundingBox = null; }
    const h = ray.intersectObjects(targets, false)[0];
    if (!h) return null;
    if (h.object.name === 'HEAD_low') return model.bone(Q('Head'));
    return model.boneAtFace(h.face, h.barycoord);
  }
  let down = null, hoverQueued = null;
  vp.canvas.addEventListener('pointerdown', e => {
    if (!ui.active || e.button !== 0) return;
    down = { x: e.clientX, y: e.clientY, gizmo: gizmo.dragging || gizmo.axis !== null };
  });
  vp.canvas.addEventListener('pointerup', e => {
    if (!ui.active || !down || e.button !== 0) return;
    if (!down.gizmo && Math.hypot(e.clientX - down.x, e.clientY - down.y) <= 5) select(pick(e));
    down = null;
  });
  vp.canvas.addEventListener('pointermove', e => {
    const tip = $('vpTip');
    if (!ui.active || gizmo.dragging || e.buttons) { tip.hidden = true; return; }
    if (hoverQueued) { hoverQueued.e = e; return; }
    hoverQueued = { e };
    requestAnimationFrame(() => {
      const ev = hoverQueued.e;
      hoverQueued = null;
      if (!ui.active) return;
      const b = gizmo.axis !== null ? null : pick(ev);
      ui.hover = b;
      highlight(true);
      const r = vp.canvas.getBoundingClientRect();
      tip.hidden = !b;
      if (b) { tip.textContent = label(b.name); tip.style.left = (ev.clientX - r.left) + 'px'; tip.style.top = (ev.clientY - r.top) + 'px'; }
    });
  });
  vp.canvas.addEventListener('pointerleave', () => { ui.hover = null; highlight(ui.active); $('vpTip').hidden = true; });

  // ------------------------------------------------------------ presets selon la sélection
  const sideOf = b => {
    const g = b && GROUPS.find(g => g.bones.includes(b.name));
    return g && /L$/.test(g.id) ? 'L' : g && /R$/.test(g.id) ? 'R' : null;
  };
  const zoneOf = b => {
    if (!b) return 'none';
    const g = GROUPS.find(g => g.bones.includes(b.name))?.id || '';
    if (g.startsWith('hand') || b.name === Q('LeftHand') || b.name === Q('LeftHand1')) return 'hand';
    if (g.startsWith('arm')) return 'arm';
    if (g.startsWith('leg')) return 'leg';
    if (b.name === Q('Head') || b.name === Q('Neck')) return 'head';
    return 'spine';
  };
  const resetList = names => names.forEach(n => model.resetBone(model.bone(n)));
  const curlTo = (sides, v) => { const st = opts.state(); for (const s of sides) { st.curl[s] = v; model.curl(s, v); } st.fingers = 'curl'; };
  const sidesFor = side => (ui.sym || !side ? ['L', 'R'] : [side]);
  const footFloor = () => {
    for (const id of ['footL', 'footR']) {
      const end = model.bone(CHAINS[id][2]), t = model.figurePos(end);
      t.y = end.userData.restW.y;
      model.solveIK(id, t);
    }
  };
  const legStep = (s, up, knee, foot) => {
    const L = SIDE_BONES[s].leg;
    resetList(L.slice(0, 3));
    model.rotateWorld(L[0], [1, 0, 0], up); model.rotateWorld(L[1], [1, 0, 0], knee); model.rotateWorld(L[2], [1, 0, 0], foot);
  };
  const look = (yaw, pitch) => {
    resetList([Q('Neck'), Q('Head')]);
    if (yaw) { model.rotateWorld(Q('Neck'), [0, 1, 0], yaw * 0.6); model.rotateWorld(Q('Head'), [0, 1, 0], yaw * 0.4); }
    if (pitch) { model.rotateWorld(Q('Neck'), [1, 0, 0], pitch * 0.5); model.rotateWorld(Q('Head'), [1, 0, 0], pitch * 0.5); }
  };
  const spine = fn => { resetList([Q('Spine'), Q('Spine1'), Q('Spine2')]); fn(); };
  const hipsRestY = () => model.hips.userData.restP.y;
  // { id, name, run(side) }
  const CTX = {
    hand: [
      { id: 'open', name: 'Main ouverte', run: s => curlTo(sidesFor(s), 0) },
      { id: 'relax', name: 'Main détendue', run: s => curlTo(sidesFor(s), 0.45) },
      { id: 'grip', name: 'Prise d\'arme', run: s => curlTo(sidesFor(s), 1.1) },
      { id: 'fist', name: 'Poing fermé', run: s => curlTo(sidesFor(s), 1.6) },
      { id: 'wrist', name: 'Poignet droit', run: s => sidesFor(s).forEach(x => model.resetBone(model.bone(SIDE_BONES[x].arm[3]))) },
    ],
    arm: [
      { id: 'down', name: 'Bras le long du corps', run: s => sidesFor(s).forEach(x => armTo(model, x, ...ARM.down)) },
      { id: 'A', name: 'Bras en A', run: s => sidesFor(s).forEach(x => resetList(SIDE_BONES[x].arm.slice(1, 3))) },
      { id: 'T', name: 'Bras en T', run: s => sidesFor(s).forEach(x => armTo(model, x, ...ARM.T)) },
      { id: 'front', name: 'Bras devant', run: s => sidesFor(s).forEach(x => armTo(model, x, ...ARM.front)) },
      { id: 'up', name: 'Bras levé', run: s => sidesFor(s).forEach(x => armTo(model, x, ...ARM.up)) },
      { id: 'elbow', name: 'Coude plié', run: s => sidesFor(s).forEach(x => armTo(model, x, null, [0.1, 0.05, 1])) },
    ],
    leg: [
      { id: 'straight', name: 'Jambe tendue', run: s => sidesFor(s).forEach(x => resetList(SIDE_BONES[x].leg)) },
      { id: 'fwd', name: 'Pas en avant', run: s => sidesFor(s).forEach(x => legStep(x, -28, 22, 6)) },
      { id: 'back', name: 'Pas en arrière', run: s => sidesFor(s).forEach(x => legStep(x, 22, 18, -10)) },
      { id: 'knee', name: 'Genou levé', run: s => sidesFor(s).forEach(x => legStep(x, -75, 85, -10)) },
    ],
    head: [
      { id: 'front', name: 'Tête droite', run: () => look(0, 0) },
      { id: 'left', name: 'Regarder à gauche', run: () => look(35, 0) },
      { id: 'right', name: 'Regarder à droite', run: () => look(-35, 0) },
      { id: 'up', name: 'Regarder en haut', run: () => look(0, -25) },
      { id: 'down', name: 'Regarder en bas', run: () => look(0, 22) },
    ],
    spine: [
      { id: 'straight', name: 'Dos droit', run: () => spine(() => {}) },
      { id: 'lean', name: 'Penché en avant', run: () => spine(() => { model.rotateWorld(Q('Spine'), [1, 0, 0], 10); model.rotateWorld(Q('Spine1'), [1, 0, 0], 12); }) },
      { id: 'proud', name: 'Torse bombé', run: () => spine(() => { model.rotateWorld(Q('Spine1'), [1, 0, 0], -8); model.rotateWorld(Q('Spine2'), [1, 0, 0], -8); }) },
      { id: 'twistL', name: 'Torsion à gauche', run: () => spine(() => { model.rotateWorld(Q('Spine'), [0, 1, 0], 10); model.rotateWorld(Q('Spine1'), [0, 1, 0], 14); }) },
      { id: 'twistR', name: 'Torsion à droite', run: () => spine(() => { model.rotateWorld(Q('Spine'), [0, 1, 0], -10); model.rotateWorld(Q('Spine1'), [0, 1, 0], -14); }) },
      { id: 'crouch', name: 'Accroupi', run: () => { model.hips.position.y = model.hips.userData.restP.y - hipsRestY() * 0.22; model.root.updateMatrixWorld(true); footFloor(); } },
      { id: 'stand', name: 'Debout', run: () => { model.hips.position.copy(model.hips.userData.restP); model.root.updateMatrixWorld(true); footFloor(); } },
    ],
    none: [
      { id: 'open', name: 'Mains ouvertes', run: () => curlTo(['L', 'R'], 0) },
      { id: 'fist', name: 'Poings fermés', run: () => curlTo(['L', 'R'], 1.6) },
      { id: 'grip', name: 'Prise d\'arme', run: () => curlTo(['L', 'R'], 1.1) },
    ],
  };
  const ZONE_TITLE = { hand: 'Main', arm: 'Bras', leg: 'Jambe', head: 'Tête', spine: 'Corps', none: 'Mains' };

  // ------------------------------------------------------------ panneau de droite
  const boneEuler = b => new THREE.Euler().setFromQuaternion(model.offset(b), 'XYZ');
  function renderPanel() {
    if (!ui.active) return;
    const b = ui.sel, st = opts.state(), zone = zoneOf(b), side = sideOf(b);
    $('pmBone').textContent = b ? label(b.name) : tr('Aucun os', 'No bone');
    $('pmMaya').textContent = b ? b.name.replace('QuickRigCharacter_', '') : '';
    $('pmReset').disabled = $('pmMirror').disabled = !b;
    const box = $('pmXf');
    if (!b) box.innerHTML = `<p class="muted" style="grid-column:1/-1">${tr('Clique sur une partie de l\'ork (ou une boule du squelette) pour la sélectionner. E : rotation, W : IK des mains, des pieds et du bassin.', 'Click a part of the ork (or a skeleton ball) to select it. E: rotate, W: IK for hands, feet and hips.')}</p>`;
    else {
      const e = boneEuler(b);
      box.innerHTML = ['x', 'y', 'z'].map(a => `<span class="ax ${a}">${a.toUpperCase()}</span>
        <input type="range" min="-180" max="180" step="0.5" value="${(e[a] * DEG).toFixed(1)}" data-pax="${a}" aria-label="Rotation ${a.toUpperCase()}">
        <input type="number" min="-180" max="180" step="1" value="${(e[a] * DEG).toFixed(1)}" data-pax="${a}" aria-label="Rotation ${a.toUpperCase()} (°)">`).join('');
    }
    $('pmCtxTitle').textContent = plasmaT(ZONE_TITLE[zone]) + (side ? ' ' + (window.plasmaLang === 'fr' ? { L: 'G', R: 'D' } : { L: 'L', R: 'R' })[side] : '');
    $('pmCtx').innerHTML = CTX[zone].map(p => `<button type="button" data-pctx="${p.id}">${esc(plasmaT(p.name))}</button>`).join('');
    const showCurl = zone === 'hand' || zone === 'none' || zone === 'arm';
    $('pmCurl').hidden = !showCurl;
    for (const s of ['L', 'R']) {
      const row = $('pmCurl' + s).closest('label');
      row.hidden = !!side && !ui.sym && side !== s && zone !== 'none';
      $('pmCurl' + s).value = st.curl[s];
      $('pmCurl' + s + 'Out').textContent = Math.round(st.curl[s] / 1.6 * 100) + ' %';
    }
    $('pmSym').checked = ui.sym;
    $('pmPins').checked = ui.pins;
    document.querySelectorAll('#tools [data-ptool]').forEach(x => x.setAttribute('aria-pressed', String(x.dataset.ptool === ui.tool)));
  }
  function syncSliders() {
    const b = ui.sel;
    if (!b) return;
    const e = boneEuler(b);
    $('pmXf').querySelectorAll('input').forEach(inp => { if (document.activeElement !== inp) inp.value = (e[inp.dataset.pax] * DEG).toFixed(1); });
  }
  $('pmXf').addEventListener('input', e => {
    const inp = e.target, b = ui.sel;
    if (!b || !inp.dataset.pax) return;
    const eu = boneEuler(b);
    eu[inp.dataset.pax] = THREE.MathUtils.degToRad(+inp.value || 0);
    b.quaternion.copy(b.userData.restQ).multiply(new THREE.Quaternion().setFromEuler(eu));
    if (ui.sym) model.mirrorBones([b]);
    if (fingerBone(b)) opts.state().fingers = 'pose';
    const twin = inp.type === 'range' ? inp.nextElementSibling : inp.previousElementSibling;
    if (twin) twin.value = inp.value;
    changed(false);
  });
  $('pmXf').addEventListener('change', () => changed(true));
  $('posePanel').addEventListener('click', e => {
    const t = e.target.closest('button');
    if (!t) return;
    if (t.dataset.pctx) {
      const b = ui.sel, list = CTX[zoneOf(b)], p = list.find(x => x.id === t.dataset.pctx);
      if (!p) return;
      p.run(sideOf(b));
      model.root.updateMatrixWorld(true);
      changed(true);
    } else if (t.id === 'pmReset' && ui.sel) { model.resetBone(ui.sel); if (ui.sym) model.mirrorBones([ui.sel]); changed(true); }
    else if (t.id === 'pmMirror' && ui.sel) {
      const chain = chainOf(ui.sel);
      model.mirrorBones(chain && ui.tool === 'ik' ? CHAINS[chain].map(n => model.bone(n)) : [ui.sel]);
      changed(true);
      toast(tr(`${label(ui.sel.name)} copié sur ${label(model.mirror.get(ui.sel).name)}`, `${label(ui.sel.name)} copied to ${label(model.mirror.get(ui.sel).name)}`));
    } else if (t.dataset.pmirror) {
      const st = opts.state(), c = st.curl, m = t.dataset.pmirror;
      model.mirrorPose(m);
      if (m === 'swap') st.curl = { L: c.R, R: c.L }; else if (m === 'LtoR') c.R = c.L; else c.L = c.R;
      changed(true);
    } else if (t.id === 'pmResetAll') {
      const st = opts.state();
      model.setPose(null); st.curl = { L: 0, R: 0 }; st.fingers = 'curl';
      changed(true);
    }
  });
  for (const s of ['L', 'R']) {
    $('pmCurl' + s).addEventListener('input', e => {
      const v = +e.target.value, sides = ui.sym ? ['L', 'R'] : [s];
      curlTo(sides, v);
      for (const x of sides) { $('pmCurl' + x).value = v; $('pmCurl' + x + 'Out').textContent = Math.round(v / 1.6 * 100) + ' %'; }
      changed(false);
    });
    $('pmCurl' + s).addEventListener('change', () => changed(true));
  }
  $('pmSym').addEventListener('change', e => { ui.sym = e.target.checked; keep(); renderPanel(); });
  $('pmPins').addEventListener('change', e => { ui.pins = e.target.checked; keep(); });

  // ------------------------------------------------------------ bibliothèque de poses (gauche)
  function renderLib() {
    if (!ui.active) return;
    const st = opts.state();
    const list = opts.list();
    const mine = list.filter(p => p.mine);
    const tile = p => `<button type="button" class="tile posetile" data-plib="${esc(p.id)}" aria-pressed="${st.poseId === p.id}"${p.mine ? ' data-mine="1"' : ''} title="${p.mine ? tr('Clic : appliquer · Clic droit : renommer, supprimer', 'Click: apply · Right click: rename, delete') : tr('Clic : appliquer', 'Click: apply')}"><img alt="" data-pose-thumb="${esc(p.id)}" src="${opts.thumb(p.id) || 'data:,'}"><span${p.mine ? ' data-noi18n' : ''}>${esc(p.mine ? p.name : plasmaT(p.name))}</span></button>`;
    $('poseLibCount').textContent = list.length;
    const save = ui.saving
      ? `<form class="posesave" id="poseSaveForm"><input type="text" id="poseSaveName" spellcheck="false" aria-label="${tr('Nom de la pose', 'Pose name')}" value="${esc(tr('Pose', 'Pose') + ' ' + (mine.length + 1))}"><button type="submit" class="primary">${tr('Enregistrer', 'Save')}</button><button type="button" id="poseSaveCancel" class="icon small" aria-label="${tr('Annuler', 'Cancel')}">${ICON('M6 6l12 12M18 6L6 18')}</button></form>`
      : '';
    $('poseLibBody').innerHTML = `${save}
      <h3>${tr('Poses de base', 'Base poses')}</h3><div class="libgrid posegrid">${list.filter(p => p.preset?.base).map(tile).join('')}</div>
      <h3>${tr('Poses de départ', 'Starter poses')}</h3><div class="libgrid posegrid">${list.filter(p => p.preset && !p.preset.base).map(tile).join('')}</div>
      <h3>${tr('Mes poses', 'My poses')} <span class="muted">${mine.length || ''}</span></h3><div class="libgrid posegrid">
        <button type="button" class="tile posetile addtile" id="poseAdd" title="${tr('Enregistrer la pose actuelle', 'Save the current pose')}"><span class="plus">${ICON('M12 5v14M5 12h14')}</span><span>${tr('Pose actuelle', 'Current pose')}</span></button>
        ${mine.map(tile).join('')}</div>`;
    if (ui.saving) { const i = $('poseSaveName'); i.focus(); i.select(); }
  }
  function saveCurrent(name) {
    const st = opts.state();
    const list = loadMyPoses();
    name = name.trim() || `Pose ${list.length + 1}`;
    const entry = { name, pose: model.getPose(), curl: { ...st.curl } };
    const i = list.findIndex(p => p.name === name);
    if (i >= 0) list[i] = entry; else list.push(entry);
    try { localStorage.setItem(MY_POSES, JSON.stringify(list)); } catch { toast(tr('Stockage indisponible', 'Storage unavailable')); return; }
    st.poseId = 'mine:' + name;
    ui.saving = false;
    opts.listChanged();
    renderLib();
    toast(tr(`Pose « ${name} » enregistrée`, `Pose "${name}" saved`));
  }
  $('poseLibBody').addEventListener('submit', e => { e.preventDefault(); saveCurrent($('poseSaveName').value); });
  $('poseLib').addEventListener('click', e => {
    const t = e.target.closest('button');
    if (!t) return;
    if (t.id === 'poseAdd' || t.id === 'poseAddTop') { ui.saving = true; renderLib(); }
    else if (t.id === 'poseSaveCancel') { ui.saving = false; renderLib(); }
    else if (t.dataset.plib) {
      const p = opts.list().find(x => x.id === t.dataset.plib);
      if (p) { opts.apply(p); renderPanel(); renderLib(); attach(); }
    }
  });
  $('poseLib').addEventListener('keydown', e => { if (e.key === 'Escape' && ui.saving) { ui.saving = false; renderLib(); } });

  // clic droit sur une de mes poses : renommer, supprimer (pas de prompt() dans l'app Windows)
  let menu = null;
  const closeMenu = () => { menu?.remove(); menu = null; removeEventListener('pointerdown', outside, true); };
  const outside = e => { if (menu && !menu.contains(e.target)) closeMenu(); };
  $('poseLib').addEventListener('contextmenu', e => {
    const t = e.target.closest('[data-plib]');
    if (!t) return;
    e.preventDefault();
    if (!t.dataset.mine) { toast(tr('Pose de départ : non modifiable. Applique-la puis enregistre ta version avec « + ».', 'Starter pose: read only. Apply it, then save your version with "+".')); return; }
    const name = t.dataset.plib.slice(5);
    closeMenu();
    menu = document.createElement('div');
    menu.className = 'ctxmenu';
    menu.setAttribute('role', 'menu');
    document.body.append(menu);
    const place = () => {
      const r = menu.getBoundingClientRect();
      menu.style.left = Math.max(8, Math.min(e.clientX, innerWidth - r.width - 8)) + 'px';
      menu.style.top = Math.max(8, Math.min(e.clientY, innerHeight - r.height - 8)) + 'px';
    };
    const main = () => {
      menu.innerHTML = `<div class="ctx-title" data-noi18n>${esc(name)}</div>
        <button type="button" role="menuitem" data-act="rename">${ICON('M4 20h4L19 9l-4-4L4 16zM14 6l4 4')}${tr('Renommer', 'Rename')}</button>
        <button type="button" role="menuitem" data-act="update">${ICON('M20 12a8 8 0 1 1-3-6.2M20 4v5h-5')}${tr('Remplacer par la pose actuelle', 'Replace with the current pose')}</button>
        <button type="button" role="menuitem" data-act="delete" class="danger">${ICON('M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13')}${tr('Supprimer', 'Delete')}</button>`;
      place();
    };
    main();
    addEventListener('pointerdown', outside, true);
    menu.addEventListener('keydown', ev => { if (ev.key === 'Escape') closeMenu(); });
    menu.addEventListener('click', ev => {
      const a = ev.target.closest('[data-act]')?.dataset.act;
      if (!a) return;
      const list = loadMyPoses(), i = list.findIndex(p => p.name === name);
      const write = () => { try { localStorage.setItem(MY_POSES, JSON.stringify(list)); } catch { /* ignoré */ } opts.listChanged(); renderLib(); };
      if (a === 'rename') {
        menu.innerHTML = `<div class="ctx-title">${tr('Renommer', 'Rename')}</div><input type="text" spellcheck="false" aria-label="${tr('Nouveau nom', 'New name')}" value="${esc(name)}">
          <div class="ctx-row"><button type="button" data-act="cancel">${tr('Annuler', 'Cancel')}</button><button type="button" data-act="dorename" class="go-btn">${tr('Renommer', 'Rename')}</button></div>`;
        place();
        const inp = menu.querySelector('input');
        inp.focus(); inp.select();
        inp.addEventListener('keydown', k => { if (k.key === 'Enter') { k.preventDefault(); menu.querySelector('[data-act="dorename"]').click(); } });
      } else if (a === 'dorename') {
        const nn = menu.querySelector('input').value.trim();
        closeMenu();
        if (!nn || nn === name || i < 0) return;
        if (list.some(p => p.name === nn)) { toast(tr(`Une pose « ${nn} » existe déjà`, `A pose "${nn}" already exists`)); return; }
        list[i].name = nn;
        const st = opts.state();
        if (st.poseId === 'mine:' + name) st.poseId = 'mine:' + nn;
        write();
      } else if (a === 'update') {
        closeMenu();
        if (i < 0) return;
        list[i] = { name, pose: model.getPose(), curl: { ...opts.state().curl } };
        write();
        toast(tr(`Pose « ${name} » mise à jour`, `Pose "${name}" updated`));
      } else if (a === 'delete') {
        menu.innerHTML = `<div class="ctx-title">${tr(`Supprimer « ${esc(name)} » ?`, `Delete "${esc(name)}"?`)}</div>
          <div class="ctx-row"><button type="button" data-act="cancel">${tr('Annuler', 'Cancel')}</button><button type="button" data-act="kill" class="danger-btn">${tr('Supprimer', 'Delete')}</button></div>`;
        place();
      } else if (a === 'kill') {
        closeMenu();
        if (i < 0) return;
        list.splice(i, 1);
        write();
        toast(tr(`Pose « ${name} » supprimée`, `Pose "${name}" deleted`));
      } else if (a === 'cancel') closeMenu();
    });
  });

  // ------------------------------------------------------------ outils, clavier
  function setTool(t) { ui.tool = t; keep(); attach(); renderPanel(); }
  $('tools').addEventListener('click', e => { const b = e.target.closest('[data-ptool]'); if (b && ui.active) setTool(b.dataset.ptool); });
  function onKey(e) {
    const k = e.key.toLowerCase();
    if (k === 'w') setTool('ik');
    else if (k === 'e' || k === 'r') setTool('fk');
    else if (k === 'escape') select(null);
    else if (k === 'm' && ui.sel) $('pmMirror').click();
    else if (k === 'f') vp.frameObject(model.root);
    else if (k === 'arrowup' && ui.sel?.parent?.isBone) select(ui.sel.parent);
    else if (k === 'arrowdown' && ui.sel) { const c = ui.sel.children.find(x => x.isBone); if (c) select(c); }
    else return false;
    return true;
  }

  // ------------------------------------------------------------ entrée / sortie du mode
  function enter() {
    ui.active = true;
    patchOrk();
    viz.visible = true;
    updateViz();
    highlight(true);
    attach();
    renderPanel();
    renderLib();
  }
  function leave() {
    ui.active = false;
    ui.saving = false;
    closeMenu();
    viz.visible = false;
    gizmo.detach();
    highlight(false);
    $('vpTip').hidden = true;
  }
  // après un annuler / rétablir, un changement de matériaux
  function refresh() {
    if (!ui.active) return;
    patchOrk();
    attach();
    renderPanel();
    renderLib();
  }
  return { enter, leave, refresh, renderLib, onKey, update: updateViz, highlight: on => highlight(on && ui.active), get active() { return ui.active; }, ui, select };
}
