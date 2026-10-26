// Rig Lab : pose de l'ork riggé importé de Maya. Même viewport que le builder (viewport.js / render.js).
// - clic sur la figurine : l'os dont l'influence domine sous la souris (lu dans les poids du low) ;
// - outil Rotation (R) : gizmo de rotation locale sur l'os ; outil IK (G) : on déplace la main ou le pied,
//   bras / jambe suivent (IK deux os, le coude garde son côté, la main garde son orientation) ;
// - bassin : se déplace en IK, les pieds restent ancrés au sol si l'option est cochée ;
// - symétrie, fermeture des mains, poses de départ et poses enregistrées, export STL posé.
import * as THREE from 'three';
import { Viewport } from '../viewport.js';
import { exportMerged, exportJSON } from '../io.js';
import { OrkModel, GROUPS, CHAINS, label } from './model.js';

const $ = id => document.getElementById(id);
const DEG = THREE.MathUtils.RAD2DEG;
const ACCENT = new THREE.Color(0xf0435a);
const STORE = 'plasmaork.riglab.poses';
const HIPS = 'QuickRigCharacter_Hips';

const vp = new Viewport($('vpCanvas'));
// Comme le builder : le path tracing n'est proposé que dans l'app Windows (desktop/).
if (!window.plasmaDesktop) $('qualitySel').querySelector('option[value="pt"]').remove();
vp.controls.target.set(0, 1.0, 0);
let model = null;
const state = {
  tool: 'fk', mesh: 'high', shade: 'resin', bones: true, weights: false,
  sel: null, hover: null, curl: { L: 0, R: 0 }, preset: null,
};

// ---------------------------------------------------------------- surbrillance de l'influence d'un os
// Les matériaux du viewport reçoivent deux uniformes partagés : l'os survolé et l'os sélectionné.
// Le vertex shader lit skinIndex / skinWeight pour connaître le poids de cet os à chaque sommet.
const HL = { uSel: { value: -1 }, uHov: { value: -1 }, uWeights: { value: 0 }, uAccent: { value: ACCENT.clone().convertSRGBToLinear() } };
function patch(mat) {
  if (mat.userData.riglab) return mat;
  mat.userData.riglab = true;
  mat.onBeforeCompile = sh => {
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
      .replace('#include <common>', '#include <common>\nuniform float uWeights;\nuniform vec3 uAccent;\nvarying float vSelW;\nvarying float vHovW;')
      .replace('#include <color_fragment>', `#include <color_fragment>
        if (uWeights > 0.5) {
          vec3 cold = vec3(0.02, 0.03, 0.08), warm = uAccent, hot = vec3(1.0, 0.85, 0.3);
          diffuseColor.rgb = vSelW < 0.6 ? mix(cold, warm, vSelW / 0.6) : mix(warm, hot, (vSelW - 0.6) / 0.4);
        }`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
        totalEmissiveRadiance += uAccent * (uWeights > 0.5 ? 0.0 : 0.55 * vSelW) + vec3(0.18) * vHovW * (1.0 - vSelW);`);
  };
  mat.customProgramCacheKey = () => 'riglab';
  mat.needsUpdate = true;
  return mat;
}
const TAGS = { BODY_high: 'skin', BODY_low: 'skin', HEAD_high: 'skin', HEAD_low: 'skin', TEETH: 'bone', EYES: 'eye' };
function applyMaterials() {
  vp.setMode(state.shade);
  for (const [n, m] of Object.entries(model.meshes)) m.material = patch(vp.material(TAGS[n] || 'skin'));
}
function applyMeshes() {
  const high = state.mesh === 'high';
  for (const [n, m] of Object.entries(model.meshes)) m.visible = /_high$/.test(n) ? high : /_low$/.test(n) ? !high : true;
  document.querySelectorAll('#meshSel button').forEach(b => b.setAttribute('aria-pressed', b.dataset.mesh === state.mesh));
}

// ---------------------------------------------------------------- os affichés (rayons X)
const boneViz = new THREE.Group();
vp.overlay.add(boneViz);
const vizMat = () => new THREE.MeshBasicMaterial({ color: 0xb5b7bd, depthTest: false, transparent: true, opacity: 0.85 });
let spheres = [], lines = null;
function buildBoneViz() {
  const geo = new THREE.SphereGeometry(1, 12, 8);
  spheres = model.bones.map(b => {
    const s = new THREE.Mesh(geo, vizMat());
    s.userData.bone = b;
    s.renderOrder = 10;
    boneViz.add(s);
    return s;
  });
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(model.bones.length * 6), 3));
  lines = new THREE.LineSegments(g, new THREE.LineBasicMaterial({ color: 0x8e9097, depthTest: false, transparent: true, opacity: 0.7 }));
  lines.renderOrder = 9;
  boneViz.add(lines);
}
const _p = new THREE.Vector3(), _p2 = new THREE.Vector3();
function updateBoneViz() {
  boneViz.visible = state.bones;
  if (!state.bones) return;
  const pos = lines.geometry.attributes.position;
  model.bones.forEach((b, i) => {
    const s = spheres[i];
    b.getWorldPosition(s.position);
    const finger = /^joint\d+$/.test(b.name) && !['joint40', 'joint41'].includes(b.name);
    s.scale.setScalar((finger ? 0.008 : 0.018) * (b === state.sel ? 1.5 : b === state.hover ? 1.3 : 1));
    s.material.color.set(b === state.sel ? ACCENT : b === state.hover ? 0xffffff : posed(b) ? 0x7bc67e : 0xb5b7bd);
    const p = b.parent && b.parent.isBone ? b.parent.getWorldPosition(_p2) : s.position;
    pos.setXYZ(i * 2, p.x, p.y, p.z);
    pos.setXYZ(i * 2 + 1, s.position.x, s.position.y, s.position.z);
  });
  pos.needsUpdate = true;
}
function posed(b) {
  return b.quaternion.angleTo(b.userData.restQ) > 1e-3 || (b.name === HIPS && b.position.distanceToSquared(b.userData.restP) > 1e-6);
}

// ---------------------------------------------------------------- historique
const undo = [], redo = [];
function snapshot() { return { pose: model.getPose(), curl: { ...state.curl } }; }
function pushUndo() { undo.push(snapshot()); if (undo.length > 100) undo.shift(); redo.length = 0; syncHistory(); }
function restore(s) { model.setPose(s.pose); state.curl = { ...s.curl }; afterChange(); }
function syncHistory() { $('undoBtn').disabled = !undo.length; $('redoBtn').disabled = !redo.length; }
$('undoBtn').addEventListener('click', () => { if (!undo.length) return; redo.push(snapshot()); restore(undo.pop()); syncHistory(); });
$('redoBtn').addEventListener('click', () => { if (!redo.length) return; undo.push(snapshot()); restore(redo.pop()); syncHistory(); });

// ---------------------------------------------------------------- sélection et gizmo
const gizmo = vp.gizmo;
const ikTarget = new THREE.Object3D();
let pins = null;   // positions des pieds à garder pendant un déplacement du bassin

function chainOf(b) {
  for (const [id, names] of Object.entries(CHAINS)) if (names.includes(b.name)) return id;
  return null;
}
function select(b) {
  state.sel = b;
  HL.uSel.value = b ? model.bones.indexOf(b) : -1;
  attachGizmo();
  renderBonePanel();
  document.querySelectorAll('#boneTree .list button').forEach(x => x.setAttribute('aria-current', x.dataset.bone === b?.name));
  if (b) {
    const btn = document.querySelector(`#boneTree button[data-bone="${b.name}"]`);
    if (btn) { btn.closest('details').open = true; btn.scrollIntoView({ block: 'nearest' }); }
  }
}
function attachGizmo() {
  const b = state.sel;
  gizmo.detach();
  if (!b) return;
  const chain = chainOf(b);
  if (state.tool === 'ik' && (chain || b.name === HIPS)) {
    if (b.name === HIPS) { gizmo.setMode('translate'); gizmo.setSpace('world'); gizmo.attach(b); return; }
    const end = model.bone(CHAINS[chain][2]);
    model.figurePos(end, ikTarget.position);
    ikTarget.userData.chain = chain;
    gizmo.setMode('translate'); gizmo.setSpace('world'); gizmo.attach(ikTarget);
    return;
  }
  gizmo.setMode('rotate'); gizmo.setSpace('local'); gizmo.attach(b);
}
gizmo.addEventListener('mouseDown', () => {
  pushUndo();
  state.preset = null;
  pins = $('pinChk').checked ? { footL: model.figurePos(model.bone(CHAINS.footL[2])), footR: model.figurePos(model.bone(CHAINS.footR[2])) } : null;
});
gizmo.addEventListener('mouseUp', () => { pins = null; renderPresets(); });
gizmo.addEventListener('objectChange', () => {
  const b = state.sel;
  if (!b) return;
  if (gizmo.object === ikTarget) {
    model.solveIK(ikTarget.userData.chain, ikTarget.position);
    if ($('symChk').checked) model.mirrorBones(CHAINS[ikTarget.userData.chain].map(n => model.bone(n)));
  } else {
    if ($('symChk').checked) model.mirrorBones([b]);
    if (pins && (b.name === HIPS || b === model.hips)) for (const id of ['footL', 'footR']) model.solveIK(id, pins[id]);
  }
  afterChange(false);
});

function afterChange(rebuildPanel = true) {
  model.root.updateMatrixWorld(true);
  if (gizmo.object === ikTarget && !gizmo.dragging && ikTarget.userData.chain) model.figurePos(model.bone(CHAINS[ikTarget.userData.chain][2]), ikTarget.position);
  if (rebuildPanel) renderBonePanel(); else syncBoneSliders();
  $('curlL').value = state.curl.L; $('curlR').value = state.curl.R;
  $('curlLOut').textContent = Math.round(state.curl.L / 1.6 * 100) + ' %';
  $('curlROut').textContent = Math.round(state.curl.R / 1.6 * 100) + ' %';
  document.querySelectorAll('#boneTree .list button').forEach(x => x.classList.toggle('posed', posed(model.bone(x.dataset.bone))));
  save();
}

// ---------------------------------------------------------------- panneau de l'os : rotations X / Y / Z (degrés, relatives au repos)
function boneEuler(b) { return new THREE.Euler().setFromQuaternion(model.offset(b), 'XYZ'); }
function renderBonePanel() {
  const b = state.sel, box = $('boneXf');
  $('curBone').textContent = b ? label(b.name) : 'Aucun os';
  $('curMaya').textContent = b ? b.name.replace('QuickRigCharacter_', '') : '';
  $('curMaya').title = b ? b.name : '';
  $('resetBoneBtn').disabled = $('mirrorBoneBtn').disabled = !b;
  box.className = 'xf rot';
  if (!b) { box.innerHTML = '<span class="muted" style="grid-column:1/-1">Clique sur une partie de l\'ork pour la sélectionner.</span>'; return; }
  const e = boneEuler(b);
  box.innerHTML = ['x', 'y', 'z'].map(a => `
    <span class="ax ${a}">${a.toUpperCase()}</span>
    <input type="range" min="-180" max="180" step="0.5" value="${(e[a] * DEG).toFixed(1)}" data-ax="${a}" aria-label="Rotation ${a.toUpperCase()}">
    <input type="number" min="-180" max="180" step="1" value="${(e[a] * DEG).toFixed(1)}" data-axn="${a}" aria-label="Rotation ${a.toUpperCase()} en degrés">`).join('');
  box.querySelectorAll('input').forEach(inp => {
    inp.addEventListener('pointerdown', () => pushUndo());
    inp.addEventListener('focus', () => { if (inp.type === 'number') pushUndo(); });
    inp.addEventListener('input', () => {
      const e2 = boneEuler(b);
      const ax = inp.dataset.ax || inp.dataset.axn;
      e2[ax] = THREE.MathUtils.degToRad(+inp.value || 0);
      b.quaternion.copy(b.userData.restQ).multiply(new THREE.Quaternion().setFromEuler(e2));
      if ($('symChk').checked) model.mirrorBones([b]);
      state.preset = null;
      afterChange(false);
    });
  });
}
function syncBoneSliders() {
  const b = state.sel;
  if (!b) return;
  const e = boneEuler(b);
  $('boneXf').querySelectorAll('input').forEach(inp => {
    if (document.activeElement === inp) return;
    inp.value = (e[inp.dataset.ax || inp.dataset.axn] * DEG).toFixed(1);
  });
}
$('resetBoneBtn').addEventListener('click', () => { if (!state.sel) return; pushUndo(); model.resetBone(state.sel); afterChange(); });
$('mirrorBoneBtn').addEventListener('click', () => {
  if (!state.sel) return;
  pushUndo();
  const chain = chainOf(state.sel);
  model.mirrorBones(chain && state.tool === 'ik' ? CHAINS[chain].map(n => model.bone(n)) : [state.sel]);
  afterChange();
  toast(`${label(state.sel.name)} copié sur ${label(model.mirror.get(state.sel).name)}`);
});

// ---------------------------------------------------------------- arbre des os
function renderTree() {
  $('boneCount').textContent = model.bones.length;
  $('boneTree').innerHTML = GROUPS.map((g, i) => `
    <details ${i < 3 ? 'open' : ''}><summary>${g.label}</summary><div class="list">
      ${g.bones.map(n => `<button type="button" data-bone="${n}"><i></i>${label(n)}</button>`).join('')}
    </div></details>`).join('');
  $('boneTree').addEventListener('click', e => {
    const b = e.target.closest('button[data-bone]');
    if (b) select(model.bone(b.dataset.bone));
  });
  $('boneTree').addEventListener('pointerover', e => {
    const b = e.target.closest('button[data-bone]');
    setHover(b ? model.bone(b.dataset.bone) : null);
  });
  $('boneTree').addEventListener('pointerleave', () => setHover(null));
}
function setHover(b) {
  state.hover = b;
  HL.uHov.value = b ? model.bones.indexOf(b) : -1;
}

// ---------------------------------------------------------------- picking dans le viewport
const ray = new THREE.Raycaster();
const ndc = new THREE.Vector2();
function pick(ev) {
  const r = vp.canvas.getBoundingClientRect();
  ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, vp.camera);
  if (state.bones) {
    // les os sont en rayons X : on prend la sphère la plus proche du rayon, dans un petit rayon écran
    let best = null, bd = Infinity;
    for (const s of spheres) {
      const d = ray.ray.distanceSqToPoint(s.position), rad = s.scale.x * 1.6;
      if (d < rad * rad && d < bd) { bd = d; best = s.userData.bone; }
    }
    if (best) return best;
  }
  const targets = [model.meshes.BODY_low, model.meshes.HEAD_low];
  for (const m of targets) { m.boundingSphere = null; m.boundingBox = null; }
  const hits = ray.intersectObjects(targets, false);
  if (!hits.length) return null;
  const h = hits[0];
  if (h.object.name === 'HEAD_low') return model.bone('QuickRigCharacter_Head');
  return model.boneAtFace(h.face, h.barycoord);
}
let down = null, hoverQueued = null;
vp.canvas.addEventListener('pointerdown', e => {
  if (e.button !== 0) return;
  down = { x: e.clientX, y: e.clientY, gizmo: gizmo.dragging || gizmo.axis !== null };
});
vp.canvas.addEventListener('pointerup', e => {
  if (!down || e.button !== 0) return;
  const moved = Math.hypot(e.clientX - down.x, e.clientY - down.y) > 5;
  if (!down.gizmo && !moved && model) select(pick(e));
  down = null;
});
vp.canvas.addEventListener('pointermove', e => {
  if (!model || gizmo.dragging || e.buttons) { $('vpTip').hidden = true; return; }
  if (hoverQueued) { hoverQueued.e = e; return; }
  hoverQueued = { e };
  requestAnimationFrame(() => {
    const ev = hoverQueued.e;
    hoverQueued = null;
    const b = gizmo.axis !== null ? null : pick(ev);
    setHover(b);
    const tip = $('vpTip'), r = vp.canvas.getBoundingClientRect();
    tip.hidden = !b;
    if (b) { tip.textContent = label(b.name); tip.style.left = (ev.clientX - r.left) + 'px'; tip.style.top = (ev.clientY - r.top) + 'px'; }
  });
});
vp.canvas.addEventListener('pointerleave', () => { setHover(null); $('vpTip').hidden = true; });

// ---------------------------------------------------------------- outils, affichage
function setTool(t) {
  state.tool = t;
  document.querySelectorAll('#tools button').forEach(b => b.setAttribute('aria-pressed', b.dataset.tool === t));
  attachGizmo();
}
$('tools').addEventListener('click', e => { const b = e.target.closest('button[data-tool]'); if (b) setTool(b.dataset.tool); });
$('meshSel').addEventListener('click', e => { const b = e.target.closest('button[data-mesh]'); if (b) { state.mesh = b.dataset.mesh; applyMeshes(); } });
$('shading').addEventListener('click', e => {
  const b = e.target.closest('button[data-shade]');
  if (!b) return;
  state.shade = b.dataset.shade;
  document.querySelectorAll('#shading [data-shade]').forEach(x => x.setAttribute('aria-pressed', x === b));
  applyMaterials();
});
$('lightBtn').addEventListener('click', () => vp.rotateLight());
$('frameBtn').addEventListener('click', () => vp.frameObject(model.root));
function toggleBones() { state.bones = !state.bones; $('bonesBtn').setAttribute('aria-pressed', state.bones); }
function toggleWeights() {
  state.weights = !state.weights;
  HL.uWeights.value = state.weights ? 1 : 0;
  $('weightsBtn').setAttribute('aria-pressed', state.weights);
  if (state.weights && !state.sel) toast('Sélectionne un os pour voir ses poids');
}
$('bonesBtn').addEventListener('click', toggleBones);
$('weightsBtn').addEventListener('click', toggleWeights);
$('qualitySel').addEventListener('change', e => vp.setQuality(e.target.value));

// ---------------------------------------------------------------- mains, miroir, poses
for (const side of ['L', 'R']) {
  const inp = $('curl' + side);
  inp.addEventListener('pointerdown', () => pushUndo());
  inp.addEventListener('input', () => { state.curl[side] = +inp.value; model.curl(side, state.curl[side]); state.preset = null; afterChange(false); });
}
document.querySelectorAll('[data-mirror]').forEach(b => b.addEventListener('click', () => {
  pushUndo();
  model.mirrorPose(b.dataset.mirror);
  const c = state.curl;
  if (b.dataset.mirror === 'swap') state.curl = { L: c.R, R: c.L };
  else if (b.dataset.mirror === 'LtoR') c.R = c.L; else c.L = c.R;
  state.preset = null;
  afterChange();
}));
$('resetAllBtn').addEventListener('click', () => { pushUndo(); model.setPose(null); state.curl = { L: 0, R: 0 }; state.preset = 'repos'; afterChange(); renderPresets(); });

// Poses de départ, construites par rotations dans l'espace de la figurine (gauche = +x, avant = +z, cm).
// Les bras du rig sont au repos en A ; le genou et le coude plient autour de x.
const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
const PRESETS = [
  { id: 'repos', name: 'Repos', build: () => {} },
  { id: 'garde', name: 'Garde', curl: [1.1, 1.1], build: m => {
    m.hips.position.y -= 0.9;
    for (const s of ['', '1']) {
      m.rotateWorld('QuickRigCharacter_LeftUpLeg' + s, X, -18);
      m.rotateWorld('QuickRigCharacter_LeftLeg' + s, X, 30);
      m.rotateWorld('QuickRigCharacter_LeftFoot' + s, X, -12);
    }
    m.rotateWorld('QuickRigCharacter_Spine1', X, 8);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -25); m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -55);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, -25); m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -55);
  } },
  { id: 'charge', name: 'Charge', curl: [1.4, 1.2], build: m => {
    m.hips.position.y -= 0.6; m.hips.position.z += 1.2;
    m.rotateWorld(HIPS, X, 10);
    m.rotateWorld('QuickRigCharacter_Spine', X, 12);
    m.rotateWorld('QuickRigCharacter_Neck', X, -8);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', X, -55); m.rotateWorld('QuickRigCharacter_LeftLeg', X, 60);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg1', X, 20); m.rotateWorld('QuickRigCharacter_LeftLeg1', X, 45);
    m.rotateWorld('QuickRigCharacter_LeftFoot1', X, 25);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -95); m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -30);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, 35); m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -70);
  } },
  { id: 'waaagh', name: 'Waaagh !', curl: [1.5, 1.5], build: m => {
    m.rotateWorld('QuickRigCharacter_Spine2', X, -10);
    m.rotateWorld('QuickRigCharacter_Neck', X, -22);
    m.rotateWorld('QuickRigCharacter_LeftArm', Z, 105); m.rotateWorld('QuickRigCharacter_LeftForeArm', Z, 35);
    m.rotateWorld('QuickRigCharacter_LeftArm1', Z, -105); m.rotateWorld('QuickRigCharacter_LeftForeArm1', Z, -35);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', Z, 8); m.rotateWorld('QuickRigCharacter_LeftUpLeg1', Z, -8);
    m.hips.position.y -= 0.4;
  } },
  { id: 'tir', name: 'Tir', curl: [1.2, 0.9], build: m => {
    m.rotateWorld('QuickRigCharacter_Spine1', Y, 18);
    m.rotateWorld('QuickRigCharacter_Neck', Y, -14);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, -70); m.rotateWorld('QuickRigCharacter_LeftArm1', Y, 15);
    m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -15);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -55); m.rotateWorld('QuickRigCharacter_LeftArm', Y, -30);
    m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -50);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', X, -15); m.rotateWorld('QuickRigCharacter_LeftLeg', X, 18);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg1', X, 12); m.rotateWorld('QuickRigCharacter_LeftLeg1', X, 10);
  } },
];
function applyPreset(p) {
  pushUndo();
  model.setPose(null);
  p.build(model);
  model.root.updateMatrixWorld(true);
  // les pieds restent posés : on ramène les chevilles à hauteur du repos sans changer leur x / z
  if (p.id !== 'repos') for (const id of ['footL', 'footR']) {
    const end = model.bone(CHAINS[id][2]), t = model.figurePos(end);
    t.y = end.userData.restW.y;
    model.solveIK(id, t);
  }
  state.curl = { L: p.curl?.[0] || 0, R: p.curl?.[1] || 0 };
  model.curl('L', state.curl.L); model.curl('R', state.curl.R);
  state.preset = p.id;
  afterChange();
  renderPresets();
}
function renderPresets() {
  $('presetRow').innerHTML = PRESETS.map(p => `<button type="button" data-preset="${p.id}" aria-pressed="${state.preset === p.id}">${p.name}</button>`).join('');
}
$('presetRow').addEventListener('click', e => { const b = e.target.closest('[data-preset]'); if (b) applyPreset(PRESETS.find(p => p.id === b.dataset.preset)); });

// Poses enregistrées dans le navigateur.
function loadMine() { try { return JSON.parse(localStorage.getItem(STORE) || '[]'); } catch { return []; } }
function saveMine(list) { try { localStorage.setItem(STORE, JSON.stringify(list)); } catch { /* stockage indisponible */ } }
function renderMine() {
  const list = loadMine();
  $('myPoses').innerHTML = list.length ? list.map((p, i) => `
    <div class="row"><button type="button" data-load="${i}">${p.name.replace(/</g, '&lt;')}</button>
    <button type="button" class="icon tiny" data-del="${i}" aria-label="Supprimer ${p.name.replace(/"/g, '')}" title="Supprimer"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/></svg></button></div>`).join('')
    : '<span class="empty">Aucune pose enregistrée.</span>';
}
$('savePoseBtn').addEventListener('click', () => {
  const list = loadMine();
  const name = $('poseNameInput').value.trim() || `Pose ${list.length + 1}`;
  const i = list.findIndex(p => p.name === name);
  const entry = { name, ...snapshot() };
  if (i >= 0) list[i] = entry; else list.push(entry);
  saveMine(list);
  renderMine();
  toast(`Pose « ${name} » enregistrée`);
});
$('myPoses').addEventListener('click', e => {
  const l = e.target.closest('[data-load]'), d = e.target.closest('[data-del]');
  const list = loadMine();
  if (l) { const p = list[+l.dataset.load]; pushUndo(); restore(p); $('poseNameInput').value = p.name; state.preset = null; renderPresets(); }
  if (d) { list.splice(+d.dataset.del, 1); saveMine(list); renderMine(); }
});

// La pose en cours survit au rechargement.
let saveTimer = 0;
function save() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => { try { localStorage.setItem(STORE + '.current', JSON.stringify(snapshot())); } catch { /* stockage indisponible */ } }, 300);
}

// ---------------------------------------------------------------- export
const menu = $('exportMenu');
$('exportMenuBtn').addEventListener('click', () => { menu.hidden = !menu.hidden; $('exportMenuBtn').setAttribute('aria-expanded', !menu.hidden); });
document.addEventListener('click', e => { if (!e.target.closest('.export')) { menu.hidden = true; $('exportMenuBtn').setAttribute('aria-expanded', 'false'); } });
$('exportBtn').addEventListener('click', () => exportSTL());
menu.addEventListener('click', e => {
  const b = e.target.closest('[data-export]');
  if (!b) return;
  menu.hidden = true;
  if (b.dataset.export === 'stl') exportSTL();
  if (b.dataset.export === 'pose') exportJSON({ type: 'plasmaork-pose', rig: 'BOY_32mm', name: $('poseNameInput').value, ...snapshot() }, $('poseNameInput').value || 'pose');
  if (b.dataset.export === 'import') $('poseInput').click();
});
$('poseInput').addEventListener('change', async e => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) return;
  try {
    const data = JSON.parse(await f.text());
    if (!data.pose?.rot) throw new Error('pas une pose');
    pushUndo(); restore(data);
    if (data.name) $('poseNameInput').value = data.name;
    toast(`Pose « ${data.name || f.name} » importée`);
  } catch { toast('Ce fichier n\'est pas une pose du Rig Lab'); }
});
// L'échelle est la hauteur des yeux (convention du builder) ; le GLB donne yeux et sol en cm.
function mmPerUnit() {
  const eyeUnits = (model.info.eyeHeight - model.info.floor) * model.k;
  return +$('scaleSel').value / eyeUnits;
}
async function exportSTL() {
  const names = state.mesh === 'high' ? ['BODY_high', 'HEAD_high', 'TEETH', 'EYES'] : ['BODY_low', 'HEAD_low', 'TEETH', 'EYES'];
  toast('Export en cours…');
  await new Promise(r => setTimeout(r, 30));
  const name = ($('poseNameInput').value || 'ork_pose').replace(/[^\w-]+/g, '_');
  const tris = await exportMerged(model.bakedMeshes(names), mmPerUnit(), name);
  if (tris) toast(`${name}.stl : ${Math.round(tris).toLocaleString('fr-FR')} triangles, ${$('scaleSel').value} mm`);
}

// ---------------------------------------------------------------- clavier
addEventListener('keydown', e => {
  if (e.target.closest('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if ((e.ctrlKey || e.metaKey) && k === 'z') { e.preventDefault(); $(e.shiftKey ? 'redoBtn' : 'undoBtn').click(); return; }
  if ((e.ctrlKey || e.metaKey) && k === 'y') { e.preventDefault(); $('redoBtn').click(); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  if (k === 'r') setTool('fk');
  else if (k === 'g') setTool('ik');
  else if (k === 'f') $('frameBtn').click();
  else if (k === 'b') toggleBones();
  else if (k === 'w') toggleWeights();
  else if (k === 'm' && state.sel) $('mirrorBoneBtn').click();
  else if (k === 'escape') select(null);
  else if (k === 'arrowup' && state.sel?.parent?.isBone) select(state.sel.parent);
  else if (k === 'arrowdown' && state.sel) { const c = state.sel.children.find(x => x.isBone); if (c) select(c); }
});

let toastTimer = 0;
function toast(msg) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 2600);
}

// ---------------------------------------------------------------- démarrage
vp.onFrame = () => { if (model) updateBoneViz(); };
setTool('fk');
document.querySelector('#shading [data-shade="resin"]').setAttribute('aria-pressed', 'true');
syncHistory();
renderPresets();
renderMine();
renderBonePanel();
try {
  model = await OrkModel.load('assets/ork_boy.glb', e => { if (e.total) $('loadBar').style.setProperty('--p', (e.loaded / e.total * 100).toFixed(0) + '%'); });
  model.fit();
  vp.scene.add(model.root);
  model.ork.add(ikTarget);
  applyMaterials();
  applyMeshes();
  buildBoneViz();
  renderTree();
  try {
    const cur = JSON.parse(localStorage.getItem(STORE + '.current') || 'null');
    if (cur?.pose) { model.setPose(cur.pose); state.curl = cur.curl || state.curl; }
  } catch { /* rien à restaurer */ }
  afterChange();
  vp.frameObject(model.root, new THREE.Vector3(0.45, 0.2, 1), true);
  $('loading').hidden = true;
  $('vpInfo').textContent = `${model.bones.length} joints · skin Maya ${model.meshes.BODY_low.geometry.attributes.position.count.toLocaleString('fr-FR')} sommets · high ${(model.meshes.BODY_high.geometry.index.count / 3).toLocaleString('fr-FR')} triangles`;
  window.riglab = { model, vp, state, select, applyPreset: id => applyPreset(PRESETS.find(p => p.id === id)) };
} catch (err) {
  console.error(err);
  const l = $('loading');
  l.classList.add('err');
  l.textContent = 'Impossible de charger assets/ork_boy.glb. Sers le dossier web/plasma-builder avec un petit serveur (python -m http.server) et vérifie que le fichier est présent.';
}
