// Menu du clic droit sur une vignette de brique (bibliothèque de l'Importeur et du Builder) : renommer, supprimer.
// Pas de prompt() / confirm() (absents de l'app Windows) : le champ et la confirmation sont dans le menu.
import { Store } from './bricks.js';

const esc = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
let menu = null;

function close() {
  if (!menu) return;
  menu.remove();
  menu = null;
  removeEventListener('pointerdown', outside, true);
  removeEventListener('keydown', onKey, true);
  removeEventListener('blur', close);
  removeEventListener('resize', close);
}
const outside = e => { if (menu && !menu.contains(e.target)) close(); };
const onKey = e => { if (e.key === 'Escape') { e.stopPropagation(); close(); } };

// grid : conteneur des vignettes [data-brick]. get(id) -> fiche ; changed(kind, rec) après l'enregistrement.
// edit(rec) facultatif : entrée « Modifier » (rouvre la brique dans l'Importeur).
export function brickMenu(grid, { get, changed, toast, edit }) {
  grid.addEventListener('contextmenu', e => {
    const tile = e.target.closest('[data-brick]');
    if (!tile) return;
    e.preventDefault();
    const rec = get(tile.dataset.brick);
    if (rec) open(e.clientX, e.clientY, rec, { changed, toast, edit });
  });
}

function open(x, y, rec, { changed, toast, edit }) {
  close();
  menu = document.createElement('div');
  menu.className = 'ctxmenu';
  menu.setAttribute('role', 'menu');
  document.body.append(menu);
  const place = () => {
    const r = menu.getBoundingClientRect();
    menu.style.left = Math.max(8, Math.min(x, innerWidth - r.width - 8)) + 'px';
    menu.style.top = Math.max(8, Math.min(y, innerHeight - r.height - 8)) + 'px';
  };
  const main = () => {
    menu.innerHTML = `<div class="ctx-title" data-noi18n>${esc(rec.name)}</div>
      ${edit ? '<button type="button" role="menuitem" data-act="edit"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 20h9M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/></svg>Modifier</button>' : ''}
      <button type="button" role="menuitem" data-act="rename"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20h4L19 9l-4-4L4 16zM14 6l4 4"/></svg>Renommer</button>
      <button type="button" role="menuitem" data-act="delete" class="danger"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/></svg>Supprimer</button>`;
    place();
    menu.querySelector('button').focus();
  };
  const rename = () => {
    menu.innerHTML = `<div class="ctx-title">Renommer</div><input type="text" spellcheck="false" aria-label="Nouveau nom" value="${esc(rec.name)}">
      <div class="ctx-row"><button type="button" data-act="cancel">Annuler</button><button type="button" data-act="save" class="go-btn">Renommer</button></div>`;
    place();
    const inp = menu.querySelector('input');
    inp.focus(); inp.select();
    inp.addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); save(); } });
  };
  const save = async () => {
    const name = menu.querySelector('input').value.trim();
    close();
    if (!name || name === rec.name) return;
    const old = rec.name;
    try {
      const r = { ...rec, name, updated: Date.now() };
      await Store.put(r);
      toast(tr(`« ${old} » renommée en « ${name} »`, `"${old}" renamed to "${name}"`));
      changed('rename', r);
    } catch (err) { console.error(err); toast(tr('Impossible de renommer : ', 'Couldn\'t rename: ') + (err.message || err)); }
  };
  const confirmDelete = () => {
    menu.innerHTML = `<div class="ctx-title">${tr(`Supprimer « ${esc(rec.name)} » ?`, `Delete "${esc(rec.name)}"?`)}</div><p class="ctx-note">Elle disparaît de la bibliothèque et des figurines où elle est posée.</p>
      <div class="ctx-row"><button type="button" data-act="cancel">Annuler</button><button type="button" data-act="kill" class="danger-btn">Supprimer</button></div>`;
    place();
    menu.querySelector('[data-act="cancel"]').focus();
  };
  const kill = async () => {
    close();
    try {
      await Store.delete(rec.id);
      toast(tr(`« ${rec.name} » supprimée`, `"${rec.name}" deleted`));
      changed('delete', rec);
    } catch (err) { console.error(err); toast(tr('Impossible de supprimer : ', 'Couldn\'t delete: ') + (err.message || err)); }
  };
  menu.addEventListener('click', e => {
    const act = e.target.closest('button')?.dataset.act;
    if (act === 'edit') { close(); edit(rec); }
    else if (act === 'rename') rename();
    else if (act === 'delete') confirmDelete();
    else if (act === 'save') save();
    else if (act === 'kill') kill();
    else if (act === 'cancel') close();
  });
  menu.addEventListener('contextmenu', e => e.preventDefault());
  main();
  addEventListener('pointerdown', outside, true);
  addEventListener('keydown', onKey, true);
  addEventListener('blur', close);
  addEventListener('resize', close);
}
