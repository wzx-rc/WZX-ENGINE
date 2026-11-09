// Interface « Atelier » (réglage Interface, enregistré) : la maquette UI/UX du 4/11 sur le vrai builder.
// La vue d'abord : liste Équipement (sockets de l'ork, icônes Recraft) à gauche, bibliothèque en tiroir filtrée par
// le socket choisi, étapes en haut de la vue, outils / affichage / qualité dans une barre flottante sur la vue,
// pastilles numérotées. Les mêmes éléments que l'interface Classique sont déplacés (leurs événements restent),
// puis remis à leur place quand on revient au Classique.
const $ = id => document.getElementById(id);
const KEY = 'plasmaork.uiTheme';

export const THEMES = ['classic', 'atelier'];
export function savedTheme() {
  try { const t = localStorage.getItem(KEY); return THEMES.includes(t) ? t : 'classic'; } catch { return 'classic'; }
}

// [élément, nouveau parent] : déplacé dans l'Atelier, remis avant son repère (commentaire) au Classique.
const MOVES = () => [
  [$('steps'), $('vpSteps')],
  [$('tools'), $('floatbar')],
  [$('shading'), $('floatbar')],
  [$('qualityBox'), $('floatbar')],
];
const marks = new Map();

export function applyTheme(theme) {
  const atelier = theme === 'atelier';
  try { localStorage.setItem(KEY, theme); } catch { /* stockage indisponible */ }
  document.documentElement.classList.toggle('ui-atelier', atelier);
  $('equip').hidden = !atelier;
  $('floatbar').hidden = !atelier;
  for (const [el, to] of MOVES()) {
    if (!el || !to) continue;
    if (!marks.has(el)) { const m = document.createComment('place:' + el.id); el.before(m); marks.set(el, m); }
    if (atelier) to.append(el); else marks.get(el).after(el);
  }
  document.querySelectorAll('#themeSel [data-theme]').forEach(b => b.setAttribute('aria-checked', String(b.dataset.theme === theme)));
}

// Icônes Recraft par type de socket.
export const SOCKET_ICONS = {
  head: 'assets/ui/icon_head.png', grip: 'assets/ui/icon_weapon.png', back: 'assets/ui/icon_back.png',
  shoulder: 'assets/ui/icon_shoulder.png', chest: 'assets/ui/icon_chest.png', belt: 'assets/ui/icon_belt.png',
  base: 'assets/ui/icon_base.png', any: 'assets/ui/icon_pose.png',
};
