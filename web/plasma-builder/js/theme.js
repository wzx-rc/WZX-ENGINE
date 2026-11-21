// Thème de faction (Réglages > Faction) : couleur d'accent de toute l'app. Chargé sans module et bloquant dans le
// <head> des 5 pages, pour que la couleur soit posée avant le premier affichage (pas de flash rose).
// Les ids sont ceux des factions du Hub (loot.js) : la faction du joueur et le thème sont le même choix.
(function () {
  var FACTIONS = [
    { id: 'rose', name: 'Cyber-Boys', col: '#ff2e88' },
    { id: 'bleu', name: 'Plasma Tek', col: '#4caee4' },
    { id: 'vert', name: 'Neon', col: '#b1ff41' },
    { id: 'jaune', name: 'Rampage', col: '#fb7e3a' },
  ];
  var KEY = 'plasmaork.faction';
  var cur = 'rose';
  try {
    var v = localStorage.getItem(KEY);
    if (!v) { var h = JSON.parse(localStorage.getItem('plasmaork.hub')); if (h && h.faction) v = h.faction; }   // choix fait avant les thèmes
    if (FACTIONS.some(function (f) { return f.id === v; })) cur = v;
  } catch (e) { /* stockage indisponible */ }
  document.documentElement.dataset.faction = cur;
  window.plasmaFactions = FACTIONS;
  window.plasmaFaction = function () { return document.documentElement.dataset.faction; };
  // Change le thème tout de suite (sans recharger) et le garde ; prévient la page (hub, library) par un événement.
  window.plasmaSetFaction = function (id) {
    if (!FACTIONS.some(function (f) { return f.id === id; })) return;
    document.documentElement.dataset.faction = id;
    try { localStorage.setItem(KEY, id); } catch (e) { /* idem */ }
    window.dispatchEvent(new CustomEvent('plasma-faction', { detail: id }));
  };
})();
