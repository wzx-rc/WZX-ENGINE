// PlasmaOrk Builder : l'ork riggé de Maya (assets/ork_boy.glb) comme base, sur lequel on accroche les briques de la
// bibliothèque (importées avec import.html) par leurs sockets, puis pose, rendu et export STL.
import * as THREE from 'three';
import { Viewport, Thumbs, RENDER_DEFAULTS } from './viewport.js';
import { Hold } from './hold.js';
import { TONEMAPPERS } from './render.js';
import { TestModel } from './testmodel.js';
import { OrkModel } from './riglab/model.js';
import { PRESETS, buildPreset, loadMyPoses } from './riglab/poses.js';
import { orkSockets, SOCKET_TYPES } from './sockets.js';
import { CATEGORIES, catById, Store, brickMeshes, refreshBrickMaterials, mat4, triCount } from './bricks.js';
import { exportMerged, exportParts, exportJSON } from './io.js';
import { brickMenu } from './brickmenu.js';
import { applyTheme, savedTheme, SOCKET_ICONS } from './atelier.js';

// Le path tracing n'est proposé que dans l'app Windows (desktop/ : Electron expose window.plasmaDesktop).
const DESKTOP = !!window.plasmaDesktop;
const $ = id => document.getElementById(id);
const ICON = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const D2R = Math.PI / 180;
const HEAD_MESHES = ['HEAD_low', 'HEAD_high', 'TEETH', 'EYES'];
const TAGS = { BODY_high: 'skin', BODY_low: 'skin', HEAD_high: 'skin', HEAD_low: 'skin', TEETH: 'bone', EYES: 'eye' };

// ---------- état ----------
// placed : une brique par socket, { brick, xf } ; xf = décalage manuel par rapport au socket (cm, degrés).
// Les sockets offerts par une brique posée s'appellent « socketParent>nom ».
export function defaultXf() { return { p: [0, 0, 0], r: [0, 0, 0], s: 1, m: [false, false, false] }; }
function freshState() { return { placed: {}, pose: null, poseId: 'garde', curl: { L: 1.1, R: 1.1 }, scale: 32 }; }
let state = freshState();
const ui = { cat: 'weapon', sel: null, tool: 'select', step: 'asm', tab: 'T', rig: false, sockets: true, view: 1, shade: 'resin', quality: 'rt', search: '', lod: 'high', test: false, info: '', theme: savedTheme(), drawer: true };
const STORE = 'plasmaork.builder';
try {
  const saved = JSON.parse(localStorage.getItem(STORE) || 'null');
  if (saved?.placed) state = Object.assign(freshState(), saved);
  const n = localStorage.getItem('plasmaork.name'); if (n) $('projName').value = n;
  const u = JSON.parse(localStorage.getItem(STORE + '.ui') || '{}');
  if (u.lod) ui.lod = u.lod;
} catch { /* stockage indisponible : on part de l'état par défaut */ }
let renderSet = { ...RENDER_DEFAULTS };
try { Object.assign(renderSet, JSON.parse(localStorage.getItem('plasmaork.render') || '{}')); if (renderSet.env === 'hdri') renderSet.env = 'studio'; } catch { /* idem */ }

// ---------- 3D ----------
const vp = new Viewport($('vpCanvas'));
vp.setRender(renderSet);
const thumbs = new Thumbs(160, 200);
thumbs.mode = renderSet.tonemapper;
const hold = new Hold(vp);
let testModel = null;
if (!DESKTOP) {
  $('qualitySel').querySelector('option[value="pt"]').remove();
  $('ptSet').hidden = true;
}
let model = null;          // OrkModel
let sockets = new Map();   // id -> { id, label, type, local (Matrix4), provider (Object3D), parent? }
let orkSocketDefs = [];
const recs = new Map();    // briques de la bibliothèque
const inst = {};           // socket id -> { holder, offset, content, rec }
const squash = {};
let skel = null;

// Hauteur des yeux en unités du viewport : c'est elle que l'échelle (32 / 54 / 75 mm) fixe.
const eyeUnits = () => (model.info.eyeHeight - model.info.floor) * model.k;
const mmPerUnit = () => state.scale / eyeUnits();
const rec = id => recs.get(id);

// Plug d'une brique (espace brique) : posée en place, c'est le socket de l'ork sur lequel elle a été modélisée.
function plugMatrix(r) {
  if (r.placement === 'inplace') {
    const home = orkSocketDefs.find(s => s.id === r.home) || orkSocketDefs.find(s => s.type === r.plug.type);
    if (home) return home.rest.clone();
  }
  return mat4(r.plug.m);
}
const compatible = (r, s) => s.type === 'any' || r.plug.type === 'any' || s.type === r.plug.type;

function rebuildSockets() {
  sockets = new Map();
  for (const s of orkSocketDefs) sockets.set(s.id, { ...s, provider: s.bone || model.ork });
  for (const sid of placedOrder()) {
    const r = rec(state.placed[sid].brick);
    for (const s of r?.sockets || []) {
      const id = sid + '>' + s.name;
      sockets.set(id, { id, label: `${r.name} · ${s.name}`, type: s.type, local: mat4(s.m), parent: sid });
    }
  }
}
const depth = id => id.split('>').length;
function placedOrder() { return Object.keys(state.placed).sort((a, b) => depth(a) - depth(b)); }
function providerOf(s) { return s.parent ? inst[s.parent]?.content : s.provider; }

// Une brique accrochée : holder (repère du socket) > offset (réglage manuel, gizmo) > content (plug⁻¹) > meshes.
function mountBrick(r, s, xf, ghostMat = null) {
  const holder = new THREE.Group();
  holder.matrixAutoUpdate = false;
  holder.matrix.copy(s.local);
  const offset = new THREE.Group();
  const content = new THREE.Group();
  content.matrixAutoUpdate = false;
  content.matrix.copy(plugMatrix(r)).invert();
  const meshes = brickMeshes(r, ui.lod, vp, { brick: r.id, socket: s.id });
  if (ghostMat) meshes.traverse(m => { if (m.isMesh) { m.material = ghostMat; m.userData = { helper: true, noExport: true }; } });
  content.add(meshes);
  offset.add(content);
  holder.add(offset);
  providerOf(s).add(holder);
  applyXf(offset, xf, 1);
  holder.updateMatrixWorld(true);
  return { holder, offset, content, meshes, rec: r };
}
function applyXf(offset, xf = defaultXf(), sq = 1) {
  offset.position.fromArray(xf.p);
  offset.rotation.set(xf.r[0] * D2R, xf.r[1] * D2R, xf.r[2] * D2R);
  offset.scale.set(xf.s * (xf.m[0] ? -1 : 1) / Math.sqrt(sq), xf.s * (xf.m[1] ? -1 : 1) * sq, xf.s * (xf.m[2] ? -1 : 1) / Math.sqrt(sq));
}

function rebuild() {
  for (const k of Object.keys(inst)) { inst[k].holder.removeFromParent(); delete inst[k]; }
  // briques supprimées de la bibliothèque, sockets disparus : on retire
  let changed = true;
  while (changed) {
    changed = false;
    rebuildSockets();
    for (const sid of Object.keys(state.placed)) if (!rec(state.placed[sid].brick) || !sockets.has(sid)) { delete state.placed[sid]; changed = true; }
  }
  for (const sid of placedOrder()) {
    const p = state.placed[sid];
    inst[sid] = mountBrick(rec(p.brick), sockets.get(sid), p.xf);
  }
  const headOff = !!state.placed.head;
  for (const [n, m] of Object.entries(model.meshes)) {
    const lodOk = /_high$/.test(n) ? ui.lod === 'high' : /_low$/.test(n) ? ui.lod === 'low' : true;
    m.visible = lodOk && !(headOff && HEAD_MESHES.includes(n));
  }
  if (ui.sel && !sockets.has(ui.sel)) ui.sel = null;
  setSelected(ui.sel);
  attachGizmo();
}

function applyMaterials() {
  vp.setMode(ui.shade);
  for (const [n, m] of Object.entries(model.meshes)) m.material = vp.material(TAGS[n] || 'skin');
  refreshBrickMaterials(model.root, vp, rec);
  testModel?.refresh();
}
function setSelected(id) {
  ui.sel = id;
  model.root.traverse(m => { if (m.isMesh && !m.userData.helper) m.userData.outline = !!id && m.userData.socket === id; });
}

// ---------- historique ----------
const history = [];
let hIndex = -1;
function save() {
  try {
    localStorage.setItem(STORE, JSON.stringify(state));
    localStorage.setItem(STORE + '.ui', JSON.stringify({ lod: ui.lod }));
    localStorage.setItem('plasmaork.name', $('projName').value);
  } catch { /* ignoré */ }
}
function commit() {
  history.splice(hIndex + 1);
  history.push(JSON.stringify(state));
  if (history.length > 80) history.shift();
  hIndex = history.length - 1;
  save();
  syncUndo();
}
function travel(d) {
  const i = hIndex + d;
  if (i < 0 || i >= history.length) return;
  hIndex = i;
  state = JSON.parse(history[i]);
  applyPose();
  rebuild();
  renderAll();
  syncUndo();
}
function syncUndo() { $('undoBtn').disabled = hIndex <= 0; $('redoBtn').disabled = hIndex >= history.length - 1; }

// ---------- pose ----------
// La pose vient du Rig Lab : poses de départ partagées, ou « Mes poses » enregistrées là-bas.
function poseList() { return [...PRESETS.map(p => ({ id: 'preset:' + p.id, name: p.name, preset: p })), ...loadMyPoses().map(p => ({ id: 'mine:' + p.name, name: p.name, mine: p }))]; }
function applyPose() {
  if (state.pose) { model.setPose(state.pose); }
  else {
    const p = PRESETS.find(x => 'preset:' + x.id === state.poseId) || PRESETS.find(x => x.id === state.poseId) || PRESETS[1];
    buildPreset(model, p);
  }
  model.curl('L', state.curl.L); model.curl('R', state.curl.R);
  model.root.updateMatrixWorld(true);
}
function setPose(entry) {
  if (entry.preset) { state.pose = null; state.poseId = entry.id; state.curl = { L: entry.preset.curl?.[0] || 0, R: entry.preset.curl?.[1] || 0 }; }
  else { state.pose = entry.mine.pose; state.poseId = entry.id; state.curl = { ...(entry.mine.curl || { L: 0, R: 0 }) }; }
  applyPose();
  commit();
  renderPoses(); renderOrk(); renderPrintInfo();
  scheduleFigureThumbs();
}

// ---------- vignettes de la figurine ----------
const VIEWS = [
  { name: 'Face', dir: [0, 0.12, 1] }, { name: 'Trois-quarts', dir: [0.7, 0.25, 1] },
  { name: 'Dos', dir: [-0.3, 0.2, -1] }, { name: 'Profil', dir: [1, 0.12, 0] },
];
let figureThumbTimer = 0;
function scheduleFigureThumbs() { clearTimeout(figureThumbTimer); figureThumbTimer = setTimeout(renderFigureThumbs, 500); }
function renderFigureThumbs() {
  if (!model || ui.test) return;
  const sel = ui.sel;
  setSelected(null);
  // les vignettes se rendent sur le low, bien plus léger
  const vis = Object.fromEntries(Object.entries(model.meshes).map(([n, m]) => [n, m.visible]));
  const headOff = !!state.placed.head;
  for (const [n, m] of Object.entries(model.meshes)) m.visible = !/_high$/.test(n) && !(headOff && HEAD_MESHES.includes(n));
  thumbs.setSize(160, 160);
  const views = VIEWS.map(v => thumbs.render(model.root, new THREE.Vector3(...v.dir), 1.0));
  thumbs.setSize(120, 170);
  const keep = { pose: model.getPose(), curl: { ...state.curl } };
  const poses = poseList().map(p => {
    if (p.preset) buildPreset(model, p.preset); else model.setPose(p.mine.pose);
    model.root.updateMatrixWorld(true);
    return thumbs.render(model.root, new THREE.Vector3(0.45, 0.15, 1), 1.0);
  });
  model.setPose(keep.pose);
  for (const [n, m] of Object.entries(model.meshes)) m.visible = vis[n];
  setSelected(sel);
  vp.scene.add(model.root);
  $('views').querySelectorAll('img').forEach((img, i) => { img.src = views[i]; });
  $('poseRow').querySelectorAll('img').forEach((img, i) => { if (poses[i]) img.src = poses[i]; });
}

// ---------- interface ----------
function renderRail() {
  const count = c => [...recs.values()].filter(r => r.cat === c).length;
  $('rail').innerHTML = CATEGORIES.map(c => `<button type="button" data-cat="${c.id}" aria-current="${ui.cat === c.id}">${ICON(c.icon)}${c.label}<span class="grow"></span><span class="muted">${count(c.id) || ''}</span></button>`).join('');
}
function renderLibrary() {
  const cat = catById(ui.cat);
  $('libTitle').textContent = cat.label;
  const q = ui.search.trim().toLowerCase();
  const all = [...recs.values()].filter(r => r.cat === cat.id);
  const list = all.filter(r => !q || r.name.toLowerCase().includes(q) || (r.tags || []).some(t => t.toLowerCase().includes(q)));
  $('libCount').textContent = all.length;
  const onSel = id => Object.values(state.placed).some(p => p.brick === id);
  $('libGrid').innerHTML = list.length ? list.map(r => `<button type="button" class="tile" title="Clic : poser · Glisser : choisir le socket" data-brick="${r.id}" aria-pressed="${onSel(r.id)}">${r.lods.high?.length ? '<em class="badge">HD</em>' : ''}${r.thumb ? `<img alt="" draggable="false" src="${r.thumb}">` : '<span class="ph"></span>'}<span>${esc(r.name)}</span></button>`).join('')
    : `<div class="libempty">${all.length ? 'Aucune brique ne correspond à la recherche.' : `Pas encore de brique dans « ${cat.label} ».<a href="import.html">Importer une brique</a>`}</div>`;
}
function renderSteps() {
  $('steps').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.step === ui.step)));
  $('tools').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === ui.tool)));
  $('socketsBtn').setAttribute('aria-pressed', String(ui.sockets));
  $('rigBtn').setAttribute('aria-pressed', String(ui.rig));
  $('shading').querySelectorAll('[data-shade]').forEach(b => b.classList.toggle('on', b.dataset.shade === ui.shade));
  $('texBtn').setAttribute('aria-pressed', String(ui.shade === 'paint'));
  $('qualitySel').value = ui.quality;
  document.body.dataset.step = ui.step;
  $('scaleSel').value = String(state.scale);
}
function renderViews() {
  if (!$('views').children.length) $('views').innerHTML = VIEWS.map((v, i) => `<button type="button" data-view="${i}" aria-label="Vue ${v.name}" title="${v.name}"><img alt=""></button>`).join('');
  $('views').querySelectorAll('button').forEach((b, i) => b.setAttribute('aria-pressed', String(i === ui.view)));
}
// Pastilles des sockets dans le viewport (libres : anneau creux ; occupés : plein).
function renderPins() {
  $('pins').innerHTML = [...sockets.values()].map((s, n) => `<button type="button" class="pin${state.placed[s.id] ? '' : ' free'}" data-socket="${s.id}" aria-pressed="${ui.sel === s.id}" aria-label="${n + 1} · ${esc(s.label)}" title="${esc(SOCKET_TYPES[s.type] || s.type)}"><i></i><b>${n + 1}</b><span>${esc(s.label)}</span></button>`).join('');
  $('pins').hidden = !ui.sockets || ui.test;
  renderEquip();
}
// Interface Atelier : la liste Équipement (un bouton par socket, mêmes numéros que les pastilles).
function renderEquip() {
  const list = [...sockets.values()];
  $('equipCount').textContent = `${list.filter(s => state.placed[s.id]).length} / ${list.length} posés`;
  $('equipList').innerHTML = list.map((s, n) => {
    const p = state.placed[s.id], r = p && rec(p.brick);
    const sub = r ? esc(r.name) : s.id === 'head' && !state.placed.head ? 'Tête d\u2019origine' : 'Vide · ' + esc(SOCKET_TYPES[s.type] || s.type);
    return `<button type="button" class="equip-row${r ? ' fitted' : ''}" data-socket="${s.id}" aria-pressed="${ui.sel === s.id}"><span class="equip-ic"><img alt="" src="${SOCKET_ICONS[s.type] || SOCKET_ICONS.any}"></span><span class="equip-txt"><b>${esc(s.label)}</b><small>${sub}</small></span><span class="equip-n">${n + 1}</span></button>`;
  }).join('');
  document.body.classList.toggle('drawer-closed', !ui.drawer);
  $('drawerOpen').hidden = ui.drawer || ui.theme !== 'atelier';
}

const XF_ROWS = [
  ['Position', 'X', 'p', 0, -10, 10, 0.05], ['', 'Y', 'p', 1, -10, 10, 0.05], ['', 'Z', 'p', 2, -10, 10, 0.05],
  ['Rotation', 'X', 'r', 0, -180, 180, 1], ['', 'Y', 'r', 1, -180, 180, 1], ['', 'Z', 'r', 2, -180, 180, 1],
  ['Échelle', '', 's', -1, 0.5, 2, 0.01],
];
function renderCustomize() {
  const s = ui.sel && sockets.get(ui.sel), p = ui.sel && state.placed[ui.sel], r = p && rec(p.brick);
  $('curSlot').textContent = s ? s.label : 'Ork';
  $('curName').textContent = r ? '· ' + r.name : s ? '· libre' : '';
  $('curSocket').textContent = s ? 'socket_' + s.id.replace('>', '.') : '';
  $('removeBtn').disabled = !r;
  document.querySelectorAll('.tabs button').forEach(b => b.setAttribute('aria-selected', String(b.dataset.tab === ui.tab)));
  for (const t of ['T', 'P', 'V']) $('pane-' + t).hidden = ui.tab !== t;
  if (r) {
    const xf = p.xf;
    $('xf').innerHTML = XF_ROWS.map(([lab, ax, k, i, min, max, step]) => {
      const val = i < 0 ? xf[k] : xf[k][i];
      return `<span>${lab}</span><span class="ax">${ax}</span><input type="range" id="xf-${k}${i}" data-k="${k}" data-i="${i}" min="${min}" max="${max}" step="${step}" value="${val}" aria-label="${lab || XF_ROWS.find(x => x[2] === k)[0]} ${ax}"><input type="number" data-k="${k}" data-i="${i}" min="${min}" max="${max}" step="${step}" value="${+(+val).toFixed(3)}" aria-label="Valeur ${ax || lab}">`;
    }).join('');
    document.querySelectorAll('[data-mirror]').forEach((b, i) => b.setAttribute('aria-pressed', String(xf.m[i])));
  } else {
    $('xf').innerHTML = `<p class="muted" style="grid-column:1/-1">${s ? 'Socket libre : choisis une brique dans la bibliothèque.' : 'Sélectionne une brique posée (clic dans le viewport ou dans le bandeau Assemblage) pour la régler. Position en cm, relative au socket.'}</p>`;
  }
  document.querySelector('.mirror').hidden = !r;
  renderPrintInfo();
}
function renderPrintInfo() {
  if (!model) return;
  const box = new THREE.Box3().setFromObject(model.root);
  const h = box.isEmpty() ? 0 : (box.max.y - box.min.y) * mmPerUnit();
  $('printHeight').textContent = `${h.toFixed(1)} mm (échelle ${state.scale} mm aux yeux)`;
  let t = 0;
  model.root.traverseVisible(m => { if (m.isMesh && !m.userData.helper) t += (m.geometry.index ? m.geometry.index.count : m.geometry.attributes.position.count) / 3; });
  $('printTris').textContent = Math.round(t).toLocaleString('fr-FR');
  $('printParts').textContent = `ork + ${Object.keys(state.placed).length} brique${Object.keys(state.placed).length > 1 ? 's' : ''}`;
  if (!ui.test) ui.info = `${Math.round(t / 1000)}k triangles · ${h.toFixed(0)} mm`;
  renderInfo();
  $('asmInfo').textContent = `${Math.round(t / 1000)}k triangles`;
}
const QUALITY = { rt: 'Temps réel', hq: 'Haute qualité', bhq: 'HQ BrickIt', pt: 'Path tracing' };
function renderInfo() {
  const s = vp.settings;
  let q = QUALITY[ui.quality];
  if (ui.quality === 'bhq' && vp.gi) q += ` · GI ${vp.gi.stats.state}`;
  if (ui.quality === 'pt') q += vp.ptLoading ? ' · chargement…' : vp.spp ? ` · ${Math.floor(vp.spp)} spp` : ' · préparation…';
  $('vpInfo').textContent = `${q} · ${TONEMAPPERS[s.tonemapper].name}\n${ui.info || ''}`;
}
const R_FMT = { ev: v => (v > 0 ? '+' : '') + (+v).toFixed(1) + ' EV', vignette: v => (+v).toFixed(2), envIntensity: v => (+v).toFixed(2), envRotation: v => Math.round(v * 180 / Math.PI) + '°', bounces: v => v, filterGlossy: v => (+v).toFixed(2), giStrength: v => (+v).toFixed(2), aoStrength: v => (+v).toFixed(2), aoRadius: v => (+v).toFixed(2) };
function renderRenderPanel() {
  const s = vp.settings;
  $('renderSet').querySelectorAll('[data-r]').forEach(el => { const k = el.dataset.r; if (el.type === 'checkbox') el.checked = !!s[k]; else el.value = String(s[k]); });
  $('renderSet').querySelectorAll('[data-o]').forEach(o => { o.textContent = R_FMT[o.dataset.o](s[o.dataset.o]); });
  $('bhqSet').hidden = ui.quality !== 'bhq';
  const hdriOpt = $('renderSet').querySelector('option[value="hdri"]');
  hdriOpt.disabled = !vp.studio.hdri;
  hdriOpt.textContent = vp.studio.hdri ? 'HDRI · ' + vp.studio.hdri.name : 'HDRI (à charger)';
}
function setRender(patch) {
  vp.setRender(patch);
  renderSet = { ...vp.settings };
  thumbs.mode = renderSet.tonemapper;
  try { localStorage.setItem('plasmaork.render', JSON.stringify(renderSet)); } catch { /* ignoré */ }
  renderRenderPanel();
  renderInfo();
}
function renderOrk() {
  $('lodSel').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.lod === ui.lod)));
  for (const side of ['L', 'R']) { $('curl' + side).value = state.curl[side]; $('curl' + side + 'Out').textContent = Math.round(state.curl[side] / 1.6 * 100) + ' %'; }
  if (!model) return;
  const tris = n => Math.round(model.meshes[n].geometry.index.count / 3 / 1000) + 'k';
  $('orkInfo').textContent = `Ork Boy riggé : ${model.bones.length} joints, low ${tris('BODY_low')} / high ${tris('BODY_high')} triangles.${state.placed.head ? ' Tête d\'origine masquée (brique de tête posée).' : ''}`;
}
function renderAssembly() {
  const ids = placedOrder();
  $('asmRow').innerHTML = ids.length ? ids.map(sid => {
    const r = rec(state.placed[sid].brick), s = sockets.get(sid);
    return `<div class="card"><button type="button" class="thumb" data-socket="${sid}" aria-pressed="${ui.sel === sid}" aria-label="${esc(s.label)} : ${esc(r.name)}">${r.thumb ? `<img alt="" src="${r.thumb}">` : ''}</button><span class="lbl">${esc(s.label)}</span><button type="button" class="x" data-clear="${sid}" aria-label="Retirer ${esc(r.name)}">${ICON('M6 6l12 12M18 6L6 18')}</button></div>`;
  }).join('') : '<p class="muted">Aucune brique posée : l\'ork est nu. Clique une brique de la bibliothèque.</p>';
}
function renderPoses() {
  const list = poseList();
  if ($('poseRow').children.length !== list.length) $('poseRow').innerHTML = list.map(p => `<div class="card"><button type="button" class="thumb" data-pose="${esc(p.id)}" aria-label="Pose ${esc(p.name)}"><img alt=""></button><span class="lbl">${esc(p.name)}</span></div>`).join('');
  $('poseRow').querySelectorAll('[data-pose]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.pose === state.poseId)));
  $('poseName').textContent = list.find(p => p.id === state.poseId)?.name || '';
}
function renderAll() {
  renderRail(); renderLibrary(); renderSteps(); renderViews(); renderPins(); renderCustomize(); renderOrk(); renderAssembly(); renderPoses(); renderRenderPanel();
  scheduleFigureThumbs();
}

// ---------- actions ----------
function select(id) {
  setSelected(id);
  const s = id && sockets.get(id);
  if (s) { const c = CATEGORIES.find(c => c.type === s.type); if (c) ui.cat = c.id; }
  attachGizmo();
  renderRail(); renderLibrary(); renderPins(); renderCustomize(); renderAssembly();
}
function attachGizmo() {
  const i = ui.sel && inst[ui.sel];
  if (ui.tool === 'select' || !i) { vp.gizmo.detach(); return; }
  vp.gizmo.setMode(ui.tool);
  vp.gizmo.attach(i.offset);
}
function place(sid, brickId) {
  const keep = state.placed[sid]?.brick === brickId ? state.placed[sid].xf : defaultXf();
  // les briques accrochées sur l'ancienne brique de ce socket tombent avec elle
  for (const k of Object.keys(state.placed)) if (k.startsWith(sid + '>')) delete state.placed[k];
  state.placed[sid] = { brick: brickId, xf: keep };
  rebuild();
  select(sid);
  commit();
  renderOrk(); renderAssembly(); renderLibrary();
  scheduleFigureThumbs();
}
function removeAt(sid) {
  for (const k of Object.keys(state.placed)) if (k === sid || k.startsWith(sid + '>')) delete state.placed[k];
  rebuild();
  commit();
  renderAll();
}
function renderGIInfo() {
  const g = vp.gi;
  $('giInfo').textContent = g ? `${g.count} sondes (${g.dims.x}×${g.dims.y}×${g.dims.z}) · ${g.stats.state}` : '';
}
function setStep(step) {
  ui.step = step;
  if (step === 'lib') { ui.sockets = true; ui.rig = false; $('searchInput').focus(); }
  if (step === 'asm') { ui.sockets = true; ui.rig = false; ui.tab = 'T'; }
  if (step === 'pose') { ui.sockets = false; ui.rig = true; }
  if (step === 'render') { ui.sockets = false; ui.rig = false; ui.tab = 'V'; if (ui.quality === 'rt') setQuality('hq'); }
  if (skel) skel.visible = ui.rig;
  renderSteps(); renderPins(); renderCustomize();
}
function setQuality(q) {
  ui.quality = q;
  vp.setQuality(q).then(renderInfo, err => { console.error(err); toast('Path tracing indisponible : ' + (err.message || err)); setQuality('hq'); });
  renderSteps(); renderPrintInfo(); renderRenderPanel();
}
function toast(msg) {
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast.timer); toast.timer = setTimeout(() => t.classList.remove('show'), 2600);
}

// ---------- pièce en main (BrickIt) ----------
// Clic sur une vignette : la brique vole jusqu'à son socket (le socket sélectionné s'il est compatible, sinon son
// socket d'origine) et s'y pose. Glisser une vignette : la brique suit le socket compatible le plus proche du pointeur.
const socketWorld = id => { const s = sockets.get(id); return new THREE.Vector3().setFromMatrixPosition(providerOf(s).matrixWorld.clone().multiply(s.local)); };
function targetsFor(r, auto) {
  const ok = [...sockets.values()].filter(s => compatible(r, s));
  if (!ok.length) return [];
  const first = (ui.sel && ok.find(s => s.id === ui.sel)) || ok.find(s => s.id === r.home && !state.placed[s.id]) || ok.find(s => !state.placed[s.id]) || ok.find(s => s.id === r.home) || ok[0];
  return auto ? [first.id] : [first.id, ...ok.filter(s => s !== first).map(s => s.id)];
}
function handObject(r, sid) {
  const s = sockets.get(sid);
  const tmp = mountBrick(r, s, defaultXf());
  tmp.holder.updateMatrixWorld(true);
  const at = socketWorld(sid);
  const inner = new THREE.Group();
  inner.matrixAutoUpdate = false;
  inner.matrix.makeTranslation(-at.x, -at.y, -at.z).multiply(tmp.content.matrixWorld);
  tmp.holder.removeFromParent();
  const meshes = brickMeshes(r, ui.lod, vp);
  meshes.traverse(m => { if (m.isMesh) m.userData.noExport = true; });
  inner.add(meshes);
  const obj = new THREE.Group();
  obj.add(inner);
  return obj;
}
let hiddenAt = null;
function showHidden() { if (hiddenAt && inst[hiddenAt]) inst[hiddenAt].holder.visible = true; hiddenAt = null; }
function startHold(r, auto) {
  const cands = targetsFor(r, auto);
  if (!cands.length) { toast(`Aucun socket « ${SOCKET_TYPES[r.plug.type] || r.plug.type} » pour ${r.name}`); return; }
  vp.gizmo.detach();
  hold.start({
    candidates: cands, auto,
    object: handObject(r, cands[0]),
    socketOf: socketWorld,
    ghostOf: id => {
      showHidden();
      if (inst[id]) { hiddenAt = id; inst[id].holder.visible = false; }
      const g = mountBrick(r, sockets.get(id), state.placed[id]?.brick === r.id ? state.placed[id].xf : defaultXf(), hold.ghostMat);
      return [g.holder];
    },
    onDrop: id => { showHidden(); place(id, r.id); toast(`${r.name} posé sur ${sockets.get(id).label}`); },
    onCancel: () => { showHidden(); attachGizmo(); },
  });
}
function applySquash(id, sq) {
  if (!inst[id]) return;
  if (sq === 1) delete squash[id]; else squash[id] = sq;
  applyXf(inst[id].offset, state.placed[id]?.xf, sq);
}

// ---------- export ----------
const projectName = () => ($('projName').value.trim() || 'plasma_ork').replace(/[^\w\-]+/g, '_');
function orkPrintMeshes() {
  // le high part toujours à l'impression ; la tête d'origine reste si aucune brique ne la remplace
  return model.bakedMeshes(['BODY_high', ...(state.placed.head ? [] : ['HEAD_high', 'TEETH', 'EYES'])]);
}
// Meshes d'une brique seule (sans les briques accrochées dessus), en version high si elle existe.
function brickPrintMeshes(sid) {
  const i = inst[sid];
  const g = brickMeshes(i.rec, 'high', null);
  i.content.add(g);
  g.updateMatrixWorld(true);
  const out = g.children.map(m => { const c = new THREE.Mesh(m.geometry); c.matrixWorld.copy(m.matrixWorld); c.matrixAutoUpdate = false; return c; });
  g.removeFromParent();
  return out;
}
async function doExport(kind) {
  $('exportMenu').hidden = true;
  $('exportMenuBtn').setAttribute('aria-expanded', 'false');
  const sel = ui.sel;
  setSelected(null);
  toast('Export en cours…');
  await new Promise(r => setTimeout(r, 30));
  try {
    if (kind === 'merged') {
      const tris = await exportMerged([...orkPrintMeshes(), ...placedOrder().flatMap(brickPrintMeshes)], mmPerUnit(), projectName());
      toast(`STL exporté : ${Math.round(tris).toLocaleString('fr-FR')} triangles, échelle ${state.scale} mm`);
    } else if (kind === 'parts') {
      const groups = [{ id: 'ork', objects: orkPrintMeshes() }, ...placedOrder().map(sid => ({ id: sid.replace(/>/g, '-') + '_' + rec(state.placed[sid].brick).name.replace(/[^\w-]+/g, '_'), objects: brickPrintMeshes(sid) }))];
      const n = await exportParts(groups, mmPerUnit(), projectName());
      toast(`${n} pièces STL exportées dans un ZIP`);
    } else {
      await exportJSON({ name: $('projName').value, app: 'plasma-ork-builder', version: 2, state, bricks: Object.fromEntries([...new Set(Object.values(state.placed).map(p => p.brick))].map(id => [id, rec(id).name])) }, projectName());
      toast('Projet exporté');
    }
  } catch (err) {
    console.error(err);
    toast('Export impossible ici : ' + (err.message || err.code || 'erreur inconnue'));
  } finally { setSelected(sel); }
}

// ---------- événements ----------
document.addEventListener('click', e => {
  const t = e.target.closest('button');
  if (!t || t.disabled) {
    if (!e.target.closest('.export')) { $('exportMenu').hidden = true; $('exportMenuBtn').setAttribute('aria-expanded', 'false'); }
    return;
  }
  const d = t.dataset;
  if (d.cat) { ui.cat = d.cat; renderRail(); renderLibrary(); }
  else if (d.brick) { if (dragTile.suppress) { dragTile.suppress = false; return; } if (model) startHold(rec(d.brick), true); }
  else if (d.theme) { ui.theme = d.theme; applyTheme(ui.theme); renderAll(); }
  else if (t.id === 'drawerClose') { ui.drawer = false; renderEquip(); }
  else if (t.id === 'drawerOpen') { ui.drawer = true; renderEquip(); }
  else if (d.socket) { if (t.classList.contains('equip-row')) ui.drawer = true; select(d.socket); if (t.classList.contains('pin')) hold.flash(socketWorld(d.socket)); }
  else if (d.clear) removeAt(d.clear);
  else if (d.tool) { ui.tool = d.tool; attachGizmo(); renderSteps(); }
  else if (d.step) setStep(d.step);
  else if (d.tab) { ui.tab = d.tab; renderCustomize(); }
  else if (d.view) { ui.view = +d.view; vp.frameObject(model.root, new THREE.Vector3(...VIEWS[ui.view].dir)); renderViews(); }
  else if (d.pose) { const p = poseList().find(x => x.id === d.pose); if (p) setPose(p); }
  else if (d.lod) { ui.lod = d.lod; rebuild(); save(); renderOrk(); renderPrintInfo(); }
  else if (d.mirror && ui.sel && state.placed[ui.sel]) { const xf = state.placed[ui.sel].xf; const i = 'xyz'.indexOf(d.mirror); xf.m[i] = !xf.m[i]; applyXf(inst[ui.sel].offset, xf); commit(); renderCustomize(); }
  else if (d.shade) { ui.shade = d.shade; applyMaterials(); renderSteps(); }
  else if (d.export) doExport(d.export);
});

$('exportBtn').addEventListener('click', () => doExport('merged'));
$('exportMenuBtn').addEventListener('click', () => { const m = $('exportMenu'); m.hidden = !m.hidden; $('exportMenuBtn').setAttribute('aria-expanded', String(!m.hidden)); });
$('undoBtn').addEventListener('click', () => travel(-1));
$('redoBtn').addEventListener('click', () => travel(1));
$('frameBtn').addEventListener('click', () => model && vp.frameObject(model.root));
$('socketsBtn').addEventListener('click', () => { ui.sockets = !ui.sockets; renderSteps(); renderPins(); });
$('rigBtn').addEventListener('click', () => { ui.rig = !ui.rig; if (skel) skel.visible = ui.rig; renderSteps(); });
$('lightBtn').addEventListener('click', () => vp.rotateLight());
$('texBtn').addEventListener('click', () => { ui.shade = ui.shade === 'paint' ? 'resin' : 'paint'; applyMaterials(); renderSteps(); });
$('fullBtn').addEventListener('click', () => { const el = $('vp'); if (document.fullscreenElement) document.exitFullscreen(); else el.requestFullscreen?.(); });
$('qualitySel').addEventListener('change', e => setQuality(e.target.value));
$('scaleSel').addEventListener('change', e => { state.scale = +e.target.value; vp.unitsPerMeter = eyeUnits() / (state.scale / 1000); vp.configurePT(); commit(); renderPrintInfo(); });
$('renderSet').addEventListener('input', e => {
  const el = e.target, k = el.dataset.r;
  if (!k) return;
  if (k === 'env' && el.value === 'hdri' && !vp.studio.hdri) { $('hdriInput').click(); return; }
  setRender({ [k]: el.type === 'checkbox' ? el.checked : (k === 'env' ? el.value : +el.value) });
});
$('hdriBtn').addEventListener('click', () => $('hdriInput').click());
// Scène de test du rendu : le Grot Bomber texturé à la place de la figurine.
$('sceneSel').addEventListener('change', async e => {
  const test = e.target.value !== 'builder';
  testModel?.root?.removeFromParent();
  ui.test = test;
  $('texBtn').hidden = !test;
  if (test) {
    testModel ||= new TestModel(vp, eyeUnits(), 'grot');
    toast('Chargement du modèle test…');
    try { await testModel.load(DESKTOP); }
    catch (err) { console.error(err); toast('Modèle test introuvable'); e.target.value = 'builder'; ui.test = false; $('texBtn').hidden = true; model.root.visible = true; return; }
    if (!ui.test) return;
    vp.scene.add(testModel.root);
    if (testModel.maps[1001] && ui.shade !== 'paint') { ui.shade = 'paint'; applyMaterials(); renderSteps(); }
    let tris = 0;
    testModel.root.traverse(m => { if (m.isMesh) tris += m.geometry.index.count / 3; });
    ui.info = `Modèle test ${testModel.def.label} · ${Math.round(tris / 1000)}k triangles`;
  } else renderPrintInfo();
  model.root.visible = !test;
  vp.gizmo.detach();
  if (!test) attachGizmo();
  if (skel) skel.visible = !test && ui.rig;
  $('pins').hidden = test || !ui.sockets;
  const target = test ? testModel.root : model.root;
  target.updateMatrixWorld(true);
  vp.frameObject(target, new THREE.Vector3(0.55, 0.2, 1));
  renderInfo();
});
$('texBtn').addEventListener('click', () => $('texInput').click());
$('texInput').addEventListener('change', async e => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f || !testModel) return;
  try {
    const udim = await testModel.loadMapFile(f);
    if (ui.shade !== 'paint') { ui.shade = 'paint'; applyMaterials(); renderSteps(); }
    testModel.refresh();
    toast(`${f.name} appliquée (UDIM ${udim})`);
  } catch (err) { console.error(err); toast(`Impossible de lire ${f.name}`); }
});
$('hdriInput').addEventListener('change', async e => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) { renderRenderPanel(); return; }
  toast(`Chargement de ${f.name}…`);
  try { await vp.setHDRI(f); setRender({}); toast(`HDRI ${f.name} chargé${vp.studio.hdri.sun ? ' (soleil extrait en lumière ombrée)' : ''}`); }
  catch (err) { console.error(err); toast(`Impossible de lire ${f.name}`); renderRenderPanel(); }
});
$('gridChk').addEventListener('change', e => { vp.grid.visible = e.target.checked; });
$('baseChk').addEventListener('change', e => { if (inst.base) inst.base.holder.visible = e.target.checked; renderPrintInfo(); });
$('searchInput').addEventListener('input', e => { ui.search = e.target.value; renderLibrary(); });
$('projName').addEventListener('change', save);
$('resetXf').addEventListener('click', () => { const p = state.placed[ui.sel]; if (!p) return; p.xf = defaultXf(); applyXf(inst[ui.sel].offset, p.xf); commit(); renderCustomize(); });
$('removeBtn').addEventListener('click', () => { if (ui.sel && state.placed[ui.sel]) removeAt(ui.sel); });
for (const side of ['L', 'R']) {
  $('curl' + side).addEventListener('input', e => { state.curl[side] = +e.target.value; model?.curl(side, state.curl[side]); renderOrk(); });
  $('curl' + side).addEventListener('change', () => { commit(); scheduleFigureThumbs(); });
}

// Curseurs de transformation : aperçu en direct, historique au relâchement.
function onXfInput(e, final) {
  const el = e.target, p = state.placed[ui.sel];
  if (!el.dataset.k || !p) return;
  const i = +el.dataset.i, v = +el.value;
  if (Number.isNaN(v)) return;
  if (i < 0) p.xf[el.dataset.k] = v; else p.xf[el.dataset.k][i] = v;
  applyXf(inst[ui.sel].offset, p.xf);
  const twin = el.type === 'range' ? el.nextElementSibling : el.previousElementSibling;
  if (twin) twin.value = el.type === 'range' ? +v.toFixed(3) : v;
  if (final) { commit(); renderPrintInfo(); scheduleFigureThumbs(); }
}
$('xf').addEventListener('input', e => onXfInput(e, false));
$('xf').addEventListener('change', e => onXfInput(e, true));

// Gizmo : le réglage manuel de la brique (relatif à son socket).
vp.gizmo.addEventListener('objectChange', () => {
  const o = vp.gizmo.object, p = state.placed[ui.sel];
  if (!o || !p) return;
  p.xf.p = o.position.toArray().map(v => +v.toFixed(3));
  p.xf.r = [o.rotation.x, o.rotation.y, o.rotation.z].map(v => +(v / D2R).toFixed(1));
  p.xf.s = +Math.abs(o.scale.y).toFixed(3);
  applyXf(o, p.xf);
});
vp.gizmo.addEventListener('mouseUp', () => { commit(); renderCustomize(); scheduleFigureThumbs(); });

// Sélection au clic dans le viewport : une brique posée, ou rien.
const ray = new THREE.Raycaster();
let downAt = null;
$('vpCanvas').addEventListener('pointerdown', e => { downAt = e.button === 0 ? [e.clientX, e.clientY] : null; });
$('vpCanvas').addEventListener('pointerup', e => {
  if (!model || ui.test || !downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 4 || vp.gizmo.dragging) return;
  const r = e.target.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), vp.camera);
  const meshes = [];
  for (const i of Object.values(inst)) i.meshes.traverseVisible(m => { if (m.isMesh) meshes.push(m); });
  const hit = ray.intersectObjects(meshes, false)[0];
  select(hit ? hit.object.userData.socket : null);
});

// Glisser une vignette vers le viewport : brique en main, posée au lâcher sur le socket visé.
const dragTile = { el: null, x: 0, y: 0, on: false, suppress: false };
$('libGrid').addEventListener('pointerdown', e => {
  const t = e.target.closest('[data-brick]');
  if (!t || e.button !== 0) return;
  Object.assign(dragTile, { el: t, x: e.clientX, y: e.clientY, on: false });
});
document.addEventListener('pointermove', e => {
  hold.pointer.x = e.clientX; hold.pointer.y = e.clientY;
  const r = $('vp').getBoundingClientRect();
  hold.pointer.inside = e.clientX >= r.left && e.clientX <= r.right && e.clientY >= r.top && e.clientY <= r.bottom;
  if (!model || !dragTile.el || dragTile.on || Math.hypot(e.clientX - dragTile.x, e.clientY - dragTile.y) < 8) return;
  dragTile.on = true;
  dragTile.el.classList.add('dragging');
  document.body.style.cursor = 'grabbing';
  startHold(rec(dragTile.el.dataset.brick), false);
});
document.addEventListener('pointerup', () => {
  if (!dragTile.el) return;
  const was = dragTile.on;
  dragTile.el.classList.remove('dragging');
  document.body.style.cursor = '';
  dragTile.el = null;
  dragTile.on = false;
  if (!was) return;
  dragTile.suppress = true;
  setTimeout(() => { dragTile.suppress = false; }, 0);
  if (hold.active && hold.pointer.inside) hold.drop(); else if (hold.active) hold.cancel();
});

document.addEventListener('keydown', e => {
  if (e.target.closest('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if (k === 'escape' && hold.active) { hold.cancel(); return; }
  if ((e.ctrlKey || e.metaKey) && k === 'z') { e.preventDefault(); travel(e.shiftKey ? 1 : -1); return; }
  if ((e.ctrlKey || e.metaKey) && k === 'y') { e.preventDefault(); travel(1); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  // comme Maya et le Rig Lab : W déplacer, E / R tourner, Q sélection, S échelle
  const tool = { q: 'select', w: 'translate', g: 'translate', e: 'rotate', r: 'rotate', s: 'scale' }[k];
  if (tool) { ui.tool = tool; attachGizmo(); renderSteps(); }
  else if (k === 'f' && model) vp.frameObject(ui.sel && inst[ui.sel] ? inst[ui.sel].meshes : model.root);   // la brique sélectionnée, sinon l'ork
  // affichage comme Maya : 4 filaire, 5 résine, 6 texture
  else if (k === '4' || k === '5' || k === '6') { ui.shade = { 4: 'wire', 5: 'resin', 6: 'paint' }[k]; applyMaterials(); renderSteps(); }
  else if (k === 'delete' || k === 'backspace') $('removeBtn').click();
});

// Boucle : position des pastilles de sockets à l'écran.
const tmp = new THREE.Vector3();
let infoT = 0;
vp.onFrame = dt => {
  hold.update(dt, applySquash);
  if ((ui.quality === 'pt' || ui.quality === 'bhq') && (infoT += dt) > 0.25) { infoT = 0; renderInfo(); renderGIInfo(); }
  if (!model || !ui.sockets) return;
  const el = $('vp'), w = el.clientWidth, h = el.clientHeight;
  for (const b of $('pins').children) {
    if (!sockets.has(b.dataset.socket)) continue;
    tmp.copy(socketWorld(b.dataset.socket)).project(vp.camera);
    const vis = tmp.z < 1 && Math.abs(tmp.x) < 1.05 && Math.abs(tmp.y) < 1.05;
    b.style.display = vis ? '' : 'none';
    b.style.left = ((tmp.x + 1) / 2 * w) + 'px';
    b.style.top = ((1 - tmp.y) / 2 * h) + 'px';
  }
};

// Clic droit sur une vignette : renommer, supprimer (retirée aussi de la figurine).
brickMenu($('libGrid'), {
  get: id => recs.get(id),
  toast,
  changed: async kind => {
    const n = Object.keys(state.placed).length;
    await loadLibrary();
    rebuild();
    if (kind === 'delete' && Object.keys(state.placed).length !== n) commit();
    renderAll();
  },
});

// La bibliothèque peut changer dans l'importeur (autre onglet) : on la relit au retour sur la page.
async function loadLibrary() {
  let list = [];
  try { list = await Store.all(); } catch (err) { console.error(err); toast('Bibliothèque indisponible (stockage du navigateur bloqué)'); }
  recs.clear();
  for (const r of list) recs.set(r.id, r);
}
document.addEventListener('visibilitychange', async () => {
  if (document.hidden || !model) return;
  const before = [...recs.values()].map(r => r.id + (r.updated || '')).join();
  await loadLibrary();
  if ([...recs.values()].map(r => r.id + (r.updated || '')).join() === before) return;
  rebuild(); renderAll();
});

// ---------- démarrage ----------
applyTheme(ui.theme);
renderSteps(); renderViews(); renderOrk(); renderPoses(); renderRenderPanel(); syncUndo();
try {
  const [m] = await Promise.all([
    OrkModel.load('assets/ork_boy.glb', e => { if (e.total) $('loading').style.setProperty('--p', (e.loaded / e.total * 100).toFixed(0) + '%'); }),
    loadLibrary(),
  ]);
  model = m;
  model.fit();
  vp.scene.add(model.root);
  vp.unitsPerMeter = eyeUnits() / (state.scale / 1000);
  orkSocketDefs = orkSockets(model);   // au repos, avant la pose
  skel = new THREE.SkeletonHelper(model.root);
  skel.material.color.set(0xf0435a);
  skel.material.depthTest = false;
  skel.material.transparent = true;
  skel.renderOrder = 10;
  skel.visible = false;
  skel.userData.helper = true;
  vp.overlay.add(skel);
  applyPose();
  rebuild();
  applyMaterials();
  commit();
  renderAll();
  setStep('asm');
  vp.frameObject(model.root, new THREE.Vector3(0.55, 0.28, 1), true);
  $('loading').hidden = true;
  window.builder = { model, vp, get state() { return state; }, ui, sockets: () => sockets, inst, recs, place, select, startHold, hold };
} catch (err) {
  console.error(err);
  const l = $('loading');
  l.classList.add('err');
  l.textContent = 'Impossible de charger assets/ork_boy.glb. Sers le dossier web/plasma-builder avec un petit serveur (python -m http.server) et vérifie que le fichier est présent.';
}
