// Minimal client of the WZX Engine command API (the editor must be running): wzx('object.add', {...}).
// The token comes from %LOCALAPPDATA%/WZX Engine/api.json. The port too, but another editor (a self-test, the one
// of the main folder) may have written that file since: the editor whose project is this folder is looked for on
// the ports from 7878 when the written one does not answer for it. scene.save is retried a few times (a file just
// written may still be held by Dropbox for a moment).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const cfg = JSON.parse(fs.readFileSync(path.join(process.env.LOCALAPPDATA, 'WZX Engine', 'api.json'), 'utf8'));
const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..').toLowerCase();
let port = null;

async function post(p, body) {
  const res = await fetch(`http://127.0.0.1:${p}/cmd`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-WZX-Token': cfg.token },
    body: JSON.stringify(body),
  });
  return res.json();
}

async function findPort() {
  const candidates = [cfg.port, ...Array.from({ length: 10 }, (_, k) => 7878 + k).filter((p) => p !== cfg.port)];
  for (const p of candidates) {
    try {
      const s = await post(p, { cmd: 'state' });
      if (s.ok && String(s.projectDir).toLowerCase() === project) return p;
    } catch { /* nothing there */ }
  }
  throw new Error('no WZX editor of ' + project + ' is running');
}

export async function wzx(cmd, args = {}) {
  if (port === null) port = await findPort();
  for (let attempt = 0; ; ++attempt) {
    const j = await post(port, { cmd, ...args });
    if (j.ok) return j;
    if (cmd === 'scene.save' && attempt < 5) { await new Promise((r) => setTimeout(r, 1500)); continue; }
    throw new Error(`${cmd}: ${j.error ?? JSON.stringify(j)}`);
  }
}
