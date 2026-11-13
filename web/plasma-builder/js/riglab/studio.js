// Rig Lab, côté studio : les sockets de l'ork en locators visibles et réglables, et les meshes remplaçables.
// - Sockets : chaque socket est un repère accroché à son joint (flèche Y = le « haut » de la brique, celui du manche
//   d'une arme modélisée debout dans Maya ; X rouge, Z bleu). Clic pour le choisir, W déplacer, E tourner. Le réglage
//   est enregistré relatif au joint : il vaut dans toutes les poses, et le Builder l'utilise pour accrocher les briques.
// - Meshes : un OBJ glissé sur un slot (corps / tête, low / high, yeux, dents) remplace ce maillage, piloté par le
//   même rig (poids transférés depuis le maillage d'origine).
import * as THREE from 'three';
import { OBJLoader } from 'three/addons/loaders/OBJLoader.js';
import { orkSockets, loadSocketOverrides, saveSocketOverrides, SOCKET_TYPES } from '../sockets.js';
import { SLOTS, slotStore, transferToSlot, setSlotGeometry, slotToOBJ } from './meshslots.js';
import { saveFile } from '../io.js';

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const ICON = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const DEG = THREE.MathUtils.RAD2DEG;
const cssAccent = () => getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#f0435a';

// opts : { vp, model, toast, deselectBone(), tool() ('fk' | 'ik'), changedMeshes() }
export function createStudio(opts) {
  const { vp, model, toast } = opts;
  const gizmo = vp.gizmo;
  const st = { show: true, sel: null, tab: 'bones' };
  try { const t = localStorage.getItem('plasmaork.riglab.tab'); if (t) st.tab = t; st.show = localStorage.getItem('plasmaork.riglab.sockets') !== '0'; } catch { /* défaut */ }

  // ------------------------------------------------------------ sockets
  // à l'appel, l'ork est au repos (calcul des sockets d'origine)
  const defs = orkSockets(model, false);
  const over = loadSocketOverrides();
  const U = 1 / model.k;   // une unité du viewport en cm de l'ork
  const L = 0.13 * U;
  const loc = new Map();
  const accent = new THREE.Color(cssAccent());
  const mat = (color, opacity = 1) => new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity });
  const lineMat = color => new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true });
  for (const d of defs) {
    const g = new THREE.Group();
    g.name = 'SOCKET_' + d.id;
    g.userData = { socketId: d.id, helper: true, noExport: true };
    const m = over[d.id]?.local ? new THREE.Matrix4().fromArray(over[d.id].local) : d.local;
    m.decompose(g.position, g.quaternion, g.scale);
    // flèche Y (le haut de la brique)
    const shaftG = new THREE.CylinderGeometry(L * 0.035, L * 0.035, L, 8); shaftG.translate(0, L / 2, 0);
    const headG = new THREE.ConeGeometry(L * 0.11, L * 0.3, 12); headG.translate(0, L * 1.15, 0);
    const up = new THREE.Mesh(shaftG, mat(accent)), tip = new THREE.Mesh(headG, mat(accent));
    // axes X (rouge) et Z (bleu)
    const axes = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3(L * 0.6, 0, 0), new THREE.Vector3(), new THREE.Vector3(0, 0, L * 0.6)]), lineMat(0xffffff));
    axes.geometry.setAttribute('color', new THREE.Float32BufferAttribute([0.9, 0.4, 0.37, 0.9, 0.4, 0.37, 0.37, 0.62, 0.9, 0.37, 0.62, 0.9], 3));
    axes.material.vertexColors = true;
    const ball = new THREE.Mesh(new THREE.SphereGeometry(L * 0.14, 14, 10), mat(0xffffff));
    const parts = [up, tip, axes, ball];
    // mains : manche fantôme le long de Y, pour voir comment une arme modélisée debout tombera dans le poing
    if (d.type === 'grip') {
      const hg = new THREE.CylinderGeometry(L * 0.09, L * 0.09, L * 3.2, 12);
      parts.push(new THREE.Mesh(hg, mat(0xb5b7bd, 0.28)));
    }
    for (const p of parts) { p.renderOrder = 12; p.userData = { helper: true, noExport: true, socketId: d.id }; g.add(p); }
    g.userData.ball = ball; g.userData.arrow = [up, tip];
    (d.bone || model.ork).add(g);
    loc.set(d.id, g);
  }
  function paint() {
    accent.set(cssAccent());
    for (const [id, g] of loc) {
      const on = id === st.sel, custom = !!loadSocketOverrides()[id];
      g.visible = st.show;
      g.userData.ball.material.color.set(on ? accent : custom ? 0x7bc67e : 0xffffff);
      g.userData.ball.scale.setScalar(on ? 1.5 : 1);
      for (const a of g.userData.arrow) a.material.color.set(accent);
    }
  }
  addEventListener('plasma-faction', paint);

  const ray = new THREE.Raycaster();
  const _w = new THREE.Vector3();
  // socket sous la souris (boule du locator), en rayons X comme les os
  function pickSocket(ev) {
    if (!st.show) return null;
    const r = vp.canvas.getBoundingClientRect();
    ray.setFromCamera(new THREE.Vector2(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1), vp.camera);
    let best = null, bd = Infinity;
    for (const [id, g] of loc) {
      g.userData.ball.getWorldPosition(_w);
      const rad = 0.026 * (id === st.sel ? 1.5 : 1);
      const d = ray.ray.distanceSqToPoint(_w);
      if (d < rad * rad && d < bd) { bd = d; best = id; }
    }
    return best;
  }
  function selectSocket(id, quiet = false) {
    st.sel = id || null;
    if (id) { opts.deselectBone(); if (st.tab !== 'sockets') setTab('sockets'); }
    paint();
    if (!quiet) attach();
    renderSockets();
  }
  function attach() {
    const g = st.sel && loc.get(st.sel);
    if (!g) return false;
    gizmo.detach();
    gizmo.setMode(opts.tool() === 'ik' ? 'translate' : 'rotate');
    gizmo.setSpace('local');
    gizmo.attach(g);
    return true;
  }
  const localOf = g => new THREE.Matrix4().compose(g.position, g.quaternion, g.scale);
  function store(id) {
    const o = loadSocketOverrides();
    o[id] = { local: localOf(loc.get(id)).toArray().map(x => +x.toFixed(6)), date: Date.now() };
    saveSocketOverrides(o);
  }
  function onGizmo() { if (st.sel) syncFields(); }
  function onGizmoUp() {
    if (!st.sel) return;
    store(st.sel);
    paint(); renderSockets();
    toast(tr(`Socket ${label(st.sel)} enregistré : le Builder l'utilise au prochain chargement`, `Socket ${label(st.sel)} saved: the Builder uses it on next load`));
  }
  const label = id => defs.find(d => d.id === id)?.label || id;
  function reset(id) {
    const o = loadSocketOverrides();
    delete o[id];
    saveSocketOverrides(o);
    const d = defs.find(x => x.id === id), g = loc.get(id);
    d.local.decompose(g.position, g.quaternion, g.scale);
    g.updateMatrixWorld(true);
  }

  // panneau Sockets
  function renderSockets() {
    if (st.tab !== 'sockets') return;
    const o = loadSocketOverrides();
    $('sockList').innerHTML = defs.map((d, n) => `<button type="button" class="sockrow${o[d.id] ? ' custom' : ''}" data-sock="${d.id}" aria-pressed="${st.sel === d.id}">
      <span class="sock-n">${n + 1}</span><span class="sock-txt"><b>${esc(d.label)}</b><small>${esc(SOCKET_TYPES[d.type] || d.type)} · ${d.bone ? esc(d.bone.name.replace('QuickRigCharacter_', '')) : 'ORK_BOY'}${o[d.id] ? ' · ' + tr('réglé', 'adjusted') : ''}</small></span></button>`).join('');
    const d = st.sel && defs.find(x => x.id === st.sel);
    $('sockEdit').hidden = !d;
    $('sockShow').checked = st.show;
    $('sockResetAll').disabled = !Object.keys(o).length;
    if (!d) return;
    $('sockName').textContent = d.label;
    $('sockCode').textContent = 'SOCKET_' + (d.id.startsWith(d.type) ? d.id : d.type + '_' + d.id);
    $('sockReset').disabled = !o[d.id];
    syncFields();
  }
  function syncFields() {
    const g = st.sel && loc.get(st.sel);
    if (!g) return;
    const e = new THREE.Euler().setFromQuaternion(g.quaternion, 'XYZ');
    const vals = { px: g.position.x, py: g.position.y, pz: g.position.z, rx: e.x * DEG, ry: e.y * DEG, rz: e.z * DEG };
    $('sockXf').querySelectorAll('input').forEach(i => { if (document.activeElement !== i) i.value = (+vals[i.dataset.k]).toFixed(i.dataset.k[0] === 'p' ? 2 : 1); });
  }
  $('sockXf').addEventListener('input', e => {
    const i = e.target, g = st.sel && loc.get(st.sel);
    if (!g || !i.dataset.k || Number.isNaN(+i.value)) return;
    const k = i.dataset.k;
    if (k[0] === 'p') g.position['xyz'['xyz'.indexOf(k[1])]] = +i.value;
    else {
      const eu = new THREE.Euler().setFromQuaternion(g.quaternion, 'XYZ');
      eu[k[1]] = THREE.MathUtils.degToRad(+i.value);
      g.quaternion.setFromEuler(eu);
    }
    g.updateMatrixWorld(true);
  });
  $('sockXf').addEventListener('change', () => { if (st.sel) { store(st.sel); paint(); renderSockets(); } });
  $('sockList').addEventListener('click', e => { const b = e.target.closest('[data-sock]'); if (b) selectSocket(b.dataset.sock); });
  $('sockShow').addEventListener('change', e => {
    st.show = e.target.checked;
    try { localStorage.setItem('plasmaork.riglab.sockets', st.show ? '1' : '0'); } catch { /* ignoré */ }
    if (!st.show && st.sel) { st.sel = null; gizmo.detach(); }
    paint(); renderSockets();
  });
  $('sockReset').addEventListener('click', () => { if (!st.sel) return; reset(st.sel); paint(); renderSockets(); attach(); toast(tr('Socket remis à sa place calculée', 'Socket back to its computed place')); });
  $('sockResetAll').addEventListener('click', () => { for (const id of Object.keys(loadSocketOverrides())) reset(id); paint(); renderSockets(); attach(); toast(tr('Tous les sockets sont remis à leur place calculée', 'All sockets are back to their computed place')); });
  $('sockSnap').addEventListener('click', () => {
    // redresse le socket : flèche Y alignée sur le haut du monde, avant (Z) de la figurine conservé autant que possible
    const g = st.sel && loc.get(st.sel);
    if (!g) return;
    const parentQ = g.parent.getWorldQuaternion(new THREE.Quaternion());
    const rootQ = model.root.getWorldQuaternion(new THREE.Quaternion());
    const want = rootQ.clone();   // repère de la figurine
    g.quaternion.copy(parentQ.invert().multiply(want));
    g.updateMatrixWorld(true);
    store(st.sel); paint(); renderSockets();
  });

  // ------------------------------------------------------------ meshes
  const loader = new OBJLoader();
  function renderMeshes() {
    if (st.tab !== 'meshes') return;
    const slots = model.slots || {};
    $('meshSlots').innerHTML = SLOTS.map(s => {
      const m = model.meshes[s.id], info = slots[s.id];
      const verts = m ? m.geometry.attributes.position.count.toLocaleString(window.plasmaLang === 'fr' ? 'fr-FR' : 'en-US') : '—';
      return `<div class="slot${info ? ' custom' : ''}" data-slot="${s.id}">
        <div class="slot-head"><b>${esc(plasmaT(s.label))}</b><code>${s.id}</code></div>
        <div class="slot-src">${info ? `<span data-noi18n>${esc(info.file)}</span>` : tr('Ork Boy d\'origine', 'Original Ork Boy')} · ${verts} ${tr('sommets', 'vertices')}</div>
        <div class="slot-drop">${ICON('M12 3v12M7 10l5 5 5-5M4 20h16')}${tr('Glisse un OBJ ici', 'Drop an OBJ here')}</div>
        <div class="slot-act">
          <button type="button" data-pick="${s.id}">${tr('Choisir…', 'Choose…')}</button>
          <button type="button" data-objout="${s.id}" title="${tr('L\'OBJ actuel, à sa place dans la scène Maya (cm)', 'The current OBJ, in place in the Maya scene (cm)')}">${tr('Exporter', 'Export')}</button>
          <button type="button" data-slotreset="${s.id}" ${info ? '' : 'disabled'}>${tr('Origine', 'Original')}</button>
        </div></div>`;
    }).join('');
  }
  async function loadInto(slotId, file) {
    if (!/\.obj$/i.test(file.name)) { toast(tr('Il faut un fichier .obj', 'An .obj file is needed')); return; }
    toast(tr(`Transfert des poids sur ${file.name}…`, `Transferring weights to ${file.name}…`));
    await new Promise(r => setTimeout(r, 30));
    try {
      const root = loader.parse(await file.text());
      const geos = [];
      root.updateMatrixWorld(true);
      root.traverse(o => { if (o.isMesh) { const g = o.geometry.clone(); g.applyMatrix4(o.matrixWorld); for (const k of Object.keys(g.attributes)) if (!['position', 'uv'].includes(k)) g.deleteAttribute(k); geos.push(g.index ? g.toNonIndexed() : g); } });
      if (!geos.length) throw new Error(tr('aucun maillage dans le fichier', 'no mesh in the file'));
      const merged = mergeGeos(geos);
      const t0 = performance.now();
      const { rec, geo, scaled, maxDist } = transferToSlot(model, slotId, merged, file.name);
      setSlotGeometry(model, slotId, geo);
      (model.slots ||= {})[slotId] = { file: file.name, date: rec.date, verts: rec.position.length / 3 };
      try { await slotStore.put(rec); } catch (err) { console.error(err); toast(tr('Mesh appliqué mais pas enregistré (stockage plein ?)', 'Mesh applied but not saved (storage full?)')); }
      opts.changedMeshes();
      renderMeshes();
      const far = maxDist * 10 > 15;   // cm -> mm, au-delà d'1,5 cm du maillage d'origine
      toast(tr(`${file.name} sur ${slotId} : ${(rec.position.length / 3).toLocaleString('fr-FR')} sommets, poids transférés en ${Math.round(performance.now() - t0)} ms${scaled !== 1 ? `, unités corrigées ×${+scaled.toFixed(3)}` : ''}${far ? ' · attention, loin du maillage d\'origine : vérifie la position dans Maya' : ''}`,
        `${file.name} on ${slotId}: ${(rec.position.length / 3).toLocaleString('en-US')} vertices, weights transferred in ${Math.round(performance.now() - t0)} ms${scaled !== 1 ? `, units fixed ×${+scaled.toFixed(3)}` : ''}${far ? ' · warning, far from the original mesh: check the position in Maya' : ''}`));
    } catch (err) { console.error(err); toast(tr('Import impossible : ', 'Import failed: ') + (err.message || err)); }
  }
  function mergeGeos(geos) {
    const hasUV = geos.every(g => g.attributes.uv);
    let n = 0; for (const g of geos) n += g.attributes.position.count;
    const pos = new Float32Array(n * 3), uv = hasUV ? new Float32Array(n * 2) : null;
    let o = 0;
    for (const g of geos) { pos.set(g.attributes.position.array, o * 3); if (uv) uv.set(g.attributes.uv.array, o * 2); o += g.attributes.position.count; }
    const r = new THREE.BufferGeometry();
    r.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    if (uv) r.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    return r;
  }
  $('meshSlots').addEventListener('click', async e => {
    const t = e.target.closest('button');
    if (!t) return;
    if (t.dataset.pick) { $('slotInput').dataset.slot = t.dataset.pick; $('slotInput').click(); }
    else if (t.dataset.objout) saveFile(t.dataset.objout + '.obj', new TextEncoder().encode(slotToOBJ(model, t.dataset.objout)));
    else if (t.dataset.slotreset) {
      const id = t.dataset.slotreset;
      try { await slotStore.delete(id); } catch { /* ignoré */ }
      setSlotGeometry(model, id, null);
      delete model.slots?.[id];
      opts.changedMeshes();
      renderMeshes();
      toast(tr(`${id} : maillage d'origine`, `${id}: original mesh`));
    }
  });
  $('slotInput').addEventListener('change', e => { const f = e.target.files[0]; e.target.value = ''; if (f) loadInto(e.target.dataset.slot, f); });
  $('meshSlots').addEventListener('dragover', e => { const s = e.target.closest('[data-slot]'); if (!s) return; e.preventDefault(); document.querySelectorAll('.slot.over').forEach(x => x !== s && x.classList.remove('over')); s.classList.add('over'); });
  $('meshSlots').addEventListener('dragleave', e => { const s = e.target.closest('[data-slot]'); if (s && !s.contains(e.relatedTarget)) s.classList.remove('over'); });
  $('meshSlots').addEventListener('drop', e => {
    const s = e.target.closest('[data-slot]');
    if (!s) return;
    e.preventDefault();
    s.classList.remove('over');
    const f = e.dataTransfer.files[0];
    if (f) loadInto(s.dataset.slot, f);
  });

  // ------------------------------------------------------------ onglets du panneau de gauche
  function setTab(t) {
    st.tab = t;
    try { localStorage.setItem('plasmaork.riglab.tab', t); } catch { /* ignoré */ }
    document.querySelectorAll('#leftTabs [data-ltab]').forEach(b => b.setAttribute('aria-selected', String(b.dataset.ltab === t)));
    for (const p of ['bones', 'sockets', 'meshes']) $('lpane-' + p).hidden = p !== t;
    renderSockets(); renderMeshes();
  }
  $('leftTabs').addEventListener('click', e => { const b = e.target.closest('[data-ltab]'); if (b) setTab(b.dataset.ltab); });

  setTab(st.tab);
  paint();
  return {
    pickSocket, selectSocket, attach, onGizmo, onGizmoUp,
    get sel() { return st.sel; },
    isSocket: o => !!o?.userData?.socketId,
    tipFor: id => label(id),
    defs, loc,
  };
}
