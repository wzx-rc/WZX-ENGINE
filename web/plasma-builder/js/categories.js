// Catégories de briques (sans dépendance : partagé par le builder, l'importeur et le hub).
// Libellés traduits à l'affichage par i18n.js (plasmaT) quand l'interface est en anglais.
const T = s => (globalThis.plasmaT ? globalThis.plasmaT(s) : s);
export const CATEGORIES = [
  { id: 'head', label: T('Têtes'), type: 'head', home: 'head', icon: 'M12 3a8 8 0 0 0-8 8v3l2 2v4h12v-4l2-2v-3a8 8 0 0 0-8-8zM9 12h.01M15 12h.01M10 20v-3M14 20v-3' },
  { id: 'weapon', label: T('Armes'), type: 'grip', home: 'grip_R', icon: 'M4 20l6-6M8 14l2 2M14 4l6 6-8 8-6-6z' },
  { id: 'back', label: T('Dos'), type: 'back', home: 'back', icon: 'M7 4h10v16H7zM7 9h10M10 4V2h4v2' },
  { id: 'shoulder', label: T('Épaulières'), type: 'shoulder', home: 'shoulder_L', icon: 'M4 14c0-5 4-9 9-9h3l4 4v3c0 5-4 8-9 8H8z' },
  { id: 'chest', label: T('Torse'), type: 'chest', home: 'chest', icon: 'M12 3l8 3v6c0 5-4 8-8 9-4-1-8-4-8-9V6z' },
  { id: 'belt', label: T('Ceinture'), type: 'belt', home: 'belt_R', icon: 'M6 8h12v12H6zM9 8V5h6v3M6 12h12' },
  { id: 'base', label: T('Socles'), type: 'base', home: 'base', icon: 'M3 17l9 4 9-4M3 17l9-4 9 4M3 17v-3l9-4 9 4v3' },
];
