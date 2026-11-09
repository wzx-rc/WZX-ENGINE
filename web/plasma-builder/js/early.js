// Chargé (sans module, bloquant) juste après le menu de la barre du haut, avant le reste de la page : tout ce qui
// change la mise en page est posé avant le premier affichage, pour que rien ne saute pendant le chargement ni
// d'une page à l'autre (interface Atelier, mode Simple, barre de titre de l'app Windows, pastille du Hub).
(function () {
  var html = document.documentElement;
  try { if (localStorage.getItem('plasmaork.uiTheme') === 'atelier' && /(index\.html|\/)$/.test(location.pathname)) html.classList.add('ui-atelier'); } catch (e) { /* stockage indisponible */ }
  try { if (localStorage.getItem('plasmaork.uiMode') === 'simple') html.classList.add('mode-simple'); } catch (e) { /* idem */ }
  if (window.plasmaDesktop) html.classList.add('desktop');
  var b = document.getElementById('navBadge');
  if (b) {
    var n = 4; // 3 packs + 1 coffre au départ (RULES.start dans loot.js)
    try { var s = JSON.parse(localStorage.getItem('plasmaork.hub')); if (s && s.v === 1) n = (s.packs || 0) + (s.chests || 0); } catch (e) { /* idem */ }
    b.hidden = !n; b.textContent = n;
  }
})();
