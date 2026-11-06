// Hub : l'utilisateur débloque les pièces de son ork en ouvrant des packs (on coupe le paquet du doigt, on retourne
// 3 cartes) ou des coffres (3 coups). Chaque ouverture donne une commune, une rare et une rare ou extrême (coffre :
// extrême garantie), rangées dans la Plasma Library avec un compteur par pièce (×N, doublons recyclables en dents).
//
// Tout est provisoire en attendant le vrai contenu et le site :
// - CATALOGUE : pièces factices (nom + picto de catégorie). À remplacer par les briques de la bibliothèque.
// - Achats : les boutons [PRIX] ajoutent le pack pour la démo ; le paiement sera branché sur le site.
// - État du joueur : localStorage 'plasmaork.hub' (à remplacer par le compte en ligne).
import { $, esc, RULES, RARITY, RCOL, ICON, FACTIONS, NAMES, CATALOG, byId, KIT, catLabel, catSingular, S, save, ownedCount, dupes, pick, draw, commit, sparks, toast, center, setNum } from './loot.js';

// ---------------------------------------------------------------- tableau de bord
function pieceCard(p, opts = {}) {
  const c = S.owned[p.id] || 0, locked = !c && !opts.reveal;
  const badge = opts.isNew ? '<span class="cnt new">NEW</span>' : c > 1 ? `<span class="cnt">×${c}</span>` : '';
  return `<div class="pc ${locked ? 'locked' : p.rk}">${locked ? '' : badge}<img src="${ICON[p.cat]}" alt=""><b>${locked ? '???' : esc(p.name)}</b><span class="rar">${RARITY[p.rk]}</span><small>${catSingular[p.cat]}</small></div>`;
}
function render() {
  const faction = FACTIONS.find(f => f.id === S.faction) || FACTIONS[0];
  document.body.style.setProperty('--fc', faction.col);
  $('factions').innerHTML = FACTIONS.map(f => `<button type="button" role="radio" aria-checked="${f.id === faction.id}" aria-label="${f.label}" title="${f.label}" data-faction="${f.id}" style="--fc:${f.col}"><img src="${f.img}" alt=""></button>`).join('');
  $('orkSub').textContent = faction.label + ' · 32 mm';
  $('headSub').textContent = 'Da_Big_Boy_01 · ' + faction.label;

  setNum('dents', S.dents);
  const packs = S.packs, chests = S.chests;
  $('packTitle').textContent = packs ? `${packs} pack${packs > 1 ? 's' : ''} t'attend${packs > 1 ? 'ent' : ''}` : 'Plus de pack à ouvrir';
  $('openPack').disabled = !packs;
  $('openChest').disabled = !chests;
  $('chestCount').textContent = chests;
  $('packStack').classList.toggle('empty', !packs);
  const badge = packs + chests;
  $('navBadge').hidden = !badge; $('navBadge').textContent = badge;

  const total = CATALOG.length, own = ownedCount();
  $('libTotal').textContent = total;
  setNum('libOwned', own);
  for (const rk of ['c', 'r', 'x']) {
    const all = CATALOG.filter(p => p.rk === rk), n = all.filter(p => S.owned[p.id]).length;
    $('bar' + rk.toUpperCase()).style.width = (n / all.length * 100).toFixed(1) + '%';
    setNum('num' + rk.toUpperCase(), n);
  }
  const prevCats = render.cats || {};
  render.cats = {};
  $('cats').innerHTML = Object.keys(NAMES).map(cat => {
    const n = CATALOG.filter(p => p.cat === cat && S.owned[p.id]).length;
    render.cats[cat] = n;
    const bump = prevCats[cat] !== undefined && prevCats[cat] !== n;
    return `<div class="${bump ? 'bump' : ''}" data-cat="${cat}" title="${catLabel(cat)}"><img src="${ICON[cat]}" alt="${catLabel(cat)}"><b>${n}</b><span>/ ${NAMES[cat].length}</span></div>`;
  }).join('');
  const d = dupes();
  $('dupeInfo').textContent = d ? `${d} doublon${d > 1 ? 's' : ''} à recycler` : 'Aucun doublon';
  $('recycle').disabled = !d;

  const last = S.last.map(id => byId[id]).filter(Boolean);
  $('lastRow').innerHTML = last.length ? last.map(p => pieceCard(p)).join('') : '<div class="empty">Ouvre un pack : tes 3 dernières pièces apparaîtront ici.</div>';
  $('lastWhen').textContent = S.lastAt ? 'il y a ' + ago(S.lastAt) : 'aucun pour l’instant';

  const kitOwn = KIT.pieces.filter(id => S.owned[id]).length;
  $('kitName').textContent = `${KIT.name} · ${kitOwn} / ${KIT.pieces.length}`;
  $('kitBar').innerHTML = KIT.pieces.map(id => `<i class="${S.owned[id] ? 'on' : ''}" title="${S.owned[id] ? esc(byId[id].name) : '???'}"></i>`).join('');
  const miss = KIT.pieces.filter(id => !S.owned[id]).map(id => byId[id]);
  $('kitInfo').textContent = miss.length ? `Il manque : ${miss.map(p => catSingular[p.cat].toLowerCase() + ' ' + RARITY[p.rk].toLowerCase()).join(', ')}. Complet : ${KIT.reward}.` : `Kit complet : ${KIT.reward} !`;
  $('packDentsPrice').textContent = RULES.packInDents;
  document.querySelector('[data-buy="dents"]').disabled = S.dents < RULES.packInDents;
}
function ago(t) {
  const m = Math.round((Date.now() - t) / 60000);
  if (m < 1) return 'quelques secondes';
  if (m < 60) return m + ' min';
  const h = Math.round(m / 60); return h < 48 ? h + ' h' : Math.round(h / 24) + ' j';
}

// Les pièces gagnées volent jusqu'à la carte Plasma Library, puis les compteurs montent.
function flyToLibrary(rects, got) {
  const lib = $('libCard').getBoundingClientRect();
  rects.forEach((r, i) => {
    const p = got[i];
    const tile = $('cats').querySelector(`[data-cat="${p.cat}"]`);
    const t = (tile && tile.getBoundingClientRect().width) ? tile.getBoundingClientRect() : { left: lib.left + lib.width / 2 - 32, top: lib.top + 60, width: 64, height: 64 };
    const tx = t.left + t.width / 2 - 32, ty = t.top + t.height / 2 - 32;
    const f = document.createElement('div');
    f.className = 'flyer';
    f.style.cssText = `left:${r.left + r.width / 2 - 32}px;top:${r.top + r.height / 2 - 32}px;border-color:${RCOL[p.rk]}`;
    f.innerHTML = `<img src="${ICON[p.cat]}" alt="">`;
    $('fx').appendChild(f);
    setTimeout(() => { f.style.transform = `translate(${tx - r.left - r.width / 2 + 32}px,${ty - r.top - r.height / 2 + 32}px) scale(.5)`; f.style.opacity = '.2'; }, 60 + i * 120);
    setTimeout(() => f.remove(), 1100 + i * 120);
  });
  const fresh = got.filter(g => g.isNew).length;
  setTimeout(() => {
    render();
    const dl = $('libDelta');
    dl.hidden = false; dl.textContent = fresh ? `+${fresh} nouvelle${fresh > 1 ? 's' : ''}` : 'doublons';
    dl.style.animation = 'none'; void dl.offsetWidth; dl.style.animation = '';
    const [x, y] = center($('libOwned'));
    sparks(x, y, '#3e8ef7', 18);
    clearTimeout(flyToLibrary.t); flyToLibrary.t = setTimeout(() => { dl.hidden = true; }, 4000);
  }, 900);
}

// ---------------------------------------------------------------- overlays
let open = null;
function showOverlay(id) { open = $(id); open.hidden = false; open.querySelector('[data-close]').focus(); }
function closeOverlay() {
  if (!open) return;
  const id = open.id;
  if (id === 'packOv' && pack.got && !pack.collected) return collect('pack');
  if (id === 'chestOv' && chest.got && !chest.collected) return collect('chest');
  open.hidden = true; open = null;
  render();
}
document.querySelectorAll('[data-close]').forEach(b => b.addEventListener('click', closeOverlay));
document.addEventListener('keydown', e => { if (e.key === 'Escape' && open) closeOverlay(); });

function cardSlot(p, i, extra = '') {
  const tag = p.isNew ? (p.rk === 'x' ? 'Nouvelle pièce extrême' : 'Nouvelle pièce') : `Doublon · tu en as ${p.count}`;
  return `<div class="slot ${extra}" style="animation-delay:${0.25 + i * 0.15}s;--rot:${(i - 1) * 25}deg">
    <button type="button" class="flip" data-i="${i}" aria-label="Retourner la carte ${i + 1}">
      <span class="face back"><span class="foil"></span><img src="assets/hub/logo_plasma_ork.png" alt=""><span class="lab" style="position:relative;color:#ffb3d3">Touche pour retourner</span></span>
      <span class="face front ${p.rk}"><span class="rar">${RARITY[p.rk]}</span><img src="${ICON[p.cat]}" alt=""><b>${esc(p.name)}</b><small>${catSingular[p.cat]}</small><span class="tag">${tag}</span></span>
    </button></div>`;
}
function reveal(slot, p) {
  const btn = slot.querySelector('.flip');
  if (btn.classList.contains('on')) return false;
  btn.classList.add('on');
  btn.setAttribute('aria-label', `${RARITY[p.rk]} : ${p.name}`);
  if (p.rk !== 'c') {
    setTimeout(() => {
      const [x, y] = center(slot);
      sparks(x, y, RCOL[p.rk], p.rk === 'x' ? 40 : 20);
      const b = document.createElement('span'); b.className = 'burst'; b.style.setProperty('--bc', RCOL[p.rk]); slot.appendChild(b);
      setTimeout(() => b.remove(), 1000);
    }, 350);
  }
  return true;
}
function collect(kind) {
  const st = kind === 'pack' ? pack : chest;
  if (st.collected) return;
  st.collected = true;
  const ov = $(kind === 'pack' ? 'packOv' : 'chestOv');
  const rects = [...$(kind === 'pack' ? 'packCards' : 'chestCards').querySelectorAll('.slot')].map(e => e.getBoundingClientRect());
  ov.hidden = true; if (open === ov) open = null;
  flyToLibrary(rects, st.got);
}

// ---- pack : couper au doigt, puis retourner les cartes
const pack = { got: null, collected: false, cut: 0, drag: false };
function openPack() {
  if (!S.packs) return;
  Object.assign(pack, { got: null, collected: false, cut: 0, drag: false });
  $('bigPack').className = 'bigpack'; $('bigPack').hidden = false;
  $('blade').style.width = '0'; $('cutZone').setAttribute('aria-valuenow', '0');
  $('packHint').hidden = false; $('packCards').innerHTML = ''; $('packFoot').hidden = true;
  $('packStep').textContent = 'Étape 1 sur 2 · couper';
  showOverlay('packOv');
}
function setCut(v) {
  pack.cut = Math.max(0, Math.min(1, v));
  $('blade').style.width = Math.round(pack.cut * 300) + 'px';
  $('cutZone').setAttribute('aria-valuenow', String(Math.round(pack.cut * 100)));
  if (pack.cut >= 0.92) cutOpen();
}
function cutOpen() {
  if (pack.got) return;
  pack.drag = false;
  pack.got = draw('pack');
  commit('pack', pack.got);
  $('bigPack').className = 'bigpack open';
  $('packHint').hidden = true;
  const [x, y] = center($('cutZone'));
  sparks(x, y, '#ff2e88', 30);
  $('packStep').textContent = 'Étape 2 sur 2 · retourner les cartes';
  setTimeout(() => {
    $('bigPack').hidden = true;
    $('packCards').innerHTML = pack.got.map((p, i) => cardSlot(p, i)).join('');
  }, 750);
}
const cutX = e => { const r = $('cutZone').getBoundingClientRect(), k = r.width / 360; return (e.clientX - r.left - 30 * k) / (300 * k); };
$('cutZone').addEventListener('pointerdown', e => { pack.drag = true; $('cutZone').setPointerCapture(e.pointerId); $('bigPack').classList.add('dragging'); setCut(Math.max(0, cutX(e))); });
$('cutZone').addEventListener('pointermove', e => { if (pack.drag && !pack.got) setCut(Math.max(pack.cut, cutX(e))); });
const cutEnd = () => { if (!pack.got) { pack.drag = false; setCut(0); $('bigPack').classList.remove('dragging'); } };
$('cutZone').addEventListener('pointerup', cutEnd);
$('cutZone').addEventListener('pointercancel', cutEnd);
$('cutZone').addEventListener('keydown', e => {
  if (e.key === 'ArrowRight') { e.preventDefault(); setCut(pack.cut + 0.2); }
  else if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); cutOpen(); }
});
$('cutClick').addEventListener('click', cutOpen);
$('packCards').addEventListener('click', e => {
  const b = e.target.closest('.flip'); if (!b) return;
  const i = +b.dataset.i;
  if (!reveal(b.parentElement, pack.got[i])) return;
  if ($('packCards').querySelectorAll('.flip.on').length === 3) {
    $('packStep').textContent = 'Terminé';
    setTimeout(() => { $('packFoot').hidden = false; $('packCollect').focus(); }, 500);
  }
});
$('packCollect').addEventListener('click', () => collect('pack'));

// ---- coffre : trois coups, le couvercle saute, les pièces volent
const chest = { got: null, collected: false, hits: 0 };
function openChest() {
  if (!S.chests) return;
  Object.assign(chest, { got: null, collected: false, hits: 0 });
  $('chest').className = 'chest'; $('chest').hidden = false; $('beam').hidden = true;
  $('chestHint').hidden = false; $('chestCards').innerHTML = ''; $('chestFoot').hidden = true;
  updateChest();
  showOverlay('chestOv');
}
function updateChest() {
  const h = chest.hits, glow = [0, 14, 28, 60][Math.min(h, 3)];
  $('seam').style.boxShadow = `0 0 ${glow}px rgba(242,194,48,.9)`;
  $('lock').style.boxShadow = `0 0 ${12 + glow}px rgba(255,46,136,.7)`;
  $('chestPrompt').textContent = ['Frappe le coffre pour l’ouvrir', 'Encore !', 'Une dernière fois !', ''][h];
  $('chestDots').querySelectorAll('i').forEach((d, i) => d.classList.toggle('on', i < h));
  $('chest').setAttribute('aria-label', h < 3 ? `Frapper le coffre (${h} sur 3)` : 'Coffre ouvert');
}
$('chest').addEventListener('click', e => {
  if (chest.hits >= 3) return;
  chest.hits++;
  const c = $('chest');
  c.className = 'chest ' + (chest.hits % 2 ? 'hitA' : 'hitB');
  sparks(e.clientX || center(c)[0], e.clientY || center(c)[1], '#f2c230', 10 + chest.hits * 6);
  updateChest();
  if (chest.hits === 3) {
    chest.got = draw('chest');
    commit('chest', chest.got);
    setTimeout(() => {
      c.className = 'chest opened';
      $('beam').hidden = false;
      $('chestHint').hidden = true;
      const [x, y] = center(c);
      sparks(x, y - 60, '#f2c230', 44);
    }, 450);
    setTimeout(() => {
      c.hidden = true;
      $('chestCards').innerHTML = chest.got.map((p, i) => cardSlot(p, i, 'fly')).join('');
      const slots = [...$('chestCards').querySelectorAll('.slot')];
      slots.forEach((s, i) => setTimeout(() => reveal(s, chest.got[i]), 700 + i * 250));
      setTimeout(() => { $('chestFoot').hidden = false; $('chestCollect').focus(); }, 1700);
    }, 1300);
  }
});
$('chestCollect').addEventListener('click', () => collect('chest'));

// ---------------------------------------------------------------- boutique, recyclage, drop du jour, factions
document.querySelector('.shop').addEventListener('click', e => {
  const b = e.target.closest('[data-buy]'); if (!b) return;
  const what = b.dataset.buy;
  if (what === 'dents') {
    if (S.dents < RULES.packInDents) return;
    S.dents -= RULES.packInDents; S.packs++; save(); render();
    toast('Pack Boyz échangé contre ' + RULES.packInDents + ' dents');
  } else if (what === 'pack') { S.packs++; save(); render(); toast('Démo : pack ajouté. Le paiement sera branché sur le site.'); }
  else if (what === 'chest') { S.chests++; save(); render(); toast('Démo : coffre ajouté. Le paiement sera branché sur le site.'); }
  else toast('Abonnement : à brancher sur le site.');
  const [x, y] = center(b); sparks(x, y, '#f2c230', 12);
});
$('shopTop').addEventListener('click', () => { $('shop').scrollIntoView({ behavior: 'smooth', block: 'center' }); $('shop').animate([{ boxShadow: '0 0 0 0 rgba(255,46,136,.7)' }, { boxShadow: '0 0 0 14px rgba(255,46,136,0)' }], { duration: 900 }); });
$('recycle').addEventListener('click', () => {
  let gain = 0;
  for (const [id, c] of Object.entries(S.owned)) if (c > 1) { gain += (c - 1) * RULES.recycle[byId[id]?.rk || 'c']; S.owned[id] = 1; }
  if (!gain) return;
  S.dents += gain; save(); render();
  const [x, y] = center($('dents')); sparks(x, y, '#f2c230', 24);
  toast(`+${gain} dents grâce aux doublons`);
});
$('openPack').addEventListener('click', openPack);
$('openChest').addEventListener('click', openChest);
$('factions').addEventListener('click', e => { const b = e.target.closest('[data-faction]'); if (b) { S.faction = b.dataset.faction; save(); render(); } });

const DAY = 24 * 3600 * 1000;
function nextDrop() { if (!S.dropAt) return 0; const d = new Date(S.dropAt); d.setHours(24, 0, 0, 0); return d.getTime(); }
function tickDrop() {
  const next = nextDrop(), left = Math.max(0, next - Date.now());
  const ready = left === 0;
  $('dropBtn').disabled = !ready;
  $('dropBtn').closest('.hdrop').classList.toggle('ready', ready);
  if (ready) { $('dropTime').textContent = 'Prêt !'; $('dropProg').style.strokeDashoffset = '0'; return; }
  const h = Math.floor(left / 3600000), m = Math.floor(left / 60000) % 60, s = Math.floor(left / 1000) % 60;
  $('dropTime').textContent = `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  $('dropProg').style.strokeDashoffset = String(276.5 * (left / DAY));
}
$('dropBtn').addEventListener('click', () => {
  if (nextDrop() > Date.now()) return;
  const p = pick('c'), before = S.owned[p.id] || 0;
  S.owned[p.id] = before + 1; S.dropAt = Date.now();
  if (!before) S.got[p.id] = { at: Date.now(), src: 'drop' };
  save();
  const [x, y] = center($('dropBtn')); sparks(x, y, '#f2c230', 30);
  toast(`Drop du jour : ${p.name} (${before ? 'doublon' : 'nouvelle pièce'})`);
  render(); tickDrop();
});

render();
tickDrop();
setInterval(tickDrop, 1000);
// hub.html#pack / #chest (lien depuis la Plasma Library) : ouvre directement
if (location.hash === '#pack' && S.packs) openPack();
else if (location.hash === '#chest' && S.chests) openChest();
else if (location.hash === '#shop') setTimeout(() => $('shopTop').click(), 300);
if (location.hash) history.replaceState(null, '', location.pathname);
