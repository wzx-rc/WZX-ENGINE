// Habillage commun aux 5 pages : menu de la barre du haut qui se resserre quand la place manque et, dans l'app
// Windows, la barre de titre maison (place réservée par early.js + CSS, boutons natifs de Windows par-dessus à droite).
// Barre du haut centrée : si elle déborde (fenêtre étroite, zoom Windows, police plus large), le menu passe
// en icônes, puis le logo et les boutons se resserrent.
const tb = document.querySelector('.topbar');
if (tb) {
  // Menu vraiment centré (même place sur toutes les pages) : il y a débordement si le logo ou les boutons de droite
  // touchent le menu, ou si la barre dépasse.
  const nav = tb.querySelector('.mainnav'), left = tb.querySelector('.brand'), right = tb.querySelector('.topbar-right');
  const over = () => {
    const n = nav.getBoundingClientRect();
    return tb.scrollWidth > tb.clientWidth + 1 || (left && left.getBoundingClientRect().right > n.left - 6) || (right && right.scrollWidth > right.clientWidth + 1) || (right && right.getBoundingClientRect().left < n.right + 6);
  };
  const fit = () => {
    tb.classList.remove('compact', 'tight');
    if (over()) tb.classList.add('compact');
    if (over()) tb.classList.add('tight');
  };
  new ResizeObserver(fit).observe(tb);
  document.fonts?.ready.then(fit);
}

if (window.plasmaDesktop) {
  document.documentElement.classList.add('desktop');
  const page = document.title.split('·')[0].trim();
  const bar = document.createElement('div');
  bar.className = 'wintitle';
  bar.innerHTML = `<svg viewBox="0 0 40 40" aria-hidden="true"><path fill="#f0435a" d="M20 4l4 6 7-4-1 8 7 1-5 6 4 6-8 0-2 8-6-5-6 5-2-8-8 0 4-6-5-6 7-1-1-8 7 4z"/><circle cx="15.5" cy="20" r="2.6" fill="#0b0c0e"/><circle cx="24.5" cy="20" r="2.6" fill="#0b0c0e"/><path d="M15 26h10l-2 3h-6z" fill="#0b0c0e"/></svg><b>Plasma Orks Builder</b>${page && !/plasma orks/i.test(page) ? `<span>${page.replace(/[<&]/g, '')}</span>` : ''}`;
  document.body.prepend(bar);
}
