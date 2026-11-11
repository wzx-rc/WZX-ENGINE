// Plasma Library (maquette C) : toutes les pièces, rangées par catégorie, avec filtres (état, rareté, recherche),
// compteur ×N par pièce et un panneau « pièce sélectionnée » (aperçu, provenance, couleurs de faction, poser sur
// l'ork, export STL, recyclage des doublons). Même contenu et même état que le Hub (js/loot.js).
import { $, esc, RULES, RARITY, RCOL, ICON, FACTIONS, NAMES, CATALOG, byId, KIT, catLabel, catSingular, S, save, ownedCount, dupes, sparks, toast, center, setNum, SOURCE } from './loot.js';

const ALL = { id: 'all', label: 'Toutes les pièces', short: 'Toutes' };
const ui = { cat: 'all', state: 'all', rar: new Set(), q: '', kit: false, sel: null };
const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
const fmtDate = t => new Date(t).toLocaleDateString(window.plasmaLang === 'fr' ? 'fr-FR' : 'en-GB', { day: 'numeric', month: 'long' });

// où trouver une pièce qui manque (texte provisoire, suit RULES)
const WHERE = {
  c: 'Une commune tombe dans chaque pack, chaque coffre et le drop du jour.',
  r: 'Une rare tombe dans chaque pack et chaque coffre.',
  x: tr(`Extrême garantie dans les coffres, ${Math.round(RULES.extremeChance * 100)} % de chance par pack (garantie au ${RULES.pity}e pack).`, `Extreme guaranteed in chests, ${Math.round(RULES.extremeChance * 100)}% chance per pack (guaranteed by pack ${RULES.pity}).`),
};

function list() {
  const q = ui.q.trim().toLowerCase();
  return CATALOG.filter(p => {
    const c = S.owned[p.id] || 0;
    if (ui.kit && !KIT.pieces.includes(p.id)) return false;
    if (ui.cat !== 'all' && p.cat !== ui.cat) return false;
    if (ui.rar.size && !ui.rar.has(p.rk)) return false;
    if (ui.state === 'own' && !c) return false;
    if (ui.state === 'miss' && c) return false;
    if (ui.state === 'dup' && c < 2) return false;
    if (q && !(c ? p.name : '').toLowerCase().includes(q) && !catSingular[p.cat].toLowerCase().includes(q)) return false;
    return true;
  });
}

// ---------------------------------------------------------------- colonne des catégories
function renderSide() {
  const rows = [ALL, ...Object.keys(NAMES).map(id => ({ id, label: catLabel(id), short: catLabel(id) }))];
  $('catList').innerHTML = rows.map(r => {
    const pool = r.id === 'all' ? CATALOG : CATALOG.filter(p => p.cat === r.id);
    const n = pool.filter(p => S.owned[p.id]).length;
    const ico = r.id === 'all'
      ? '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="3" width="8" height="8" rx="2"/><rect x="13" y="3" width="8" height="8" rx="2"/><rect x="3" y="13" width="8" height="8" rx="2"/><rect x="13" y="13" width="8" height="8" rx="2"/></svg>'
      : `<img src="${ICON[r.id]}" alt="">`;
    return `<button type="button" class="catbtn" data-cat="${r.id}" aria-pressed="${ui.cat === r.id}">${ico}<span class="cl">${r.short}</span><span class="cn">${n} / ${pool.length}</span><i style="--p:${(n / pool.length * 100).toFixed(1)}%"></i></button>`;
  }).join('');
  const packs = S.packs, chests = S.chests;
  $('sidePackTitle').textContent = packs ? tr(`${packs} pack${packs > 1 ? 's' : ''} à ouvrir`, `${packs} pack${packs > 1 ? 's' : ''} to open`) : chests ? tr(`${chests} coffre${chests > 1 ? 's' : ''} à ouvrir`, `${chests} chest${chests > 1 ? 's' : ''} to open`) : 'Plus rien à ouvrir';
  $('sidePackGo').textContent = packs || chests ? 'Ouvrir' : 'Boutique';
  $('sidePackGo').href = packs ? 'hub.html#pack' : chests ? 'hub.html#chest' : 'hub.html#shop';
  $('sidePack').classList.toggle('empty', !packs && !chests);
  const badge = packs + chests;
  $('navBadge').hidden = !badge; $('navBadge').textContent = badge;
  setNum('dents', S.dents);
}

// ---------------------------------------------------------------- en-tête, kit, filtres, grille
function renderMain(animate = true) {
  $('libTitle').textContent = ui.kit ? KIT.name : ui.cat === 'all' ? ALL.label : catLabel(ui.cat);
  const d = dupes();
  $('libSub').textContent = tr(`${ownedCount()} / ${CATALOG.length} débloquées · ${d} doublon${d > 1 ? 's' : ''}`, `${ownedCount()} / ${CATALOG.length} unlocked · ${d} duplicate${d > 1 ? 's' : ''}`);

  const kitOwn = KIT.pieces.filter(id => S.owned[id]).length;
  $('kitName').textContent = `${KIT.name} · ${kitOwn} / ${KIT.pieces.length}`;
  $('kitSegs').innerHTML = KIT.pieces.map(id => `<i class="${S.owned[id] ? 'on' : ''}"></i>`).join('');
  const miss = KIT.pieces.filter(id => !S.owned[id]).map(id => byId[id]);
  $('kitText').textContent = miss.length
    ? tr(`Il manque : ${miss.map(p => `${catSingular[p.cat].toLowerCase()} ${RARITY[p.rk].toLowerCase()}`).join(', ')}. Kit complet : ${KIT.reward}.`, `Missing: ${miss.map(p => `${RARITY[p.rk].toLowerCase()} ${catSingular[p.cat].toLowerCase()}`).join(', ')}. Kit complete: ${KIT.reward}.`)
    : tr(`Kit complet : ${KIT.reward} !`, `Kit complete: ${KIT.reward}!`);
  $('kitBtn').setAttribute('aria-pressed', String(ui.kit));
  $('kitBtn').textContent = ui.kit ? 'Toutes les pièces' : 'Voir les pièces du kit';

  $('fState').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(b.dataset.f === ui.state)));
  $('fRar').querySelectorAll('button').forEach(b => b.setAttribute('aria-pressed', String(ui.rar.has(b.dataset.r))));

  const items = list();
  if (!items.some(p => p.id === ui.sel)) ui.sel = items.find(p => S.owned[p.id])?.id || items[0]?.id || ui.sel;
  $('pgrid').classList.toggle('anim', animate && !reduced);
  $('pgrid').innerHTML = items.length ? items.map((p, i) => tile(p, i)).join('') : '<p class="pempty">Aucune pièce ne correspond à ces filtres.</p>';
}
function tile(p, i) {
  const c = S.owned[p.id] || 0, locked = !c;
  const sel = p.id === ui.sel, kit = KIT.pieces.includes(p.id);
  return `<button type="button" role="option" aria-selected="${sel}" class="pc ptile ${locked ? 'locked ' + p.rk + '-l' : p.rk}${sel ? ' sel' : ''}" data-id="${p.id}" style="--i:${Math.min(i, 30)}" aria-label="${locked ? `${tr(`${RARITY[p.rk]} ${catSingular[p.cat]} pas encore débloquée`, `${RARITY[p.rk]} ${catSingular[p.cat]}, not unlocked yet`)}` : `${esc(p.name)}, ${RARITY[p.rk]}, ×${c}`}">
    ${c ? `<span class="cnt">×${c}</span>` : ''}${kit ? '<span class="kitdot" title="Pièce du kit"></span>' : ''}
    <img src="${ICON[p.cat]}" alt=""><b>${locked ? '???' : esc(p.name)}</b><span class="rar">${RARITY[p.rk]}</span>
  </button>`;
}

// ---------------------------------------------------------------- pièce sélectionnée
function renderDetail(pop = false) {
  const p = byId[ui.sel];
  const box = $('detail');
  if (!p) { box.hidden = true; return; }
  box.hidden = false;
  const c = S.owned[p.id] || 0, locked = !c;
  const fac = FACTIONS.find(f => f.id === S.faction) || FACTIONS[0];
  box.className = `libdetail ${p.rk}${locked ? ' locked' : ''}`;
  box.style.setProperty('--fc', fac.col);
  $('dImg').src = ICON[p.cat];
  $('dTag').textContent = RARITY[p.rk].toUpperCase();
  $('dCnt').textContent = '×' + c; $('dCnt').hidden = locked;
  $('dName').textContent = locked ? 'Pièce à débloquer' : p.name;
  $('dMeta').textContent = locked ? `${catSingular[p.cat]} · ${RARITY[p.rk]}` : `${catSingular[p.cat]} · ${fac.label} · low + high`;
  const got = S.got[p.id];
  $('dGot').textContent = locked ? WHERE[p.rk] : got ? tr(`Obtenue le ${fmtDate(got.at)} · ${SOURCE[got.src] || got.src}`, `Obtained on ${fmtDate(got.at)} · ${SOURCE[got.src] || got.src}`) : tr(`Obtenue avec le ${SOURCE.start.toLowerCase()}`, `Obtained with the ${SOURCE.start.toLowerCase()}`);
  box.querySelector('.dfac').hidden = locked;
  $('dFac').innerHTML = FACTIONS.map(f => `<button type="button" role="radio" aria-checked="${f.id === fac.id}" aria-label="${f.label}" title="${f.label}" data-faction="${f.id}" style="--fc:${f.col}"><img src="${f.img}" alt=""></button>`).join('');
  const pose = $('dPose');
  if (locked) { pose.textContent = S.packs ? 'Ouvrir un pack' : S.chests ? 'Ouvrir un coffre' : 'Aller à la boutique'; pose.href = S.packs ? 'hub.html#pack' : S.chests ? 'hub.html#chest' : 'hub.html#shop'; }
  else { pose.textContent = 'Poser sur mon ork'; pose.href = `index.html#piece=${encodeURIComponent(p.id)}`; }
  $('dStl').disabled = locked;
  const extra = c - 1;
  $('dRecycle').hidden = extra < 1;
  if (extra > 0) $('dRecycle').textContent = tr(`Recycler ${extra} doublon${extra > 1 ? 's' : ''} · +${extra * RULES.recycle[p.rk]} dents`, `Recycle ${extra} duplicate${extra > 1 ? 's' : ''} · +${extra * RULES.recycle[p.rk]} teeth`);
  if (pop && !reduced) $('dPrev').animate([{ transform: 'scale(.94)', opacity: 0.4 }, { transform: 'scale(1.02)', opacity: 1, offset: 0.6 }, { transform: 'none' }], { duration: 380, easing: 'cubic-bezier(.2,.8,.3,1)' });
}

function renderAll(animate = true) { renderSide(); renderMain(animate); renderDetail(); }
function select(id, pop = true) {
  if (!byId[id]) return;
  ui.sel = id;
  $('pgrid').querySelectorAll('.ptile').forEach(t => { const on = t.dataset.id === id; t.classList.toggle('sel', on); t.setAttribute('aria-selected', String(on)); });
  renderDetail(pop);
}

// ---------------------------------------------------------------- interactions
$('catList').addEventListener('click', e => {
  const b = e.target.closest('[data-cat]'); if (!b) return;
  ui.cat = b.dataset.cat; ui.kit = false; renderSide(); renderMain(); renderDetail();
});
$('fState').addEventListener('click', e => { const b = e.target.closest('[data-f]'); if (b) { ui.state = b.dataset.f; renderMain(); renderDetail(); } });
$('fRar').addEventListener('click', e => {
  const b = e.target.closest('[data-r]'); if (!b) return;
  ui.rar.has(b.dataset.r) ? ui.rar.delete(b.dataset.r) : ui.rar.add(b.dataset.r);
  renderMain(); renderDetail();
});
$('q').addEventListener('input', e => { ui.q = e.target.value; renderMain(false); renderDetail(); });
$('kitBtn').addEventListener('click', () => { ui.kit = !ui.kit; if (ui.kit) { ui.cat = 'all'; ui.state = 'all'; ui.rar.clear(); } renderSide(); renderMain(); renderDetail(); });
$('pgrid').addEventListener('click', e => { const t = e.target.closest('.ptile'); if (t) select(t.dataset.id); });
$('pgrid').addEventListener('keydown', e => {
  const keys = { ArrowRight: 1, ArrowLeft: -1, ArrowDown: 'down', ArrowUp: 'up' };
  if (!(e.key in keys)) return;
  const tiles = [...$('pgrid').querySelectorAll('.ptile')];
  const i = tiles.indexOf(document.activeElement); if (i < 0) return;
  const cols = getComputedStyle($('pgrid')).gridTemplateColumns.split(' ').length;
  const step = keys[e.key] === 'down' ? cols : keys[e.key] === 'up' ? -cols : keys[e.key];
  const t = tiles[Math.max(0, Math.min(tiles.length - 1, i + step))];
  e.preventDefault(); t.focus(); select(t.dataset.id, false);
});
$('dFac').addEventListener('click', e => {
  const b = e.target.closest('[data-faction]'); if (!b) return;
  S.faction = b.dataset.faction; save(); renderDetail();
  const [x, y] = center($('dPrev')); sparks(x, y, FACTIONS.find(f => f.id === S.faction).col, 14);
});
$('dStl').addEventListener('click', () => toast('Export STL de la pièce : à brancher sur l’exporteur du builder.'));
$('dRecycle').addEventListener('click', () => {
  const p = byId[ui.sel], extra = (S.owned[p.id] || 0) - 1; if (extra < 1) return;
  const gain = extra * RULES.recycle[p.rk];
  S.owned[p.id] = 1; S.dents += gain; save();
  const [x, y] = center($('dRecycle')); sparks(x, y, '#f2c230', 24);
  toast(tr(`+${gain} dents : ${extra} « ${p.name} » recyclé${extra > 1 ? 's' : ''}`, `+${gain} teeth: ${extra} "${p.name}" recycled`));
  renderSide(); renderMain(false); renderDetail();
});
// un autre onglet (le Hub) a ouvert un pack : on se met à jour
addEventListener('storage', e => {
  if (e.key !== 'plasmaork.hub' || !e.newValue) return;
  try { Object.assign(S, JSON.parse(e.newValue)); renderAll(false); } catch { /* ignoré */ }
});

// library.html#cat=weapon ou #piece=back_10
const h = new URLSearchParams(location.hash.slice(1));
if (h.get('cat') && NAMES[h.get('cat')]) ui.cat = h.get('cat');
if (h.get('piece') && byId[h.get('piece')]) { ui.sel = h.get('piece'); ui.cat = byId[ui.sel].cat; }
renderAll();
