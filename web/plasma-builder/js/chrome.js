// Habillage commun aux 5 pages : pastille du Hub (packs + coffres à ouvrir) et, dans l'app Windows, la barre de
// titre maison (la barre native est masquée par main.js ; les boutons réduire / agrandir / fermer de Windows sont
// dessinés par-dessus, à droite, aux couleurs de l'app).
import { S } from './loot.js';

const badge = document.getElementById('navBadge');
if (badge && !/hub|library/.test(document.body.className)) {
  const n = (S.packs || 0) + (S.chests || 0);
  badge.hidden = !n; badge.textContent = n;
}

if (window.plasmaDesktop) {
  document.documentElement.classList.add('desktop');
  const page = document.title.split('·')[0].trim();
  const bar = document.createElement('div');
  bar.className = 'wintitle';
  bar.innerHTML = `<svg viewBox="0 0 40 40" aria-hidden="true"><path fill="#f0435a" d="M20 4l4 6 7-4-1 8 7 1-5 6 4 6-8 0-2 8-6-5-6 5-2-8-8 0 4-6-5-6 7-1-1-8 7 4z"/><circle cx="15.5" cy="20" r="2.6" fill="#0b0c0e"/><circle cx="24.5" cy="20" r="2.6" fill="#0b0c0e"/><path d="M15 26h10l-2 3h-6z" fill="#0b0c0e"/></svg><b>Plasma Orks Builder</b>${page && !/plasma orks/i.test(page) ? `<span>${page.replace(/[<&]/g, '')}</span>` : ''}`;
  document.body.prepend(bar);
}
