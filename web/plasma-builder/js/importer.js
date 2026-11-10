// Importeur de briques : on dépose les fichiers d'une brique sortie de Maya (FBX, OBJ, glTF / GLB, texture, sockets
// .json du script tools/maya/plasma_export_brick.py), on vérifie qu'elle tombe bien sur l'ork, on règle ses sockets
// et on l'enregistre dans la bibliothèque du builder (IndexedDB du navigateur, voir bricks.js).
//
// - low / high : un objet dont le nom (ou celui du fichier) contient high, hd, decim… est rangé en high, le reste en
//   low ; modifiable fichier par fichier. Pas de high : le low sert aux deux.
// - locators SOCKET_<type>_<nom> et PLUG_<type> : sockets offerts par la brique et point qui vient sur le socket.
// - placement : « en place » (la brique a été modélisée sur l'ork, dans la scène de BOY_32mm : elle garde sa position),
//   « au plug » (locator PLUG_ ou point cliqué), ou « à l'origine » de la scène.
import * as THREE from 'three';
import { FBXLoader } from 'three/addons/loaders/FBXLoader.js';
import { OBJLoader } from 'three/addons/loaders/OBJLoader.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { mergeVertices, mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';
import { MeshBVH } from 'three-mesh-bvh';
import { Viewport, Thumbs } from './viewport.js';
import { OrkModel } from './riglab/model.js';
import { orkSockets, SOCKET_TYPES, parseLocatorName } from './sockets.js';
import { CATEGORIES, catById, Store, newId, geoToRec, recToGeo, triCount, mat4, packLibrary, unpackLibrary } from './bricks.js';
import { saveFile } from './io.js';

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const ICON = d => `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${d}"/></svg>`;
const TRASH = 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13';
const MODEL_EXT = /\.(fbx|obj|glb|gltf)$/i, IMAGE_EXT = /\.(jpe?g|png|webp|tga)$/i, SCENE_EXT = /\.(ma|mb)$/i;
// Édition développeur de l'app Windows : passerelle Maya (étagère PlasmaOrks, scènes .ma / .mb converties par mayapy).
const maya = window.plasmaDesktop?.maya || null;

const vp = new Viewport($('vpCanvas'));
vp.controls.target.set(0, 1.0, 0);
const thumbs = new Thumbs(160, 200);
let model = null, orkDefs = [];
let library = [];
let filter = 'all';
let draft = null;
const ui = { tool: 'orbit', show: 'low', ork: true, shade: 'resin', sel: null };
const preview = new THREE.Group();
preview.matrixAutoUpdate = false;
const orkMat = new THREE.MeshStandardMaterial({ color: 0x55585e, roughness: 0.85 });

// ---------------------------------------------------------------- brouillon
function emptyDraft() {
  const cat = catById(filter !== 'all' ? filter : 'weapon');
  return {
    id: null, created: 0, name: '', cat: cat.id, tags: [], meshes: [], locators: [], unit: 1, zup: false,
    texture: null, textureName: '', mask: null, maskName: '', flipY: true,
    home: cat.home, plugType: cat.type, placement: 'inplace', sockets: [], plugM: null, plugFrom: null, files: [], catAuto: filter === 'all',
  };
}
// Scène source -> espace brique (cm, Y en haut).
function conversion() {
  const m = new THREE.Matrix4().makeScale(draft.unit, draft.unit, draft.unit);
  if (draft.zup) m.premultiply(new THREE.Matrix4().makeRotationX(-Math.PI / 2));
  return m;
}
const unscaled = m => { const p = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3(); m.decompose(p, q, s); return new THREE.Matrix4().compose(p, q, new THREE.Vector3(1, 1, 1)); };
function brickGeo(mesh) { return mesh.src ? mesh.geo.clone().applyMatrix4(conversion()) : mesh.geo; }
const lodGeos = lod => draft.meshes.filter(m => m.lod === lod).map(brickGeo);

// Sockets et plug lus dans les locators (refaits quand l'unité ou l'axe changent ; ceux ajoutés à la main restent).
function applyLocators() {
  const C = conversion();
  draft.sockets = draft.sockets.filter(s => s.from !== 'file');
  if (draft.plugFrom === 'file') { draft.plugM = null; draft.plugFrom = null; }
  for (const l of draft.locators) {
    const info = parseLocatorName(l.name);
    if (!info) continue;
    const m = unscaled(C.clone().multiply(l.m));
    if (info.kind === 'plug') { draft.plugM = m; draft.plugFrom = 'file'; if (info.type !== 'any') draft.plugType = info.type; }
    else draft.sockets.push({ name: uniqueName(info.name), type: info.type, m, from: 'file' });
  }
}
function uniqueName(n) { let k = n, i = 2; while (draft.sockets.some(s => s.name === k)) k = n + '_' + i++; return k; }

// ---------------------------------------------------------------- lecture des fichiers
function lodHint(n) {
  n = n.toLowerCase();
  if (/(^|[^a-z])(high|hi|hd|hp|decim\w*|zbrush|sculpt\w*|dense)([^a-z]|$)/.test(n)) return 'high';
  if (/(^|[^a-z])(low|lo|lp|base\w*|retopo\w*)([^a-z]|$)/.test(n)) return 'low';
  return null;
}
async function readModel(file, urls) {
  const manager = new THREE.LoadingManager();
  // les textures référencées par le fichier sont cherchées parmi les fichiers déposés
  manager.setURLModifier(url => urls.get(decodeURIComponent(url.split(/[\\/]/).pop()).toLowerCase()) || url);
  const ext = file.name.split('.').pop().toLowerCase();
  let root, flipY = true, unit = null;
  if (ext === 'fbx') { root = new FBXLoader(manager).parse(await file.arrayBuffer(), ''); unit = root.userData.unitScaleFactor || null; }
  else if (ext === 'obj') root = new OBJLoader(manager).parse(await file.text());
  else {
    const data = ext === 'glb' ? await file.arrayBuffer() : await file.text();
    root = (await new GLTFLoader(manager).parseAsync(data, '')).scene;
    flipY = false;
    unit = 100;   // glTF est en mètres
  }
  root.updateMatrixWorld(true);
  const meshes = [], locators = [];
  root.traverse(o => {
    if (o.isMesh && o.geometry?.attributes.position) {
      let g = new THREE.BufferGeometry();
      for (const k of ['position', 'normal', 'uv']) if (o.geometry.attributes[k]) g.setAttribute(k, o.geometry.attributes[k].clone());
      if (o.geometry.index) g.setIndex(o.geometry.index.clone());
      g.applyMatrix4(o.matrixWorld);
      if (!g.index) g = mergeVertices(g, 1e-6);
      const name = o.name || file.name;
      meshes.push({ name, file: file.name, geo: g, lod: lodHint(name) || lodHint(file.name) || 'low', src: true, uv: !!g.attributes.uv });
    } else if (parseLocatorName(o.name || '')) locators.push({ name: o.name, m: o.matrixWorld.clone() });
  });
  return { meshes, locators, flipY, unit };
}

async function addFiles(files) {
  if (!model) return;
  if (!draft) draft = emptyDraft();
  const urls = new Map(files.map(f => [f.name.toLowerCase(), URL.createObjectURL(f)]));
  const fresh = !draft.meshes.length;
  let found = 0;
  for (const f of files) {
    try {
      if (MODEL_EXT.test(f.name)) {
        const r = await readModel(f, urls);
        draft.meshes.push(...r.meshes);
        draft.locators.push(...r.locators);
        if (fresh) { draft.flipY = r.flipY; if (r.unit) draft.unit = r.unit; }
        if (!draft.name) draft.name = f.name.replace(/\.[^.]+$/, '').replace(/[_-](low|high|lo|hi|hd)$/i, '');
        draft.files.push(f.name);
        found += r.meshes.length;
      } else if (IMAGE_EXT.test(f.name)) {
        const blob = new Blob([await f.arrayBuffer()], { type: f.type || 'image/' + f.name.split('.').pop().toLowerCase().replace('jpg', 'jpeg') });
        if (/(^|[^a-z])(mask|id|faction)([^a-z]|$)/i.test(f.name)) { draft.mask = blob; draft.maskName = f.name; }
        else { draft.texture = blob; draft.textureName = f.name; }
      } else if (/\.json$/i.test(f.name)) {
        const j = JSON.parse(await f.text());
        for (const l of j.locators || []) draft.locators.push({ name: l.name, m: new THREE.Matrix4().fromArray(l.matrix) });
        if (j.units === 'm') draft.unit = 100; else if (j.units === 'mm') draft.unit = 0.1;
        draft.files.push(f.name);
      }
    } catch (err) {
      console.error(err);
      toast(`Impossible de lire ${f.name}`);
    }
  }
  setTimeout(() => urls.forEach(u => URL.revokeObjectURL(u)), 4000);
  // deux fichiers sans indice low / high : le plus lourd est le high
  const byFile = new Map();
  for (const m of draft.meshes) if (m.src) byFile.set(m.file, (byFile.get(m.file) || 0) + (m.geo.index ? m.geo.index.count : m.geo.attributes.position.count) / 3);
  if (byFile.size === 2 && !draft.meshes.some(m => lodHint(m.name) || lodHint(m.file))) {
    const heavy = [...byFile].sort((a, b) => b[1] - a[1])[0][0];
    for (const m of draft.meshes) m.lod = m.file === heavy ? 'high' : 'low';
  }
  if (draft.meshes.length && !draft.meshes.some(m => m.lod === 'low')) draft.meshes.filter(m => m.lod === 'high').forEach(m => { m.lod = 'low'; });
  applyLocators();
  // un locator PLUG_<type> dit aussi ce qu'est la brique
  if (draft.catAuto && draft.plugFrom === 'file') {
    const cat = CATEGORIES.find(c => c.type === draft.plugType);
    if (cat) { draft.cat = cat.id; draft.home = cat.home; }
  }
  if (fresh && draft.meshes.length) guessPlacement();
  if (found) toast(`${found} objet${found > 1 ? 's' : ''} lu${found > 1 ? 's' : ''}${draft.locators.length ? `, ${draft.locators.length} locator${draft.locators.length > 1 ? 's' : ''}` : ''}`);
  refresh(true);
}

// Fichiers déposés ou choisis : les scènes Maya partent à la conversion (édition développeur), le reste est lu ici.
function takeFiles(files) {
  const scenes = files.filter(f => SCENE_EXT.test(f.name));
  const rest = files.filter(f => !SCENE_EXT.test(f.name));
  if (rest.length) addFiles(rest);
  if (!scenes.length) return;
  if (!maya) { toast('Les scènes Maya (.ma / .mb) s\'importent seulement dans l\'édition développeur de l\'app. Exporte la brique en .fbx.'); return; }
  for (const f of scenes) convertScene(f);
}
async function convertScene(f) {
  setBusy(`Maya convertit ${f.name}…`);
  try {
    const res = await maya.convert(f);
    if (res?.error) { setBusy(null); toast(res.error, 8000); }
    // le résultat arrive par maya.onFiles, comme un envoi de l'étagère
  } catch (err) { console.error(err); setBusy(null); toast('Conversion Maya impossible : ' + (err.message || err), 8000); }
}
function setBusy(msg) {
  const l = $('loading');
  l.classList.toggle('busy', !!msg);
  l.hidden = !msg;
  if (msg) l.querySelector('span').textContent = msg;
}
// Une brique envoyée depuis Maya remplace le brouillon en cours (nouvelle brique).
async function fromMaya(p) {
  setBusy(null);
  if (p.error) { toast(p.error, 8000); return; }
  draft = emptyDraft();
  ui.sel = null;
  const files = p.files.map(f => new File([f.data], f.name));
  await addFiles(files);
  if (p.name) draft.name = p.name;
  // catégorie choisie dans la fenêtre Plasma Bridge (sinon celle du PLUG, ou devinée par le placement)
  const cat = p.manifest?.category && catById(p.manifest.category);
  if (cat && cat.id === p.manifest.category && draft.plugFrom !== 'file') { draft.cat = cat.id; draft.home = cat.home; draft.plugType = cat.type; draft.catAuto = false; }
  refresh(true);
  for (const w of p.manifest?.warnings || []) console.warn('[Maya]', w);
  toast(`Reçu de Maya : ${p.name || files[0].name}${p.manifest?.warnings?.length ? ` (${p.manifest.warnings.length} remarque${p.manifest.warnings.length > 1 ? 's' : ''}, voir la console F12)` : ''}`, 4000);
  renderLibrary();
}

// En place si la brique est déjà autour d'un socket de l'ork (modélisée sur l'ork dans Maya) : ce socket devient son
// socket par défaut et, tant que la catégorie n'a pas été choisie à la main, sa catégorie. Sinon au plug s'il existe,
// sinon à l'origine. Le socle n'est jamais deviné (une brique à l'origine de la scène est entre les pieds).
function guessPlacement(type = null) {
  const box = new THREE.Box3();
  for (const g of lodGeos('low')) { g.computeBoundingBox(); box.union(g.boundingBox); }
  if (box.isEmpty()) return;
  const c = box.getCenter(new THREE.Vector3()), reach = Math.max(8, box.getSize(new THREE.Vector3()).length() * 0.6);
  let best = null, bd = Infinity;
  for (const s of orkDefs) {
    if (type ? s.type !== type : s.type === 'base') continue;
    const d = c.distanceTo(new THREE.Vector3().setFromMatrixPosition(s.rest));
    if (d < bd) { bd = d; best = s; }
  }
  if (best && bd < reach && !draft.plugM) {
    draft.placement = 'inplace';
    draft.home = best.id;
    if (!type && draft.catAuto) {
      const cat = CATEGORIES.find(x => x.type === best.type);
      if (cat) { draft.cat = cat.id; if (draft.plugFrom !== 'file') draft.plugType = cat.type; }
    }
  } else draft.placement = draft.plugM ? 'plug' : 'origin';
}

// ---------------------------------------------------------------- aperçu
// La brique est montrée là où elle tombera sur l'ork au repos : socket par défaut ∘ plug⁻¹.
function plugMatrix() {
  const home = orkDefs.find(s => s.id === draft.home);
  if (draft.placement === 'inplace' && home) return home.rest.clone();
  if (draft.placement === 'plug' && draft.plugM) return draft.plugM.clone();
  return new THREE.Matrix4();
}
function previewMatrix() {
  const home = orkDefs.find(s => s.id === draft.home);
  return (home ? home.rest.clone() : new THREE.Matrix4()).multiply(plugMatrix().invert());
}
let texture = null, textureBlob = null;
function brickMaterial() {
  if (ui.shade === 'wire') return new THREE.MeshBasicMaterial({ color: 0xd8dade, wireframe: true });
  if (ui.shade === 'paint' && draft.texture) {
    if (textureBlob !== draft.texture) {
      texture?.dispose();
      textureBlob = draft.texture;
      const img = new Image();
      texture = new THREE.Texture(img);
      img.onload = () => { texture.needsUpdate = true; };
      img.src = URL.createObjectURL(draft.texture);
      texture.colorSpace = THREE.SRGBColorSpace;
      texture.anisotropy = 8;
    }
    texture.flipY = draft.flipY;
    texture.needsUpdate = !!texture.image.complete;
    return new THREE.MeshPhysicalMaterial({ map: texture, roughness: 0.6 });
  }
  return new THREE.MeshPhysicalMaterial({ color: 0xc9a066, roughness: 0.5, clearcoat: 0.1 });
}
const markers = [];
function buildPreview() {
  preview.clear();
  vp.gizmo.detach();
  markers.length = 0;
  if (!draft) return;
  preview.matrix.copy(previewMatrix());
  const lod = ui.show === 'high' && draft.meshes.some(m => m.lod === 'high') ? 'high' : 'low';
  const mat = brickMaterial();
  for (const m of draft.meshes.filter(x => x.lod === lod)) {
    const mesh = new THREE.Mesh(brickGeo(m), mat);
    mesh.castShadow = mesh.receiveShadow = true;
    mesh.userData.pickable = true;
    preview.add(mesh);
  }
  const axis = (m, size, key) => {
    const o = new THREE.Object3D();
    o.matrix.copy(m);
    o.matrix.decompose(o.position, o.quaternion, o.scale);
    const a = new THREE.AxesHelper(size);
    a.material.depthTest = false;
    a.renderOrder = 20;
    a.userData.helper = true;
    o.add(a);
    o.userData.key = key;
    preview.add(o);
    markers.push(o);
    return o;
  };
  draft.sockets.forEach((s, i) => axis(s.m, 2.5, i));
  if (draft.placement === 'plug' && draft.plugM) axis(draft.plugM, 3, 'plug');
  else if (draft.placement === 'inplace') axis(plugMatrix(), 3, 'plug');
  attachGizmo();
  renderPins();
}
function attachGizmo() {
  const o = markers.find(m => m.userData.key === ui.sel);
  const editable = o && (ui.sel !== 'plug' || draft.placement === 'plug');
  if (!editable || !['translate', 'rotate'].includes(ui.tool)) { vp.gizmo.detach(); return; }
  vp.gizmo.setMode(ui.tool);
  vp.gizmo.setSpace('local');
  vp.gizmo.attach(o);
}
vp.gizmo.addEventListener('objectChange', () => {
  const o = vp.gizmo.object;
  if (!o || !draft) return;
  o.updateMatrix();
  if (o.userData.key === 'plug') { draft.plugM = o.matrix.clone(); draft.plugFrom = 'click'; preview.matrix.copy(previewMatrix()); }
  else { const s = draft.sockets[o.userData.key]; s.m = o.matrix.clone(); s.from = 'click'; }
});
vp.gizmo.addEventListener('mouseUp', () => { renderForm(); runChecks(); });

function renderPins() {
  const pins = markers.map(o => {
    const k = o.userData.key;
    const label = k === 'plug' ? `PLUG ${SOCKET_TYPES[draft.plugType] || draft.plugType}` : `${draft.sockets[k].name} · ${SOCKET_TYPES[draft.sockets[k].type] || draft.sockets[k].type}`;
    return `<button type="button" class="pin ${k === 'plug' ? 'plug' : 'sock'}" data-mark="${k}" aria-pressed="${ui.sel === k}"><i></i>${esc(label)}</button>`;
  });
  $('pins').innerHTML = pins.join('');
}

// ---------------------------------------------------------------- contrôles
const mmPerCm = () => 32 / (model.info.eyeHeight - model.info.floor);
function positionsOnly(geos) {
  const list = geos.map(g => { const c = new THREE.BufferGeometry(); c.setAttribute('position', g.attributes.position); if (g.index) c.setIndex(g.index); return c.index ? c.toNonIndexed() : c; });
  return mergeVertices(mergeGeometries(list, false), 1e-4);
}
function openEdges(g) {
  const idx = g.index.array, n = g.attributes.position.count, edges = new Map();
  for (let i = 0; i < idx.length; i += 3) for (let k = 0; k < 3; k++) {
    const a = idx[i + k], b = idx[i + (k + 1) % 3], key = a < b ? a * n + b : b * n + a;
    edges.set(key, (edges.get(key) || 0) + 1);
  }
  let open = 0;
  for (const c of edges.values()) if (c === 1) open++;
  return open;
}
function runChecks() {
  const out = [];
  const add = (cls, txt) => out.push(`<li class="${cls}">${txt}</li>`);
  if (!draft || !draft.meshes.length) { $('fChecks').innerHTML = '<li class="muted">Dépose une brique pour lancer les contrôles.</li>'; return; }
  const low = lodGeos('low'), high = lodGeos('high');
  const tri = gs => Math.round(gs.reduce((n, g) => n + (g.index ? g.index.count : g.attributes.position.count) / 3, 0));
  add(low.length ? 'ok' : 'bad', low.length ? `Low : ${tri(low).toLocaleString('fr-FR')} triangles (${low.length} objet${low.length > 1 ? 's' : ''})` : 'Aucun objet en low');
  add(high.length ? 'ok' : 'warn', high.length ? `High : ${tri(high).toLocaleString('fr-FR')} triangles` : 'Pas de high : le low servira aussi en high et à l\'impression');
  const uv = draft.meshes.filter(m => m.lod !== 'skip').every(m => m.uv);
  add(uv ? 'ok' : 'warn', uv ? 'UV présentes' : 'Objets sans UV : la texture ne s\'appliquera pas dessus');
  add(draft.texture ? 'ok' : 'warn', draft.texture ? `Texture : ${esc(draft.textureName)}` : 'Pas de texture (mode Peint en gris)');
  const box = new THREE.Box3();
  for (const g of low) { g.computeBoundingBox(); box.union(g.boundingBox); }
  const size = box.getSize(new THREE.Vector3()).multiplyScalar(mmPerCm());
  add(size.length() > 0.5 && size.length() < 60 ? 'ok' : 'warn', `Taille à l'échelle 32 mm : ${size.x.toFixed(1)} × ${size.y.toFixed(1)} × ${size.z.toFixed(1)} mm${size.length() < 0.5 ? ' (minuscule : vérifie les unités)' : size.length() > 60 ? ' (énorme : vérifie les unités)' : ''}`);
  const home = orkDefs.find(s => s.id === draft.home);
  if (draft.placement === 'inplace' && home) {
    const d = box.getCenter(new THREE.Vector3()).distanceTo(new THREE.Vector3().setFromMatrixPosition(home.rest));
    add(d < 15 ? 'ok' : 'warn', d < 15 ? `En place : à ${(d * mmPerCm()).toFixed(1)} mm de ${home.label.toLowerCase()}` : `En place, mais à ${(d * mmPerCm()).toFixed(0)} mm de ${home.label.toLowerCase()} : la brique a-t-elle été modélisée sur l'ork ?`);
  }
  if (draft.placement === 'plug' && !draft.plugM) add('bad', 'Pas de plug : ajoute un locator PLUG_ ou place-le au clic');
  // le high doit recouvrir le low (même scène, même position)
  if (high.length && low.length) {
    const bvh = new MeshBVH(positionsOnly(low));
    const hp = positionsOnly(high).attributes.position, step = Math.max(1, Math.floor(hp.count / 4000));
    const p = new THREE.Vector3(), hit = {};
    let sum = 0, max = 0, n = 0;
    for (let i = 0; i < hp.count; i += step) { p.fromBufferAttribute(hp, i); bvh.closestPointToPoint(p, hit); sum += hit.distance; max = Math.max(max, hit.distance); n++; }
    const mean = sum / n * mmPerCm(), mx = max * mmPerCm();
    add(mean < 0.6 ? 'ok' : mean < 2 ? 'warn' : 'bad', `Écart high / low : ${mean.toFixed(2)} mm en moyenne, ${mx.toFixed(2)} mm au plus${mean >= 2 ? ' : ils ne se superposent pas (même scène, même position ?)' : ''}`);
  }
  const print = high.length ? high : low;
  if (print.length) {
    const open = openEdges(positionsOnly(print));
    add(open ? 'warn' : 'ok', open ? `${open.toLocaleString('fr-FR')} arêtes ouvertes sur le ${high.length ? 'high' : 'low'} : à fermer pour l'impression` : `Maillage ${high.length ? 'high' : 'low'} fermé (imprimable)`);
  }
  const ns = draft.sockets.length;
  add('ok', `${ns ? `${ns} socket${ns > 1 ? 's' : ''}` : 'Aucun socket'} sur la brique, plug ${draft.placement === 'inplace' ? 'en place sur ' + (home?.label.toLowerCase() || '?') : draft.placement === 'plug' ? (draft.plugM ? (draft.plugFrom === 'file' ? 'lu dans le fichier' : 'placé à la main') : 'manquant') : 'à l\'origine'}`);
  $('fChecks').innerHTML = out.join('');
}

// ---------------------------------------------------------------- formulaire
function fillSelects() {
  $('fCat').innerHTML = CATEGORIES.map(c => `<option value="${c.id}">${c.label}</option>`).join('');
  $('fHome').innerHTML = orkDefs.map(s => `<option value="${s.id}">${s.label}</option>`).join('');
  $('fPlugType').innerHTML = Object.entries(SOCKET_TYPES).map(([k, v]) => `<option value="${k}">${v} (${k})</option>`).join('');
}
const typeOptions = cur => Object.entries(SOCKET_TYPES).map(([k, v]) => `<option value="${k}" ${k === cur ? 'selected' : ''}>${v}</option>`).join('');
function renderForm() {
  const d = draft || emptyDraft();
  $('formTitle').textContent = d.id ? d.name || 'Brique' : 'Nouvelle brique';
  $('deleteBtn').hidden = !d.id;
  $('fName').value = d.name;
  $('fCat').value = d.cat;
  $('fTags').value = d.tags.join(', ');
  $('fUnit').value = String(d.unit);
  $('fZup').checked = d.zup;
  const anySrc = d.meshes.some(m => m.src);
  $('fUnit').disabled = $('fZup').disabled = !anySrc;
  $('fHome').value = d.home;
  $('fPlugType').value = d.plugType;
  document.querySelectorAll('input[name="placement"]').forEach(r => { r.checked = r.value === d.placement; });
  $('fFiles').innerHTML = d.meshes.length ? d.meshes.map((m, i) => {
    const t = Math.round((m.geo.index ? m.geo.index.count : m.geo.attributes.position.count) / 3);
    return `<div class="it"><span title="${esc(m.file)}">${esc(m.name)} <small>${t.toLocaleString('fr-FR')} tri</small></span><select data-lod="${i}" aria-label="Version de ${esc(m.name)}"><option value="low" ${m.lod === 'low' ? 'selected' : ''}>Low</option><option value="high" ${m.lod === 'high' ? 'selected' : ''}>High</option><option value="skip" ${m.lod === 'skip' ? 'selected' : ''}>Ignorer</option></select><button type="button" class="icon" data-rmesh="${i}" aria-label="Retirer ${esc(m.name)}">${ICON(TRASH)}</button></div>`;
  }).join('') : '<span class="muted">Aucun fichier.</span>';
  $('fSockets').innerHTML = d.sockets.length ? d.sockets.map((s, i) => `<div class="it${ui.sel === i ? ' sel' : ''}"><input type="text" data-sname="${i}" value="${esc(s.name)}" aria-label="Nom du socket" spellcheck="false"><select data-stype="${i}" aria-label="Type du socket">${typeOptions(s.type)}</select><button type="button" class="icon" data-rsock="${i}" aria-label="Supprimer le socket ${esc(s.name)}">${ICON(TRASH)}</button></div>`).join('')
    : '<span class="muted">Aucun socket : la brique ne porte rien.</span>';
  const tex = [];
  if (d.texture) tex.push(`<div class="it"><span>${esc(d.textureName || 'basecolor')} <small>basecolor</small></span><span></span><button type="button" class="icon" data-rtex="texture" aria-label="Retirer la texture">${ICON(TRASH)}</button></div>`);
  if (d.mask) tex.push(`<div class="it"><span>${esc(d.maskName || 'masque')} <small>masque faction</small></span><span></span><button type="button" class="icon" data-rtex="mask" aria-label="Retirer le masque">${ICON(TRASH)}</button></div>`);
  $('fTex').innerHTML = tex.join('') || '<span class="muted">Aucune texture. Dépose un .jpg ou .png (un nom avec « mask » est rangé en masque de faction).</span>';
  $('saveBtn').disabled = !d.meshes.some(m => m.lod === 'low');
  $('saveBtn').textContent = d.id ? 'Enregistrer les modifications' : 'Enregistrer dans la bibliothèque';
  $('dropzone').hidden = !!d.meshes.length;
  $('vpInfo').textContent = d.meshes.length ? `${d.files.join(', ') || d.name}\nAperçu sur ${orkDefs.find(s => s.id === d.home)?.label.toLowerCase() || 'l\'ork'}, ork au repos` : '';
}
function refresh(frame = false) {
  renderForm();
  buildPreview();
  runChecks();
  if (frame && preview.children.length) frameBrick();
}
function frameBrick() {
  const box = new THREE.Box3();
  preview.updateMatrixWorld(true);
  preview.children.forEach(m => { if (m.isMesh) box.expandByObject(m); });
  if (box.isEmpty()) return;
  const helper = new THREE.Mesh(new THREE.BoxGeometry(...box.getSize(new THREE.Vector3()).multiplyScalar(2.2).toArray()));
  box.getCenter(helper.position);
  vp.frameObject(helper, new THREE.Vector3(0.6, 0.3, 1));
}

$('fName').addEventListener('input', e => { if (draft) draft.name = e.target.value; });
$('fTags').addEventListener('change', e => { if (draft) draft.tags = e.target.value.split(',').map(s => s.trim()).filter(Boolean); });
$('fCat').addEventListener('change', e => {
  draft ||= emptyDraft();
  const c = catById(e.target.value);
  draft.cat = c.id; draft.home = c.home; draft.catAuto = false;
  if (draft.plugFrom !== 'file') draft.plugType = c.type;
  if (draft.meshes.length && draft.placement !== 'plug') guessPlacement(c.type);
  refresh(true);
});
$('fHome').addEventListener('change', e => { draft ||= emptyDraft(); draft.home = e.target.value; refresh(true); });
$('fPlugType').addEventListener('change', e => { draft ||= emptyDraft(); draft.plugType = e.target.value; renderPins(); runChecks(); });
$('fUnit').addEventListener('change', e => { draft.unit = +e.target.value; applyLocators(); refresh(true); });
$('fZup').addEventListener('change', e => { draft.zup = e.target.checked; applyLocators(); refresh(true); });
$('fPlacement').addEventListener('change', e => { draft ||= emptyDraft(); draft.placement = e.target.value; refresh(true); });
$('fFiles').addEventListener('change', e => { const i = e.target.dataset.lod; if (i != null) { draft.meshes[+i].lod = e.target.value; refresh(); } });
$('fSockets').addEventListener('change', e => {
  const t = e.target;
  if (t.dataset.sname != null) { const s = draft.sockets[+t.dataset.sname]; const v = t.value.trim().replace(/[^\w-]+/g, '_'); s.name = v && !draft.sockets.some(x => x !== s && x.name === v) ? v : s.name; s.from = 'click'; }
  if (t.dataset.stype != null) { draft.sockets[+t.dataset.stype].type = t.value; draft.sockets[+t.dataset.stype].from = 'click'; }
  renderForm(); renderPins();
});
document.addEventListener('click', e => {
  const t = e.target.closest('button');
  if (!t) { if (!e.target.closest('.export')) $('libMenu').hidden = true; return; }
  const d = t.dataset;
  if (d.rmesh != null) { draft.meshes.splice(+d.rmesh, 1); refresh(); }
  else if (d.rsock != null) { draft.sockets.splice(+d.rsock, 1); ui.sel = null; refresh(); }
  else if (d.rtex) { draft[d.rtex] = null; refresh(); }
  else if (d.mark != null) { ui.sel = d.mark === 'plug' ? 'plug' : +d.mark; if (ui.tool === 'orbit' || ui.tool === 'socket' || ui.tool === 'plug') setTool('translate'); else { attachGizmo(); renderPins(); renderForm(); } }
  else if (d.tool) setTool(d.tool);
  else if (d.show) { ui.show = d.show; renderBar(); buildPreview(); }
  else if (d.shade) { ui.shade = d.shade; renderBar(); buildPreview(); }
  else if (d.cat) { filter = d.cat; renderLibrary(); }
  else if (d.brick) loadRec(library.find(r => r.id === d.brick));
  else if (d.lib) { $('libMenu').hidden = true; if (d.lib === 'export') exportLibrary(); else $('libInput').click(); }
});
function setTool(t) { ui.tool = t; renderBar(); attachGizmo(); renderPins(); renderForm(); }
function renderBar() {
  $('tools').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.tool === ui.tool)));
  document.querySelectorAll('[data-show]').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.show === ui.show)));
  $('orkBtn').setAttribute('aria-pressed', String(ui.ork));
  $('shading').querySelectorAll('[data-shade]').forEach(b => b.classList.toggle('on', b.dataset.shade === ui.shade));
  $('vpCanvas').style.cursor = ui.tool === 'socket' || ui.tool === 'plug' ? 'crosshair' : '';
}
$('orkBtn').addEventListener('click', () => { ui.ork = !ui.ork; model.root.visible = ui.ork; renderBar(); });
$('frameBtn').addEventListener('click', frameBrick);
$('newBtn').addEventListener('click', () => { draft = emptyDraft(); ui.sel = null; refresh(); renderLibrary(); });
$('pickBtn').addEventListener('click', () => $('fileInput').click());
$('fileInput').addEventListener('change', e => { takeFiles([...e.target.files]); e.target.value = ''; });
$('libMenuBtn').addEventListener('click', () => { const m = $('libMenu'); m.hidden = !m.hidden; $('libMenuBtn').setAttribute('aria-expanded', String(!m.hidden)); });

// Clic sur la brique avec l'outil socket ou plug : un repère au point touché, axes de la scène.
const ray = new THREE.Raycaster();
let downAt = null;
$('vpCanvas').addEventListener('pointerdown', e => { downAt = e.button === 0 ? [e.clientX, e.clientY] : null; });
$('vpCanvas').addEventListener('pointerup', e => {
  if (!draft || !downAt || Math.hypot(e.clientX - downAt[0], e.clientY - downAt[1]) > 4 || vp.gizmo.dragging) return;
  if (ui.tool !== 'socket' && ui.tool !== 'plug') return;
  const r = e.target.getBoundingClientRect();
  ray.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), vp.camera);
  const hit = ray.intersectObjects(preview.children.filter(m => m.userData.pickable), false)[0];
  if (!hit) { toast('Clique sur la brique'); return; }
  preview.updateMatrixWorld(true);
  const p = preview.worldToLocal(hit.point.clone());
  const m = new THREE.Matrix4().makeTranslation(p.x, p.y, p.z);
  if (ui.tool === 'plug') { draft.plugM = m; draft.plugFrom = 'click'; draft.placement = 'plug'; ui.sel = 'plug'; }
  else {
    draft.sockets.push({ name: uniqueName('socket'), type: 'any', m, from: 'click' });
    ui.sel = draft.sockets.length - 1;
  }
  ui.tool = 'translate';
  renderBar();
  refresh();
});

// Glisser-déposer sur l'aperçu.
const vpEl = $('vp');
vpEl.addEventListener('dragover', e => { e.preventDefault(); $('drop').hidden = false; });
vpEl.addEventListener('dragleave', e => { if (!vpEl.contains(e.relatedTarget)) $('drop').hidden = true; });
vpEl.addEventListener('drop', e => { e.preventDefault(); $('drop').hidden = true; takeFiles([...e.dataTransfer.files]); });

// ---------------------------------------------------------------- bibliothèque
async function loadLibrary() {
  try { library = await Store.all(); } catch (err) { console.error(err); library = []; toast('Stockage du navigateur indisponible : la bibliothèque ne peut pas être enregistrée ici'); }
  renderLibrary();
}
function renderLibrary() {
  $('catFilter').innerHTML = [['all', 'Toutes'], ...CATEGORIES.map(c => [c.id, c.label])].map(([id, l]) => `<button type="button" data-cat="${id}" aria-pressed="${filter === id}">${l}</button>`).join('');
  const list = library.filter(r => filter === 'all' || r.cat === filter);
  $('libCount').textContent = library.length;
  $('libGrid').innerHTML = list.length ? list.map(r => `<button type="button" class="tile" data-brick="${r.id}" aria-pressed="${draft?.id === r.id}" title="${esc(catById(r.cat).label)} · ${Math.round(triCount(r.lods.low) / 1000)}k tri low">${r.lods.high?.length ? '<em class="badge">HD</em>' : ''}${r.thumb ? `<img alt="" src="${r.thumb}">` : '<span class="ph"></span>'}<span>${esc(r.name)}</span></button>`).join('')
    : `<div class="libempty">${library.length ? 'Aucune brique dans cette catégorie.' : 'La bibliothèque est vide. Dépose les fichiers d\'une première brique dans l\'aperçu.'}</div>`;
}
function loadRec(r) {
  if (!r) return;
  draft = {
    id: r.id, created: r.created, name: r.name, cat: r.cat, tags: [...(r.tags || [])], unit: 1, zup: false, files: [...(r.source || [])],
    meshes: [...r.lods.low.map(g => ({ name: g.name || 'low', file: '', geo: recToGeo(g), lod: 'low', src: false, uv: !!g.uv })),
      ...(r.lods.high || []).map(g => ({ name: g.name || 'high', file: '', geo: recToGeo(g), lod: 'high', src: false, uv: !!g.uv }))],
    locators: [], texture: r.texture, textureName: r.textureName || 'basecolor', mask: r.mask, maskName: r.maskName || 'masque', flipY: r.flipY !== false,
    home: r.home, plugType: r.plug.type, placement: r.placement, sockets: r.sockets.map(s => ({ name: s.name, type: s.type, m: mat4(s.m), from: 'click' })),
    plugM: r.placement === 'plug' ? mat4(r.plug.m) : null, plugFrom: r.placement === 'plug' ? 'click' : null,
  };
  ui.sel = null;
  refresh(true);
  renderLibrary();
}

function thumbnail(lowGeos, highGeos) {
  const g = new THREE.Group();
  const mat = draft.texture && texture ? new THREE.MeshPhysicalMaterial({ map: texture, roughness: 0.6 }) : thumbs.mat;
  for (const geo of highGeos.length ? highGeos : lowGeos) g.add(new THREE.Mesh(geo, mat));
  g.scale.setScalar(model.k);
  const dir = { weapon: [0.2, 0.25, 1], back: [0.6, 0.3, -1], base: [0.3, 0.8, 1] }[draft.cat] || [0.45, 0.25, 1];
  thumbs.setSize(160, 200);
  return thumbs.render(g, new THREE.Vector3(...dir));
}
$('saveBtn').addEventListener('click', async () => {
  if (!draft) return;
  const low = draft.meshes.filter(m => m.lod === 'low'), high = draft.meshes.filter(m => m.lod === 'high');
  const lowG = low.map(brickGeo), highG = high.map(brickGeo);
  // la texture doit être prête pour la vignette
  if (draft.texture && ui.shade !== 'paint') { ui.shade = 'paint'; buildPreview(); ui.shade = 'resin'; buildPreview(); }
  if (texture?.image && !texture.image.complete) await new Promise(r => { texture.image.onload = () => { texture.needsUpdate = true; r(); }; setTimeout(r, 1500); });
  const now = Date.now();
  const rec = {
    id: draft.id || newId(), created: draft.created || now, updated: now,
    name: draft.name.trim() || 'brique', cat: draft.cat, tags: draft.tags,
    lods: { low: lowG.map((g, i) => geoToRec(g, low[i].name)), high: highG.map((g, i) => geoToRec(g, high[i].name)) },
    texture: draft.texture, textureName: draft.textureName, mask: draft.mask, maskName: draft.maskName, flipY: draft.flipY,
    plug: { type: draft.plugType, m: plugMatrix().toArray() },
    home: draft.home, placement: draft.placement,
    sockets: draft.sockets.map(s => ({ name: s.name, type: s.type, m: s.m.toArray() })),
    thumb: thumbnail(lowG, highG),
    stats: { low: triCount(lowG.map(g => geoToRec(g))), high: highG.length ? triCount(highG.map(g => geoToRec(g))) : 0 },
    source: draft.files,
  };
  try {
    await Store.put(rec);
    toast(`« ${rec.name} » enregistrée dans ${catById(rec.cat).label}`);
    await loadLibrary();
    loadRec(library.find(r => r.id === rec.id));
  } catch (err) {
    console.error(err);
    toast('Enregistrement impossible : ' + (err.message || err));
  }
});
// Deux clics pour supprimer : le premier arme le bouton.
let armed = 0;
$('deleteBtn').addEventListener('click', async () => {
  if (!draft?.id) return;
  if (Date.now() - armed > 2500) { armed = Date.now(); toast('Clique encore pour supprimer la brique'); return; }
  armed = 0;
  await Store.delete(draft.id);
  toast(`« ${draft.name} » supprimée`);
  draft = emptyDraft();
  refresh();
  loadLibrary();
});

async function exportLibrary() {
  if (!library.length) { toast('La bibliothèque est vide'); return; }
  toast('Préparation du ZIP…');
  try { await saveFile('plasma_bricks.zip', await packLibrary(library)); toast(`${library.length} brique${library.length > 1 ? 's' : ''} exportée${library.length > 1 ? 's' : ''}`); }
  catch (err) { console.error(err); toast('Export impossible : ' + (err.message || err)); }
}
$('libInput').addEventListener('change', async e => {
  const f = e.target.files[0];
  e.target.value = '';
  if (!f) return;
  try {
    const list = unpackLibrary(await f.arrayBuffer());
    for (const r of list) await Store.put(r);
    toast(`${list.length} brique${list.length > 1 ? 's' : ''} importée${list.length > 1 ? 's' : ''}`);
    loadLibrary();
  } catch (err) { console.error(err); toast('Ce fichier n\'est pas une bibliothèque de briques : ' + (err.message || err)); }
});

addEventListener('keydown', e => {
  if (e.target.closest?.('input, select, textarea')) return;
  const k = e.key.toLowerCase();
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  if (k === 'q') setTool('orbit');
  else if (k === 'w') setTool('translate');
  else if (k === 'e' || k === 'r') setTool('rotate');
  else if (k === 'f') frameBrick();
  else if (k === 'escape') { ui.sel = null; setTool('orbit'); }
});

let toastTimer = 0;
function toast(msg, ms = 2800) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), ms);
}

// Pastilles des sockets à l'écran.
const tmp = new THREE.Vector3();
vp.onFrame = () => {
  const el = $('vp'), w = el.clientWidth, h = el.clientHeight;
  preview.updateMatrixWorld(true);
  for (const b of $('pins').children) {
    const o = markers.find(m => String(m.userData.key) === b.dataset.mark);
    if (!o) continue;
    o.getWorldPosition(tmp).project(vp.camera);
    const vis = tmp.z < 1 && Math.abs(tmp.x) < 1.05 && Math.abs(tmp.y) < 1.05;
    b.style.display = vis ? '' : 'none';
    b.style.left = ((tmp.x + 1) / 2 * w) + 'px';
    b.style.top = ((1 - tmp.y) / 2 * h) + 'px';
  }
};

// ---------------------------------------------------------------- démarrage
if (maya) {
  $('fileInput').accept += ',.ma,.mb';
  $('dropHint').textContent = 'Modèle .fbx, .obj, .glb ou .gltf, ou directement la scène Maya .ma / .mb (convertie par Maya en arrière-plan), texture .jpg / .png. Depuis Maya : étagère PlasmaOrks, bouton Envoyer.';
}
renderBar();
loadLibrary();
try {
  model = await OrkModel.load('assets/ork_boy.glb', e => { if (e.total) $('loading').style.setProperty('--p', (e.loaded / e.total * 100).toFixed(0) + '%'); });
  model.fit();
  vp.scene.add(model.root);
  for (const [n, m] of Object.entries(model.meshes)) { m.material = orkMat; m.visible = !/_low$/.test(n); }
  orkDefs = orkSockets(model);
  model.ork.add(preview);
  fillSelects();
  draft = emptyDraft();
  refresh();
  vp.frameObject(model.root, new THREE.Vector3(0.55, 0.25, 1), true);
  $('loading').hidden = true;
  window.importer = { model, vp, draft: () => draft, addFiles, orkDefs, refresh };
  if (maya) { maya.onFiles(p => fromMaya(p).catch(err => { console.error(err); toast('Envoi Maya illisible : ' + (err.message || err), 8000); })); maya.ready(); }
} catch (err) {
  console.error(err);
  const l = $('loading');
  l.classList.add('err');
  l.textContent = 'Impossible de charger assets/ork_boy.glb. Sers le dossier web/plasma-builder avec un petit serveur (python -m http.server) et vérifie que le fichier est présent.';
}
