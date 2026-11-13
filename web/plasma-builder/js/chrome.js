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
  window.plasmaFitTopbar = fit;
  new ResizeObserver(fit).observe(tb);
  document.fonts?.ready.then(fit);
}

// Icônes des boutons de fenêtre (Recraft, redessinées en 24 px ; copies dans assets/ui/wincontrols/).
const WIN_ICONS = {
  min: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M7.2 12.2 L12 11.8 L16.8 12.1"/><circle cx="4.6" cy="12" r="1.4" fill="currentColor" stroke="none"/><circle cx="19.4" cy="12" r="1.4" fill="currentColor" stroke="none"/></svg>',
  max: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M7.1 5.2 L17.2 4.9 Q19.2 5 19.1 7 L19.3 17.1 Q19.1 19.1 17 19 L6.9 19.3 Q4.9 19.1 5 17 L4.8 6.9 Q5 5.1 7.1 5.2Z"/><g fill="currentColor" stroke="none"><circle cx="8" cy="8" r="0.9"/><circle cx="16" cy="8" r="0.9"/><circle cx="8" cy="16" r="0.9"/><circle cx="16" cy="16" r="0.9"/></g></svg>',
  restore: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M8.6 7.8 L8.7 5.9 Q8.9 4.2 10.6 4.3 L18.1 4.1 Q19.9 4.2 19.8 6 L20 13.4 Q19.9 15.2 18.1 15.3 L16.2 15.4"/><path d="M5.9 8.6 L14.3 8.3 Q16.1 8.4 16 10.2 L16.2 18.2 Q16.1 20 14.3 19.9 L6 20.1 Q4.2 20 4.3 18.2 L4.1 10.3 Q4.2 8.5 5.9 8.6Z"/><g fill="currentColor" stroke="none"><circle cx="7.4" cy="11.6" r="0.85"/><circle cx="12.9" cy="16.9" r="0.85"/></g></svg>',
  close: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6.2 5.6 Q10.4 9.4 13.4 13.3 Q15.6 16.2 17.6 18.8"/><path d="M17.8 5.6 Q13.6 9.4 10.6 13.3 Q8.4 16.2 6.4 18.8"/><path d="M4.9 17.4 L7.9 20.1 M19.1 17.4 L16.1 20.1" stroke-width="1.6"/></svg>',
};

if (window.plasmaDesktop) {
  document.documentElement.classList.add('desktop');
  const page = document.title.split('·')[0].trim();
  const bar = document.createElement('div');
  bar.className = 'wintitle';
  bar.innerHTML = `<img src="assets/brand/plasma-orks-icon-32.png" alt=""><b>Plasma Orks Builder</b>${page && !/plasma orks/i.test(page) ? `<span>${page.replace(/[<&]/g, '')}</span>` : ''}`;
  const w = window.plasmaDesktop.win;
  if (w) {
    const ctl = document.createElement('div');
    ctl.className = 'wctl';
    ctl.innerHTML = `<button type="button" class="wbtn" data-w="min" aria-label="Réduire" title="Réduire">${WIN_ICONS.min}</button><button type="button" class="wbtn" data-w="max" aria-label="Agrandir" title="Agrandir">${WIN_ICONS.max}</button><button type="button" class="wbtn close" data-w="close" aria-label="Fermer" title="Fermer">${WIN_ICONS.close}</button>`;
    ctl.addEventListener('click', e => { const b = e.target.closest('[data-w]'); if (b) w[b.dataset.w](); });
    bar.append(ctl);
    const mx = ctl.querySelector('[data-w="max"]');
    w.onState(m => { mx.innerHTML = m ? WIN_ICONS.restore : WIN_ICONS.max; mx.title = mx.ariaLabel = m ? tr('Restaurer', 'Restore') : tr('Agrandir', 'Maximize'); });
  }
  document.body.prepend(bar);
}

// ---------------------------------------------------------------- Réglages (bouton engrenage à côté du logo)
// Mode : Complet (tout) ou Simple (Builder et Importer seulement : Hub, Library et Rig Lab sont cachés).
// Interface du Builder : Classique ou Atelier (appliquée par main.js sur la page Builder).
const MODE_KEY = 'plasmaork.uiMode', THEME_KEY = 'plasmaork.uiTheme';
const read = (k, d) => { try { return localStorage.getItem(k) || d; } catch { return d; } };
const write = (k, v) => { try { localStorage.setItem(k, v); } catch { /* stockage indisponible */ } };
const gbtn = document.getElementById('gsetBtn');
if (gbtn) {
  const panel = document.createElement('div');
  panel.id = 'gset'; panel.className = 'gset'; panel.hidden = true;
  panel.setAttribute('role', 'dialog'); panel.setAttribute('aria-label', 'Réglages');
  panel.innerHTML = `
    <div class="gset-head"><b>Réglages</b><button type="button" class="icon small" data-gclose aria-label="Fermer"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18"/></svg></button></div>
    <section><h3>Mode</h3>
      <div class="seg" role="radiogroup" aria-label="Mode"><button type="button" role="radio" data-gmode="full">Complet</button><button type="button" role="radio" data-gmode="simple">Simple</button></div>
      <p class="muted">Simple : seulement le Builder et l'Importer. Le Hub, la Library et le Rig Lab sont cachés.</p></section>
    <section><h3>Interface du Builder</h3>
      <div class="seg" role="radiogroup" aria-label="Interface" id="themeSel"><button type="button" role="radio" data-theme="classic">Classique</button><button type="button" role="radio" data-theme="atelier">Atelier</button></div>
      <p class="muted">Classique : l'interface d'origine. Atelier : la vue d'abord, l'équipement à gauche, la bibliothèque en tiroir.</p></section>
    <section><h3>Faction</h3>
      <div class="facsel" role="radiogroup" aria-label="Faction">${(window.plasmaFactions || []).map(f => `<button type="button" role="radio" data-gfac="${f.id}" style="--fc:${f.col}"><i></i><span>${f.name}</span></button>`).join('')}</div>
      <p class="muted">La couleur de la faction devient l'accent de toute l'app.</p></section>
    <section><h3>Langue</h3>
      <div class="seg" role="radiogroup" aria-label="Langue"><button type="button" role="radio" data-glang="en" lang="en">English</button><button type="button" role="radio" data-glang="fr" lang="fr">Français</button></div>
      <p class="muted">La page se recharge pour changer de langue.</p></section>
    <p class="muted gset-foot">Choix enregistrés sur cet appareil.</p>`;
  gbtn.after(panel);
  const sync = () => {
    const mode = read(MODE_KEY, 'full'), theme = read(THEME_KEY, 'classic');
    panel.querySelectorAll('[data-gmode]').forEach(b => b.setAttribute('aria-checked', String(b.dataset.gmode === mode)));
    panel.querySelectorAll('[data-theme]').forEach(b => b.setAttribute('aria-checked', String(b.dataset.theme === theme)));
    panel.querySelectorAll('[data-gfac]').forEach(b => b.setAttribute('aria-checked', String(b.dataset.gfac === window.plasmaFaction?.())));
    panel.querySelectorAll('[data-glang]').forEach(b => b.setAttribute('aria-checked', String(b.dataset.glang === (window.plasmaLang || 'en'))));
  };
  const open = v => { panel.hidden = !v; gbtn.setAttribute('aria-expanded', String(v)); if (v) sync(); };
  gbtn.addEventListener('click', () => open(panel.hidden));
  panel.addEventListener('click', e => {
    const b = e.target.closest('button'); if (!b) return;
    if (b.hasAttribute('data-gclose')) open(false);
    else if (b.dataset.gfac) window.plasmaSetFaction?.(b.dataset.gfac);
    else if (b.dataset.glang) { if (b.dataset.glang !== window.plasmaLang) window.plasmaSetLang?.(b.dataset.glang); }
    else if (b.dataset.gmode) {
      write(MODE_KEY, b.dataset.gmode);
      document.documentElement.classList.toggle('mode-simple', b.dataset.gmode === 'simple');
      window.plasmaFitTopbar?.();
    } else if (b.dataset.theme) write(THEME_KEY, b.dataset.theme);   // le Builder l'applique (main.js)
    sync();
  });
  document.addEventListener('click', e => { if (!panel.hidden && !e.target.closest('#gset, #gsetBtn')) open(false); });
  document.addEventListener('keydown', e => { if (e.key === 'Escape' && !panel.hidden) { open(false); gbtn.focus(); } });
}
