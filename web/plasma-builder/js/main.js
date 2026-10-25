// PlasmaOrk Builder : assemblage d'un Ork à partir de pièces STL, rig, poses, rendu et export.
import * as THREE from 'three';
import { Viewport, Thumbs } from './viewport.js';
import { Figure, defaultXf } from './figure.js';
import { POSES, POSABLE } from './rig.js';
import { SLOTS, CATEGORIES, PRESETS, KITS, OPTIONS, IMPORT_SIZE, buildVariant, buildKit } from './parts.js';
import { readSTL, exportMerged, exportParts, exportJSON } from './io.js';

const $ = id => document.getElementById(id);
const slotById = id => SLOTS.find(s => s.id === id);
const ICON = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const EYE_HEIGHT = 1.95; // hauteur des yeux dans le rig : c'est elle que l'échelle (32/54/75 mm) fixe

// ---------- état ----------
function freshState() {
  const st = { sel: {}, xf: {}, pose: 'garde', manual: {}, kits: [], opts: { plates: false, pouches: true, ornament: false }, wear: 0, scale: 32 };
  Object.assign(st.sel, PRESETS[2].sel);
  st.kits = [...PRESETS[2].kits];
  st.opts = { plates: true, pouches: false, ornament: true };
  for (const s of SLOTS) st.xf[s.id] = defaultXf();
  return st;
}
let state = freshState();
const ui = { cat: 'heads', slot: 'head', chip: 'Tous', tool: 'select', step: 'asm', tab: 'T', rig: false, sockets: true, view: 1, shade: 'resin', quality: 'rt', search: '', showBase: true };

try {
  const saved = JSON.parse(localStorage.getItem('plasmaork.project') || 'null');
  if (saved && saved.sel) { state = Object.assign(freshState(), saved); for (const s of SLOTS) state.xf[s.id] ||= defaultXf(); }
  const n = localStorage.getItem('plasmaork.name'); if (n) $('projName').value = n;
} catch (e) { /* stockage indisponible : on part de l'état par défaut */ }

// ---------- 3D ----------
const vp = new Viewport($('vpCanvas'));
const thumbs = new Thumbs(160, 200);
const figure = new Figure((tag, sel) => vp.material(tag, sel));
vp.scene.add(figure.root);
figure.apply(state, true);
figure.setSelected(ui.slot);
requestAnimationFrame(() => { vp.resize(); vp.frameObject(figure.root, new THREE.Vector3(0.55, 0.28, 1), true); });

const skel = new THREE.SkeletonHelper(figure.root);
skel.material.color.set(0xf0435a);
skel.material.depthTest = false;
skel.material.transparent = true;
skel.renderOrder = 10;
skel.visible = false;
vp.scene.add(skel);

// ---------- historique ----------
const history = [JSON.stringify(state)];
let hIndex = 0;
function save() {
  try { localStorage.setItem('plasmaork.project', JSON.stringify(state)); localStorage.setItem('plasmaork.name', $('projName').value); } catch (e) { /* ignoré */ }
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
  refresh3D();
  renderAll();
  syncUndo();
}
function syncUndo() { $('undoBtn').disabled = hIndex <= 0; $('redoBtn').disabled = hIndex >= history.length - 1; }

// ---------- vignettes (rendues à la demande, mises en cache) ----------
const thumbCache = new Map();
function piecesObject(pieces, rig) {
  const g = new THREE.Group();
  for (const pc of pieces) for (const tag in pc.geos) {
    const m = new THREE.Mesh(pc.geos[tag], thumbs.mat);
    m.position.copy(rig.restWorld(pc.bone));
    g.add(m);
  }
  return g;
}
function variantThumb(slot, v) {
  const key = slot.id + ':' + v.id + ':' + state.wear;
  if (thumbCache.has(key)) return thumbCache.get(key);
  if (v.none) return null;
  thumbs.setSize(160, 200);
  const dir = slot.id === 'back' ? new THREE.Vector3(0.6, 0.3, -1) : slot.id === 'base' ? new THREE.Vector3(0.3, 0.8, 1) : new THREE.Vector3(slot.id === 'armR' ? -0.5 : 0.45, 0.2, 1);
  const url = thumbs.render(piecesObject(buildVariant(slot, v, state.wear), figure.rig), dir);
  thumbCache.set(key, url);
  return url;
}
function kitThumb(kit) {
  const key = 'kit:' + kit.id;
  if (!thumbCache.has(key)) { thumbs.setSize(160, 200); thumbCache.set(key, thumbs.render(piecesObject(buildKit(kit, 0), figure.rig), new THREE.Vector3(0.6, 0.2, 1))); }
  return thumbCache.get(key);
}
function presetThumb(p) {
  const key = 'preset:' + p.id;
  if (!thumbCache.has(key)) {
    const f = new Figure(() => thumbs.mat);
    const st = freshState();
    Object.assign(st.sel, p.sel); st.kits = p.kits; st.opts = { plates: false, pouches: false, ornament: false, ...p.opts };
    f.apply(st, true);
    thumbs.setSize(160, 200);
    thumbCache.set(key, thumbs.render(f.root, new THREE.Vector3(0.5, 0.2, 1), 1.05));
  }
  return thumbCache.get(key);
}
// Rendus de la figurine en cours : vues caméra et poses.
const VIEWS = [
  { name: 'Face', dir: [0, 0.12, 1] }, { name: 'Trois-quarts', dir: [0.7, 0.25, 1] },
  { name: 'Dos', dir: [-0.3, 0.2, -1] }, { name: 'Profil', dir: [1, 0.12, 0] },
];
let figureThumbTimer = 0;
function scheduleFigureThumbs() {
  clearTimeout(figureThumbTimer);
  figureThumbTimer = setTimeout(renderFigureThumbs, 450);
}
function renderFigureThumbs() {
  const snap = figure.rig.snapshot();
  figure.setSelected(null);
  thumbs.setSize(160, 160);
  figure.rig.applyInstant(state.pose);
  const views = VIEWS.map(v => thumbs.render(figure.root, new THREE.Vector3(...v.dir), 1.0));
  thumbs.setSize(120, 170);
  const poses = POSES.map(p => { figure.rig.applyInstant(p.id); return thumbs.render(figure.root, new THREE.Vector3(0.45, 0.15, 1), 1.0); });
  figure.rig.restore(snap);
  figure.setSelected(ui.slot);
  vp.scene.add(figure.root);
  $('views').querySelectorAll('img').forEach((img, i) => { img.src = views[i]; });
  $('poseRow').querySelectorAll('img').forEach((img, i) => { img.src = poses[i]; });
}

// ---------- rendu de l'interface ----------
function renderRail() {
  $('rail').innerHTML = CATEGORIES.map(c => `<button type="button" data-cat="${c.id}" ${c.soon ? 'disabled title="Bientôt"' : ''} aria-current="${ui.cat === c.id}">${ICON(c.icon)}${c.label}</button>`).join('');
}

function libSlot() {
  if (ui.cat === 'arms') return slotById(ui.slot === 'armR' ? 'armR' : 'armL');
  return SLOTS.find(s => s.cat === ui.cat);
}

function renderLibrary() {
  const cat = CATEGORIES.find(c => c.id === ui.cat);
  $('libTitle').textContent = cat.label;
  $('sidePick').hidden = ui.cat !== 'arms';
  $('sidePick').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(libSlot()?.id === b.dataset.side)));
  const q = ui.search.trim().toLowerCase();
  if (ui.cat === 'presets') {
    $('chips').innerHTML = '';
    const list = PRESETS.filter(p => !q || p.name.toLowerCase().includes(q));
    $('libCount').textContent = list.length;
    $('libGrid').innerHTML = list.map(p => `<button type="button" class="tile" data-preset="${p.id}" aria-pressed="false"><img alt="" src="${presetThumb(p)}"><span>${esc(p.name)}</span></button>`).join('');
    return;
  }
  const slot = libSlot();
  const tags = ['Tous', ...new Set(slot.variants.flatMap(v => v.tags || []))];
  if (!tags.includes(ui.chip)) ui.chip = 'Tous';
  $('chips').innerHTML = tags.map(t => `<button type="button" data-chip="${esc(t)}" aria-pressed="${ui.chip === t}">${esc(t)}</button>`).join('');
  const list = slot.variants.filter(v => (ui.chip === 'Tous' || (v.tags || []).includes(ui.chip)) && (!q || v.name.toLowerCase().includes(q)));
  $('libCount').textContent = slot.variants.filter(v => !v.none).length;
  const current = figure.variantOf(slot, state.sel[slot.id]).id;
  $('libGrid').innerHTML = list.map(v => {
    const img = v.none ? `<span class="ph">Aucun</span>` : `<img alt="" src="${variantThumb(slot, v)}">`;
    const badge = v.imported ? '<em class="badge">STL</em>' : '';
    return `<button type="button" class="tile${v.none ? ' none' : ''}" data-variant="${v.id}" aria-pressed="${current === v.id}">${badge}${img}<span>${esc(v.name)}</span></button>`;
  }).join('');
}

function renderSteps() {
  $('steps').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.step === ui.step)));
  $('tools').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === ui.tool)));
  $('socketsBtn').setAttribute('aria-pressed', String(ui.sockets));
  $('rigBtn').setAttribute('aria-pressed', String(ui.rig));
  $('shading').querySelectorAll('[data-shade]').forEach(b => b.classList.toggle('on', b.dataset.shade === ui.shade));
  $('qualitySel').value = ui.quality;
  $('scaleSel').value = String(state.scale);
}

function renderViews() {
  if (!$('views').children.length) $('views').innerHTML = VIEWS.map((v, i) => `<button type="button" data-view="${i}" aria-label="Vue ${v.name}" title="${v.name}"><img alt=""></button>`).join('');
  $('views').querySelectorAll('button').forEach((b, i) => b.setAttribute('aria-pressed', String(i === ui.view)));
}

function renderPins() {
  $('pins').innerHTML = SLOTS.map(s => `<button type="button" class="pin" data-slot="${s.id}" aria-pressed="${ui.slot === s.id}"><i></i>${s.label}</button>`).join('');
  $('pins').hidden = !ui.sockets;
}

const XF_ROWS = [
  ['Position', 'X', 'p', 0, -0.3, 0.3, 0.005], ['', 'Y', 'p', 1, -0.3, 0.3, 0.005], ['', 'Z', 'p', 2, -0.3, 0.3, 0.005],
  ['Rotation', 'X', 'r', 0, -180, 180, 1], ['', 'Y', 'r', 1, -180, 180, 1], ['', 'Z', 'r', 2, -180, 180, 1],
  ['Échelle', '', 's', -1, 0.5, 2, 0.01],
];
function renderCustomize() {
  const slot = slotById(ui.slot);
  const v = figure.variantOf(slot, state.sel[slot.id]);
  $('curSlot').textContent = slot.label;
  $('curName').textContent = '· ' + v.name;
  $('curSocket').textContent = 'socket_' + slot.id;
  $('removeBtn').disabled = !slot.variants.some(x => x.none);
  document.querySelectorAll('.tabs button').forEach(b => b.setAttribute('aria-selected', String(b.dataset.tab === ui.tab)));
  for (const t of ['T', 'R', 'P']) $('pane-' + t).hidden = ui.tab !== t;
  const xf = state.xf[slot.id];
  $('xf').innerHTML = XF_ROWS.map(([lab, ax, k, i, min, max, step]) => {
    const val = i < 0 ? xf[k] : xf[k][i];
    const id = `xf-${k}${i}`;
    return `<span>${lab}</span><span class="ax">${ax}</span><input type="range" id="${id}" data-k="${k}" data-i="${i}" min="${min}" max="${max}" step="${step}" value="${val}" aria-label="${lab || XF_ROWS.find(r => r[2] === k)[0]} ${ax}"><input type="number" data-k="${k}" data-i="${i}" min="${min}" max="${max}" step="${step}" value="${+(+val).toFixed(3)}" aria-label="Valeur ${ax || lab}">`;
  }).join('');
  document.querySelectorAll('[data-mirror]').forEach((b, i) => b.setAttribute('aria-pressed', String(xf.m[i])));
  // Rig
  $('rigBone').textContent = slot.bone;
  $('rigSockets').textContent = [...new Set(buildVariant(slot, v, state.wear).map(p => p.bone))].join(', ') || '—';
  const bones = POSABLE[slot.id] || [];
  $('boneSliders').innerHTML = bones.map(bn => {
    const m = state.manual[bn] || [0, 0, 0];
    return ['X', 'Y', 'Z'].map((ax, i) => `<span>${i === 0 ? bn : ''}</span><span class="ax">${ax}</span><input type="range" data-bone="${bn}" data-i="${i}" min="-90" max="90" step="1" value="${Math.round(m[i] * 180 / Math.PI)}" aria-label="${bn} ${ax}"><input type="number" data-bone="${bn}" data-i="${i}" min="-90" max="90" value="${Math.round(m[i] * 180 / Math.PI)}" aria-label="Valeur ${bn} ${ax}">`).join('');
  }).join('');
  $('resetPose').hidden = !bones.length;
  renderPrintInfo();
}

function renderPrintInfo() {
  const box = new THREE.Box3().setFromObject(figure.root);
  const h = box.isEmpty() ? 0 : (box.max.y - Math.max(box.min.y, -0.1)) * state.scale / EYE_HEIGHT;
  $('printHeight').textContent = `${h.toFixed(1)} mm (échelle ${state.scale} mm)`;
  const t = figure.tris();
  $('printTris').textContent = Math.round(t).toLocaleString('fr-FR');
  $('printParts').textContent = SLOTS.filter(s => !figure.variantOf(s, state.sel[s.id]).none).length + ' emplacements';
  const q = ui.quality === 'hq' ? 'Haute qualité' : 'Temps réel';
  $('vpInfo').textContent = `${q} · AgX\n${Math.round(t / 1000)}k triangles · ${h.toFixed(0)} mm`;
  $('asmInfo').textContent = `${Math.round(t / 1000)}k triangles`;
}

function renderKits() {
  $('kitGrid').innerHTML = KITS.map(k => `<button type="button" data-kit="${k.id}" aria-pressed="${state.kits.includes(k.id)}" aria-label="${esc(k.name)}" title="${esc(k.name)}"><img alt="" src="${kitThumb(k)}"></button>`).join('');
}

const OPT_ICONS = {
  plates: 'M12 3l8 3v6c0 5-4 8-8 9-4-1-8-4-8-9V6z',
  pouches: 'M6 8h12v12H6zM9 8V5h6v3M6 12h12',
  ornament: 'M12 3a6 6 0 0 0-6 6v3l2 2v3h8v-3l2-2V9a6 6 0 0 0-6-6zM10 10h.01M14 10h.01',
  wear: 'M12 2l3 7h7l-6 4 2 8-6-5-6 5 2-8-6-4h7z',
};
const openOpts = new Set();
function renderOptions() {
  const rows = Object.entries(OPTIONS).map(([id, o]) => ({ id, name: o.name, body: `<label class="check"><input type="checkbox" data-opt="${id}" ${state.opts[id] ? 'checked' : ''}>Ajouter à la figurine</label>` }));
  rows.push({ id: 'wear', name: 'Dégâts / usure', body: `<label>Intensité <input type="range" id="wearRange" min="0" max="1" step="0.25" value="${state.wear}" aria-label="Dégâts et usure"></label>` });
  $('options').innerHTML = rows.map(r => `<div class="opt"><button type="button" data-acc="${r.id}" aria-expanded="${openOpts.has(r.id)}">${ICON(OPT_ICONS[r.id])}<span class="grow">${esc(r.name)}</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 6l6 6-6 6"/></svg></button><div class="body2" ${openOpts.has(r.id) ? '' : 'hidden'}>${r.body}</div></div>`).join('');
}

function renderAssembly() {
  $('asmRow').innerHTML = SLOTS.map(s => {
    const v = figure.variantOf(s, state.sel[s.id]);
    const img = v.none ? '<span class="ph" style="display:flex;aspect-ratio:4/5;align-items:center;justify-content:center;color:#6b6d74;font-size:11px">Aucun</span>' : `<img alt="" src="${variantThumb(s, v)}">`;
    const canClear = s.variants.some(x => x.none) && !v.none;
    return `<div class="card"><button type="button" class="thumb" data-slot="${s.id}" aria-pressed="${ui.slot === s.id}" aria-label="${s.label} : ${esc(v.name)}">${img}</button><span class="lbl">${s.label}</span>${canClear ? `<button type="button" class="x" data-clear="${s.id}" aria-label="Retirer ${s.label}">${ICON('M6 6l12 12M18 6L6 18')}</button>` : ''}</div>`;
  }).join('');
}

function renderPoses() {
  if (!$('poseRow').children.length) $('poseRow').innerHTML = POSES.map(p => `<div class="card"><button type="button" class="thumb" data-pose="${p.id}" aria-label="Pose ${p.name}"><img alt=""></button><span class="lbl">${p.name}</span></div>`).join('');
  $('poseRow').querySelectorAll('[data-pose]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.pose === state.pose)));
  $('poseName').textContent = POSES.find(p => p.id === state.pose)?.name || '';
}

function renderAll() {
  renderRail(); renderLibrary(); renderSteps(); renderViews(); renderPins(); renderCustomize(); renderKits(); renderOptions(); renderAssembly(); renderPoses();
  scheduleFigureThumbs();
}

function refresh3D(instant = false) {
  figure.apply(state, instant);
  figure.setVisible('base', ui.showBase);
  figure.setSelected(ui.slot);
  attachGizmo();
}

// ---------- actions ----------
function selectSlot(id, syncCat = true) {
  ui.slot = id;
  if (syncCat) ui.cat = slotById(id).cat;
  figure.setSelected(id);
  attachGizmo();
  renderRail(); renderLibrary(); renderPins(); renderCustomize(); renderAssembly();
}
function setVariant(slotId, variantId) {
  state.sel[slotId] = variantId;
  ui.slot = slotId;
  refresh3D();
  commit();
  renderLibrary(); renderCustomize(); renderAssembly(); renderPins();
  scheduleFigureThumbs();
}
function attachGizmo() {
  const w = figure.slots[ui.slot]?.wrappers[0];
  if (ui.tool === 'select' || !w) { vp.gizmo.detach(); return; }
  vp.gizmo.setMode(ui.tool);
  vp.gizmo.attach(w);
}
function setStep(step) {
  ui.step = step;
  if (step === 'lib') { ui.sockets = true; ui.rig = false; $('searchInput').focus(); }
  if (step === 'asm') { ui.sockets = true; ui.rig = false; ui.tab = 'T'; }
  if (step === 'rig') { ui.sockets = true; ui.rig = true; ui.tab = 'R'; }
  if (step === 'pose') { ui.sockets = false; ui.rig = true; ui.tab = 'R'; }
  if (step === 'render') { ui.sockets = false; ui.rig = false; ui.tab = 'P'; setQuality('hq'); }
  skel.visible = ui.rig;
  renderSteps(); renderPins(); renderCustomize();
}
function setQuality(q) { ui.quality = q; vp.setQuality(q); renderSteps(); renderPrintInfo(); }
function toast(msg) {
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast.timer); toast.timer = setTimeout(() => t.classList.remove('show'), 2600);
}
function applyPreset(p) {
  Object.assign(state.sel, p.sel);
  state.kits = [...p.kits];
  state.opts = { plates: false, pouches: false, ornament: false, ...p.opts };
  for (const s of SLOTS) state.xf[s.id] = defaultXf();
  refresh3D();
  commit();
  renderAll();
  toast(`Preset « ${p.name} » appliqué`);
}
const projectName = () => ($('projName').value.trim() || 'plasma_ork').replace(/[^\w\-]+/g, '_');

async function doExport(kind) {
  $('exportMenu').hidden = true;
  $('exportMenuBtn').setAttribute('aria-expanded', 'false');
  const mm = state.scale / EYE_HEIGHT;
  const wasSel = ui.slot;
  figure.setSelected(null);
  // On exporte la pose visée, pas une étape de la transition.
  figure.rig.applyInstant(state.pose);
  try {
    if (kind === 'merged') {
      const tris = await exportMerged([figure.root], mm, projectName());
      toast(`STL exporté : ${Math.round(tris).toLocaleString('fr-FR')} triangles, échelle ${state.scale} mm`);
    } else if (kind === 'parts') {
      const n = await exportParts(figure.exportGroups(), mm, projectName(), $('keysChk').checked);
      toast(`${n} pièces STL exportées dans un ZIP`);
    } else {
      await exportJSON({ name: $('projName').value, app: 'plasma-ork-builder', version: 1, state }, projectName());
      toast('Projet exporté');
    }
  } catch (err) {
    console.error(err);
    toast('Export impossible ici : ' + (err.message || err.code || 'erreur inconnue'));
  } finally { figure.setSelected(wasSel); }
}

async function importFiles(files) {
  const slot = ui.cat === 'presets' ? slotById(ui.slot) : libSlot() || slotById(ui.slot);
  let added = 0;
  for (const f of files) {
    if (!/\.stl$/i.test(f.name)) continue;
    try {
      const { geometry, tris } = await readSTL(f, IMPORT_SIZE[slot.cat] || 0.5, slot.id === 'base');
      const id = 'imp_' + Date.now().toString(36) + added;
      const v = { id, name: f.name.replace(/\.stl$/i, ''), tags: ['Importé'], imported: true, build: (w, s) => [{ bone: s.bone, geos: { imported: geometry } }] };
      slot.variants.push(v);
      setVariant(slot.id, id);
      added++;
      toast(`${f.name} importé (${Math.round(tris).toLocaleString('fr-FR')} triangles) dans ${CATEGORIES.find(c => c.id === slot.cat).label}`);
    } catch (e) {
      console.error(e);
      toast(`Impossible de lire ${f.name}`);
    }
  }
  if (added) { ui.cat = slot.cat; renderRail(); renderLibrary(); }
}

// ---------- événements ----------
document.addEventListener('click', e => {
  const t = e.target.closest('button');
  if (!t || t.disabled) {
    if (!e.target.closest('.export')) { $('exportMenu').hidden = true; $('exportMenuBtn').setAttribute('aria-expanded', 'false'); }
    return;
  }
  const d = t.dataset;
  if (d.cat) { ui.cat = d.cat; const s = d.cat === 'arms' ? (ui.slot === 'armR' ? 'armR' : 'armL') : SLOTS.find(x => x.cat === d.cat)?.id; if (s) selectSlot(s, false); else { renderRail(); renderLibrary(); } }
  else if (d.chip) { ui.chip = d.chip; renderLibrary(); }
  else if (d.side) selectSlot(d.side, false);
  else if (d.variant) setVariant(libSlot().id, d.variant);
  else if (d.preset) applyPreset(PRESETS.find(p => p.id === d.preset));
  else if (d.slot) selectSlot(d.slot);
  else if (d.clear) { const s = slotById(d.clear); setVariant(s.id, s.variants.find(v => v.none).id); }
  else if (d.tool) { ui.tool = d.tool; attachGizmo(); renderSteps(); }
  else if (d.step) setStep(d.step);
  else if (d.tab) { ui.tab = d.tab; renderCustomize(); }
  else if (d.view) { ui.view = +d.view; vp.frameObject(figure.root, new THREE.Vector3(...VIEWS[ui.view].dir)); renderViews(); }
  else if (d.pose) { state.pose = d.pose; figure.rig.setPose(state.pose); commit(); renderPoses(); renderPrintInfo(); if (!ui.rig && ui.step !== 'render') { ui.rig = true; skel.visible = true; renderSteps(); } }
  else if (d.kit) { const i = state.kits.indexOf(d.kit); if (i < 0) state.kits.push(d.kit); else state.kits.splice(i, 1); refresh3D(); commit(); renderKits(); renderPrintInfo(); scheduleFigureThumbs(); }
  else if (d.acc) { if (openOpts.has(d.acc)) openOpts.delete(d.acc); else openOpts.add(d.acc); renderOptions(); }
  else if (d.mirror) { const xf = state.xf[ui.slot]; const i = 'xyz'.indexOf(d.mirror); xf.m[i] = !xf.m[i]; figure.applyXf(ui.slot, xf); commit(); renderCustomize(); }
  else if (d.shade) { ui.shade = d.shade; vp.setMode(d.shade); figure.refreshMaterials(); renderSteps(); }
  else if (d.export) doExport(d.export);
});

$('exportBtn').addEventListener('click', () => doExport('merged'));
$('exportMenuBtn').addEventListener('click', () => { const m = $('exportMenu'); m.hidden = !m.hidden; $('exportMenuBtn').setAttribute('aria-expanded', String(!m.hidden)); });
$('undoBtn').addEventListener('click', () => travel(-1));
$('redoBtn').addEventListener('click', () => travel(1));
$('frameBtn').addEventListener('click', () => vp.frameObject(figure.root));
$('socketsBtn').addEventListener('click', () => { ui.sockets = !ui.sockets; renderSteps(); renderPins(); });
$('rigBtn').addEventListener('click', () => { ui.rig = !ui.rig; skel.visible = ui.rig; renderSteps(); });
$('lightBtn').addEventListener('click', () => vp.rotateLight());
$('fullBtn').addEventListener('click', () => { const el = $('vp'); if (document.fullscreenElement) document.exitFullscreen(); else el.requestFullscreen?.(); });
$('qualitySel').addEventListener('change', e => setQuality(e.target.value));
$('scaleSel').addEventListener('change', e => { state.scale = +e.target.value; commit(); renderPrintInfo(); });
$('gridChk').addEventListener('change', e => { vp.grid.visible = e.target.checked; });
$('baseChk').addEventListener('change', e => { ui.showBase = e.target.checked; figure.setVisible('base', ui.showBase); renderPrintInfo(); });
$('searchInput').addEventListener('input', e => { ui.search = e.target.value; renderLibrary(); });
$('projName').addEventListener('change', save);
$('importBtn').addEventListener('click', () => $('importInput').click());
$('importInput').addEventListener('change', e => { importFiles([...e.target.files]); e.target.value = ''; });
$('resetXf').addEventListener('click', () => { state.xf[ui.slot] = defaultXf(); figure.applyXf(ui.slot, state.xf[ui.slot]); commit(); renderCustomize(); });
$('removeBtn').addEventListener('click', () => { const s = slotById(ui.slot); const none = s.variants.find(v => v.none); if (none) setVariant(s.id, none.id); });
$('resetPose').addEventListener('click', () => { for (const bn of POSABLE[ui.slot] || []) delete state.manual[bn]; figure.apply(state); commit(); renderCustomize(); scheduleFigureThumbs(); });

// Curseurs de transformation et d'os : aperçu en direct, historique au relâchement.
function onXfInput(e, final) {
  const el = e.target;
  if (el.dataset.k) {
    const xf = state.xf[ui.slot], i = +el.dataset.i, v = +el.value;
    if (Number.isNaN(v)) return;
    if (i < 0) xf[el.dataset.k] = v; else xf[el.dataset.k][i] = v;
    figure.applyXf(ui.slot, xf);
    const twin = el.type === 'range' ? el.nextElementSibling : el.previousElementSibling;
    if (twin) twin.value = el.type === 'range' ? +v.toFixed(3) : v;
  } else if (el.dataset.bone) {
    const bn = el.dataset.bone, i = +el.dataset.i, v = +el.value;
    if (Number.isNaN(v)) return;
    state.manual[bn] ||= [0, 0, 0];
    state.manual[bn][i] = v * Math.PI / 180;
    figure.rig.manual = JSON.parse(JSON.stringify(state.manual));
    figure.rig.setPose(state.pose, true);
    const twin = el.type === 'range' ? el.nextElementSibling : el.previousElementSibling;
    if (twin) twin.value = v;
  } else return;
  if (final) { commit(); renderPrintInfo(); scheduleFigureThumbs(); }
}
for (const id of ['xf', 'boneSliders']) {
  $(id).addEventListener('input', e => onXfInput(e, false));
  $(id).addEventListener('change', e => onXfInput(e, true));
}
$('options').addEventListener('change', e => {
  if (e.target.dataset.opt) { state.opts[e.target.dataset.opt] = e.target.checked; refresh3D(); commit(); renderPrintInfo(); scheduleFigureThumbs(); }
  if (e.target.id === 'wearRange') { state.wear = +e.target.value; thumbCache.clear(); refresh3D(); commit(); renderAll(); }
});

// Gizmo : la pièce déplacée met à jour son emplacement (et son jumeau en miroir).
vp.gizmo.addEventListener('objectChange', () => {
  const w = vp.gizmo.object; if (!w) return;
  const xf = state.xf[ui.slot], mir = w.userData.mirrored;
  xf.p = [mir ? -w.position.x : w.position.x, w.position.y, w.position.z].map(v => +v.toFixed(4));
  const r = [w.rotation.x, w.rotation.y, w.rotation.z].map(v => +(v * 180 / Math.PI).toFixed(1));
  xf.r = mir ? [r[0], -r[1], -r[2]] : r;
  xf.s = +Math.abs(w.scale.x).toFixed(3);
  figure.applyXf(ui.slot, xf);
});
vp.gizmo.addEventListener('mouseUp', () => { commit(); renderCustomize(); scheduleFigureThumbs(); });

// Sélection au clic dans le viewport (sans glisser).
const ray = new THREE.Raycaster();
let downAt = null;
$('vpCanvas').addEventListener('pointerdown', e => { downAt = e.button === 0 ? [e.clientX, e.clientY] : null; });
$('vpCanvas').addEventListener('pointerup', e => {
  if (!downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 4 || vp.gizmo.dragging) return;
  const r = e.target.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), vp.camera);
  const hit = ray.intersectObjects(figure.meshes(), false).find(h => SLOTS.some(s => s.id === h.object.userData.slot));
  if (hit) selectSlot(hit.object.userData.slot);
});

// Glisser-déposer de STL sur le viewport.
const vpEl = $('vp');
vpEl.addEventListener('dragover', e => { e.preventDefault(); $('drop').hidden = false; $('dropCat').textContent = CATEGORIES.find(c => c.id === (ui.cat === 'presets' ? slotById(ui.slot).cat : ui.cat)).label; });
vpEl.addEventListener('dragleave', e => { if (!vpEl.contains(e.relatedTarget)) $('drop').hidden = true; });
vpEl.addEventListener('drop', e => { e.preventDefault(); $('drop').hidden = true; importFiles([...e.dataTransfer.files]); });

document.addEventListener('keydown', e => {
  if (e.target.closest('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if ((e.ctrlKey || e.metaKey) && k === 'z') { e.preventDefault(); travel(e.shiftKey ? 1 : -1); return; }
  if ((e.ctrlKey || e.metaKey) && k === 'y') { e.preventDefault(); travel(1); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const tool = { q: 'select', g: 'translate', r: 'rotate', s: 'scale' }[k];
  if (tool) { ui.tool = tool; attachGizmo(); renderSteps(); }
  else if (k === 'f') vp.frameObject(figure.root);
  else if (k === 'delete' || k === 'backspace') $('removeBtn').click();
});

// Boucle : transition de pose, position des sockets à l'écran.
const tmp = new THREE.Vector3();
const PIN_AT = {
  torso: ['chest', [0, 0.02, 0.4]], head: ['head', [0, 0.16, 0.28]], armL: ['elbow_L', [0.06, 0, 0.12]], armR: ['elbow_R', [-0.06, 0, 0.12]],
  weapon: ['hand_R', [0, -0.25, 0.15]], legs: ['knee_L', [0.08, 0.02, 0.14]], back: ['chest', [0, 0.32, -0.46]], shoulders: ['clav_L', [0.3, 0.16, 0]], base: ['root', [0.5, 0, 0.42]],
};
let lastPins = 0;
vp.onFrame = dt => {
  const moving = figure.rig.update(dt);
  if (moving) figure.root.updateMatrixWorld(true);
  if (!ui.sockets) return;
  lastPins += dt;
  const el = $('vp'), w = el.clientWidth, h = el.clientHeight;
  for (const b of $('pins').children) {
    const [bone, off] = PIN_AT[b.dataset.slot];
    const bn = figure.rig.bones[bone];
    tmp.set(...off).applyMatrix4(bn.matrixWorld).project(vp.camera);
    const vis = tmp.z < 1 && Math.abs(tmp.x) < 1.05 && Math.abs(tmp.y) < 1.05;
    b.style.display = vis ? '' : 'none';
    b.style.left = ((tmp.x + 1) / 2 * w) + 'px';
    b.style.top = ((1 - tmp.y) / 2 * h) + 'px';
  }
};

renderAll();
syncUndo();
setStep('asm');
