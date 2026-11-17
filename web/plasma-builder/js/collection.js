// Collection STL : les figurines (fichiers STL à imprimer) que le joueur a débloquées, téléchargées et imprimées.
// Provisoire en attendant le site : la liste FIGS et les visuels viennent de la Plasma Library du PC
// (rendus sur fond blanc détourés dans assets/collection/), l'état est dans localStorage 'plasmaork.collection'.
const T = s => (globalThis.plasmaT ? globalThis.plasmaT(s) : s);
const KEY = 'plasmaork.collection';

// Les statuts suivent l'ordre de la vie d'une figurine : on la débloque, on télécharge ses STL, on l'imprime.
export const STEPS = ['locked', 'download', 'owned', 'printed'];
export const STATUS = {
  locked: { label: T('À débloquer'), short: T('À débloquer'), col: '#8b8e96' },
  download: { label: T('À télécharger'), short: T('À télécharger'), col: '#4caee4' },
  owned: { label: T('STL possédés'), short: T('Possédée'), col: '#7bc67e' },
  printed: { label: T('Imprimée'), short: T('Imprimée'), col: '#f2c230' },
};

const img = id => `assets/collection/${id}.webp`;
export const FIGS = [
  { id: 'boy_infantry_001', name: 'Boy Infantry 001', squad: 'First Squad', faction: 'rose', img: img('boy_infantry_001') },
  { id: 'boy_infantry_003', name: 'Boy Infantry 003', squad: 'First Squad', faction: 'rose', img: img('boy_infantry_003') },
  { id: 'lance', name: 'Lance', squad: 'First Squad', faction: 'rose', img: img('lance') },
  { id: 'grot_bomber', name: 'Grot Bomber', squad: 'Grots', faction: 'rose', img: img('grot_bomber') },
  { id: 'grot_medic', name: 'Grot Medic', squad: 'Grots', faction: 'rose', img: img('grot_medic') },
  { id: 'grot_pistol', name: 'Grot Pistol', squad: 'Grots', faction: 'rose', img: img('grot_pistol') },
  { id: 'fat_ork', name: 'Fat Ork', squad: 'Spéciaux', faction: 'rose', img: img('fat_ork') },
  { id: 'neon_boyz_01', name: 'Neon Boyz 01', squad: 'Neon Waaagh', faction: 'vert', img: img('neon_boyz_01') },
];
export const figById = Object.fromEntries(FIGS.map(f => [f.id, f]));

const DEMO = {
  boy_infantry_001: { st: 'printed', prints: 2 }, grot_bomber: { st: 'printed', prints: 1 },
  neon_boyz_01: { st: 'owned' }, grot_medic: { st: 'owned' },
  fat_ork: { st: 'download' },
};
function load() {
  let s = null;
  try { s = JSON.parse(localStorage.getItem(KEY)); } catch { /* stockage indisponible */ }
  if (!s || s.v !== 1) s = { v: 1, figs: structuredClone(DEMO) };
  return s;
}
export const C = load();
export const saveC = () => { try { localStorage.setItem(KEY, JSON.stringify(C)); } catch { /* stockage indisponible */ } };
export const statusOf = id => C.figs[id]?.st || 'locked';
export const printsOf = id => C.figs[id]?.prints || 0;
export function setStatus(id, st) {
  const cur = C.figs[id] || {};
  if (st === 'locked') delete C.figs[id];
  else C.figs[id] = { ...cur, st, prints: st === 'printed' ? Math.max(1, cur.prints || 0) : 0, at: Date.now() };
  saveC();
}
export function addPrint(id, d) {
  const n = printsOf(id) + d;
  if (n <= 0) setStatus(id, 'owned');
  else { C.figs[id] = { ...C.figs[id], st: 'printed', prints: n }; saveC(); }
}
export function counts() {
  const n = { all: FIGS.length, locked: 0, download: 0, owned: 0, printed: 0 };
  for (const f of FIGS) n[statusOf(f.id)]++;
  return n;
}
