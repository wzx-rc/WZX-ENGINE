// Minimal client of the WZX Engine command API (the editor must be running): wzx('object.add', {...}).
// Port and token come from %LOCALAPPDATA%/WZX Engine/api.json.
import fs from 'node:fs';
import path from 'node:path';

const cfg = JSON.parse(fs.readFileSync(path.join(process.env.LOCALAPPDATA, 'WZX Engine', 'api.json'), 'utf8'));

export async function wzx(cmd, args = {}) {
  const res = await fetch(`http://127.0.0.1:${cfg.port}/cmd`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-WZX-Token': cfg.token },
    body: JSON.stringify({ cmd, ...args }),
  });
  const j = await res.json();
  if (!j.ok) throw new Error(`${cmd}: ${j.error ?? JSON.stringify(j)}`);
  return j;
}
