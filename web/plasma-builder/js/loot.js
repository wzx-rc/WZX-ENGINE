// Contenu et état partagés par le Hub (hub.html) et la Plasma Library (library.html).
// Tout est provisoire en attendant le vrai contenu et le site :
// - CATALOGUE : pièces factices (nom + picto de catégorie). À remplacer par les briques de la bibliothèque.
// - État du joueur : localStorage 'plasmaork.hub' (à remplacer par le compte en ligne).
import { CATEGORIES } from './categories.js';

export const $ = id => document.getElementById(id);
export const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
export const KEY = 'plasmaork.hub';
// Textes affichés : traduits par i18n.js quand l'interface est en anglais (les ids et l'état restent identiques).
export const T = s => (globalThis.plasmaT ? globalThis.plasmaT(s) : s);

// ---------------------------------------------------------------- réglages (à équilibrer)
export const RULES = {
  extremeChance: 0.25,   // 3e carte d'un pack : extrême avec cette probabilité, sinon rare
  pity: 5,               // extrême garantie au plus tard au 5e pack sans extrême
  recycle: { c: 10, r: 30, x: 100 },   // dents par doublon recyclé
  packInDents: 300,
  start: { dents: 120, packs: 3, chests: 1 },
};
export const RARITY = { c: T('Commune'), r: T('Rare'), x: T('Extrême') };
export const RCOL = { c: '#b9c0c8', r: '#3e8ef7', x: '#ff2e88' };
export const ICON = { head: 'assets/ui/icon_head.png', weapon: 'assets/ui/icon_weapon.png', back: 'assets/ui/icon_back.png', shoulder: 'assets/ui/icon_shoulder.png', chest: 'assets/ui/icon_chest.png', belt: 'assets/ui/icon_belt.png', base: 'assets/ui/icon_base.png' };
export const FACTIONS = [
  { id: 'rose', label: T('Klan rose'), img: 'assets/hub/faction_rose.png', col: '#ff2e88' },
  { id: 'bleu', label: T('Klan bleu'), img: 'assets/hub/faction_bleu.png', col: '#3e8ef7' },
  { id: 'jaune', label: T('Klan jaune'), img: 'assets/hub/faction_jaune.png', col: '#f2c230' },
  { id: 'vert', label: T('Klan vert'), img: 'assets/hub/faction_vert.png', col: '#6bbf3a' },
];

// ---------------------------------------------------------------- catalogue provisoire
export const NAMES = {
  head: [['Kasque cabossé', 'c'], ['Mâchoire basique', 'c'], ['Kasque à cornes', 'c'], ['Bandana du klan', 'c'], ['Tête balafrée', 'c'], ['Kasque riveté', 'c'], ['Kasque de Nob', 'r'], ['Lunettes de Mek', 'r'], ['Krâne bioniK', 'r'], ['Kasque à pointes', 'r'], ['Mâchoire d’acier', 'x'], ['Kouronne de Warboss', 'x']],
  weapon: [['Choppa rouillée', 'c'], ['Slugga', 'c'], ['Kosto', 'c'], ['Kikoup’', 'c'], ['Slugga double', 'c'], ['Matraque', 'c'], ['Kustom Shoota', 'r'], ['Big Choppa', 'r'], ['Pince énergétique', 'r'], ['Lance-rokkit', 'r'], ['Big Shoota plasma', 'x'], ['Kustom Méga-Blasta', 'x']],
  back: [['Sac à butin', 'c'], ['Bidon de fuel', 'c'], ['Fanion', 'c'], ['Tuyaux', 'c'], ['Réservoir', 'c'], ['Pot d’échappement', 'c'], ['Rokkit Pack', 'r'], ['Générateur', 'r'], ['Bannière du klan', 'r'], ['Antenne', 'r'], ['Bannière de Warboss', 'x'], ['Rokkit Pack plasma', 'x']],
  shoulder: [['Épaulière simple', 'c'], ['Épaulière à pointes', 'c'], ['Plaque rivetée', 'c'], ['Épaulière en pneu', 'c'], ['Épaulière à crâne', 'c'], ['Épaulière dentée', 'c'], ['Épaulière gauche Rokkit', 'r'], ['Épaulière de Nob', 'r'], ['Épaulière à chaîne', 'r'], ['Épaulière à cornes', 'r'], ['Épaulière Warboss', 'x'], ['Épaulière plasma', 'x']],
  chest: [['Plastron riveté', 'c'], ['Gilet en cuir', 'c'], ['Plaques de fer', 'c'], ['Bandoulière', 'c'], ['Harnais', 'c'], ['Torse nu', 'c'], ['Torse de Mek', 'r'], ['Cuirasse de Nob', 'r'], ['Plastron à crânes', 'r'], ['Torse rokkit', 'r'], ['Méga-armure', 'x'], ['Cœur de plasma', 'x']],
  belt: [['Bourse à dents', 'c'], ['Grenades stikk', 'c'], ['Ceinture simple', 'c'], ['Cartouchière', 'c'], ['Pochette', 'c'], ['Chaîne', 'c'], ['Ceinture à crânes', 'r'], ['Trophées', 'r'], ['Boucle du klan', 'r'], ['Ceinture de Mek', 'r'], ['Ceinture de Warboss', 'x'], ['Bombe plasma', 'x']],
  base: [['Socle rond', 'c'], ['Socle en terre', 'c'], ['Socle rocheux', 'c'], ['Socle de ferraille', 'c'], ['Socle en ruines', 'c'], ['Socle de rouille', 'c'], ['Socle de tranchée', 'r'], ['Socle d’épave', 'r'], ['Socle de crânes', 'r'], ['Socle de lave', 'r'], ['Socle plasma', 'x'], ['Trône de Warboss', 'x']],
};
export const CATALOG = Object.entries(NAMES).flatMap(([cat, list]) => list.map(([name, rk], i) => ({ id: cat + '_' + i, cat, name: T(name), rk })));
export const byId = Object.fromEntries(CATALOG.map(p => [p.id, p]));
export const KIT = { name: T('Kit Rokkit'), pieces: ['back_6', 'shoulder_6', 'chest_9', 'weapon_9', 'belt_1'], reward: T('skin plasma offert') };
export const catLabel = id => CATEGORIES.find(c => c.id === id)?.label || id;
export const catSingular = Object.fromEntries(Object.entries({ head: 'Tête', weapon: 'Arme', back: 'Dos', shoulder: 'Épaulière', chest: 'Torse', belt: 'Ceinture', base: 'Socle' }).map(([k, v]) => [k, T(v)]));

// ---------------------------------------------------------------- état
export function fresh() {
  const owned = {};
  ['head_0', 'weapon_0', 'weapon_1', 'back_0', 'shoulder_1', 'chest_0', 'belt_0', 'belt_1', 'base_0', 'weapon_6', 'back_6', 'chest_9'].forEach(id => { owned[id] = 1; });
  owned.belt_0 = 3; owned.chest_0 = 2;
  return { v: 1, ...RULES.start, owned, last: [], lastAt: 0, dropAt: 0, faction: 'rose', sinceX: 0 };
}
function load() {
  let s = null;
  try { s = JSON.parse(localStorage.getItem(KEY)); } catch { /* stockage indisponible */ }
  if (!s || s.v !== 1) s = fresh();
  s.got ||= {};   // id -> { at, src } : quand et comment la pièce a été obtenue
  return s;
}
export const S = load();
export const save = () => { try { localStorage.setItem(KEY, JSON.stringify(S)); } catch { /* stockage indisponible */ } };
export const ownedCount = () => Object.keys(S.owned).filter(k => S.owned[k] > 0).length;
export const dupes = () => Object.entries(S.owned).reduce((n, [, c]) => n + Math.max(0, c - 1), 0);

// ---------------------------------------------------------------- tirage
export function pick(rk) {
  const pool = CATALOG.filter(p => p.rk === rk);
  // les pièces manquantes ont deux fois plus de chances de tomber
  const w = pool.map(p => (S.owned[p.id] ? 1 : 2));
  let t = Math.random() * w.reduce((a, b) => a + b, 0);
  for (let i = 0; i < pool.length; i++) { t -= w[i]; if (t <= 0) return pool[i]; }
  return pool[pool.length - 1];
}
export const SOURCE = { pack: T('Pack Boyz'), chest: T('Coffre de Nob'), drop: T('Drop du jour'), start: T('Kit de départ') };
export function draw(kind) {
  let third = 'r';
  if (kind === 'chest') third = 'x';
  else if (Math.random() < RULES.extremeChance || S.sinceX + 1 >= RULES.pity) third = 'x';
  S.sinceX = third === 'x' ? 0 : S.sinceX + 1;
  const got = [pick('c'), pick('r'), pick(third)];
  return got.map(p => {
    const before = S.owned[p.id] || 0;
    S.owned[p.id] = before + 1;
    if (!before) S.got[p.id] = { at: Date.now(), src: kind };
    return { ...p, isNew: before === 0, count: before + 1 };
  });
}
export function commit(kind, got) {
  if (kind === 'pack') S.packs--; else S.chests--;
  S.last = got.map(g => g.id); S.lastAt = Date.now();
  save();
}

// ---------------------------------------------------------------- petites animations
export function tween(el, from, to, ms = 900) {
  if (!el) return;
  const t0 = performance.now(), fmt = n => Math.round(n).toLocaleString(globalThis.plasmaLang === 'fr' ? 'fr-FR' : 'en-US');
  if (from === to || matchMedia('(prefers-reduced-motion: reduce)').matches) { el.textContent = fmt(to); return; }
  const step = now => {
    const k = Math.min(1, (now - t0) / ms), e = 1 - Math.pow(1 - k, 3);
    el.textContent = fmt(from + (to - from) * e);
    if (k < 1) requestAnimationFrame(step);
  };
  requestAnimationFrame(step);
}
const shown = {};   // dernières valeurs affichées, pour animer les compteurs
export function setNum(id, v) { tween($(id), shown[id] ?? 0, v); shown[id] = v; }
export function sparks(x, y, color, n = 26) {
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
  for (let i = 0; i < n; i++) {
    const s = document.createElement('i');
    s.className = 'spark';
    const a = Math.random() * Math.PI * 2, d = 80 + Math.random() * 160;
    s.style.cssText = `left:${x}px;top:${y}px;background:${i % 3 ? color : '#fff4d6'};--dx:${Math.cos(a) * d}px;--dy:${Math.sin(a) * d}px;--dr:${Math.random() * 360}deg`;
    $('fx').appendChild(s);
    setTimeout(() => s.remove(), 1000);
  }
}
export function toast(msg) {
  const t = $('toast'); t.textContent = msg; t.classList.add('show');
  clearTimeout(toast.t); toast.t = setTimeout(() => t.classList.remove('show'), 2800);
}
export const center = el => { const r = el.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; };
