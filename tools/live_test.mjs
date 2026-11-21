// WZX Live, end-to-end test on one PC (no iPhone): the editor sends the mecha mission to WZXPlayer.exe --live over
// 127.0.0.1, the player restarts it, its log and a screenshot come back; then the warnings of the editor are checked
// against players that claim to be older apps (--live-simulate-old, --live-simulate-protocol).
// Usage: node tools\live_test.mjs <Viewport3D.exe> <WZXPlayer.exe> <out folder>
// Only the processes it starts are closed. %LOCALAPPDATA%\WZX Engine\api.json is put back as it was.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const [editorExe, playerExe, outDir] = process.argv.slice(2);
if (!editorExe || !playerExe || !outDir) { console.error('usage: node live_test.mjs <Viewport3D.exe> <WZXPlayer.exe> <out>'); process.exit(2); }
fs.mkdirSync(outDir, { recursive: true });
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1')), '..');
const apiFile = path.join(process.env.LOCALAPPDATA, 'WZX Engine', 'api.json');
const apiBackup = fs.existsSync(apiFile) ? fs.readFileSync(apiFile) : null;
const devicesFile = path.join(process.env.LOCALAPPDATA, 'WZX Engine', 'live_devices.json');   // the devices the editor remembers
const devicesBackup = fs.existsSync(devicesFile) ? fs.readFileSync(devicesFile) : null;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let passes = 0, fails = 0;
const report = [];
function check(name, ok, info = '') {
  report.push(`${ok ? 'PASS' : 'FAIL'} ${name}${info ? '  ' + info : ''}`);
  console.log(report[report.length - 1]);
  ok ? passes++ : fails++;
}
const procs = [];
function start(exe, args) {
  const p = spawn(exe, args, { cwd: path.dirname(exe), stdio: 'ignore', detached: false });
  procs.push(p);
  return p;
}
function player(port, extra = []) {
  const dir = path.join(outDir, `device${port}`, 'game');
  fs.rmSync(path.dirname(dir), { recursive: true, force: true });
  const p = start(playerExe, ['--live', '--live-port', String(port), '--live-dir', dir, ...extra]);
  return { p, dir, state: path.join(path.dirname(dir), 'wzx_live_device.json'), port };
}
async function code(dev) {
  for (let k = 0; k < 100 && !fs.existsSync(dev.state); k++) await sleep(100);
  return JSON.parse(fs.readFileSync(dev.state, 'utf8')).code;
}

let api = null;
async function cmd(body) {
  const r = await fetch(`http://127.0.0.1:${api.port}/cmd`, { method: 'POST', headers: { 'X-WZX-Token': api.token, 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  return r.json();
}

try {
  const dev = player(47800);
  const old = player(47810, ['--live-simulate-old', 'unit.produce,prefab-clip']);
  const other = player(47820, ['--live-simulate-protocol', '99']);
  const editor = start(editorExe, []);
  for (let k = 0; k < 120; k++) {   // the editor writes its port into api.json
    await sleep(500);
    try { const j = JSON.parse(fs.readFileSync(apiFile, 'utf8')); if (j.pid === editor.pid && j.port) { api = j; break; } } catch {}
  }
  check('editor API up', !!api);
  if (!api) throw new Error('no API');
  const loaded = await cmd({ cmd: 'scene.load', path: 'game/scenes/mecha_level1.wzx' });
  check('mecha mission opened in the editor', loaded.ok === true, loaded.error || '');
  await sleep(2000);

  const list = await cmd({ cmd: 'device.list' });
  check('device.list: this PC\'s player listed', list.ok && list.devices.some((d) => d.host === '127.0.0.1' && d.port === 47800));

  // pairing: a wrong code is refused, the right one accepted (and remembered)
  const bad = await cmd({ cmd: 'device.connect', host: '127.0.0.1', port: 47800, code: '99999' });
  check('wrong pairing code refused', !bad.ok && bad.state === 'bad-code', bad.state);
  const c = await code(dev);
  const good = await cmd({ cmd: 'device.connect', host: '127.0.0.1', port: 47800, code: c });
  check('right pairing code: connected', good.ok && good.state === 'connected', JSON.stringify(good.device || good.error));
  check('device info: same engine signature as the editor', good.device && good.device.signature === good.device.editorSignature);

  // send the mecha mission: every file, then the player restarts on it
  let t0 = Date.now();
  const sent = await cmd({ cmd: 'device.send' });
  check('device.send: the game is running on the device', sent.ok === true, `${sent.files} files, ${(sent.bytes / 1048576).toFixed(1)} MB, ${Date.now() - t0} ms ${sent.message || sent.error || ''}`);
  check('no warning for an up-to-date app', sent.warnings && sent.warnings.length === 0, JSON.stringify(sent.warnings));
  check('files written in the live folder only', fs.existsSync(path.join(dev.dir, 'game.json')) && fs.existsSync(path.join(dev.dir, 'scene.wzx')) &&
        fs.existsSync(path.join(dev.dir, 'assets', 'HOUSE')));
  await sleep(1500);
  const log = await cmd({ cmd: 'device.log', lines: 200 });
  check('device log came back: game reloaded', log.ok && log.lines.some((l) => l.includes('Jeu recharg')), (log.lines || []).slice(-4).join(' | '));
  const shot = await cmd({ cmd: 'device.screenshot', maxWidth: 1280 });
  const shotOk = shot.ok && fs.existsSync(shot.path) && fs.statSync(shot.path).size > 15000;
  if (shotOk) fs.copyFileSync(shot.path, path.join(outDir, 'device_mecha.jpg'));
  check('device.screenshot: a JPEG of the game', shotOk, shot.ok ? `${shot.width}x${shot.height}, ${fs.statSync(shot.path).size} bytes` : shot.error);

  // send again, nothing changed: no file travels
  t0 = Date.now();
  const again = await cmd({ cmd: 'device.send' });
  check('second send: only changed files (none)', again.ok && again.files === 0, `${again.files} files, ${Date.now() - t0} ms`);

  // a change in the scene: only scene.wzx travels
  await cmd({ cmd: 'object.add', primitive: 'cube', pos: [0, 2, 0], name: 'LiveCube', color: [1, 0.2, 0.1] });
  const third = await cmd({ cmd: 'device.send' });
  check('after a change: only the scene file is sent', third.ok && third.files >= 1 && third.files <= 2, `${third.files} files`);

  // an older app: the warning names the node, send refused without force, accepted with force
  await cmd({ cmd: 'device.connect', host: '127.0.0.1', port: 47810, code: await code(old) });
  const warn = await cmd({ cmd: 'device.send' });
  const nodeWarn = (warn.warnings || []).some((w) => w.includes('unit.produce') && w.includes('trop ancienne'));
  check('older app: send stopped with a warning naming the node', warn.ok === false && !warn.blocked && nodeWarn, (warn.warnings || [])[0]);
  check('older app: content version and feature warnings', (warn.warnings || []).some((w) => w.includes('format de contenu')) &&
        (warn.warnings || []).some((w) => w.includes('clips')), String((warn.warnings || []).length) + ' warnings');
  const forced = await cmd({ cmd: 'device.send', force: true });
  check('older app: "envoyer quand meme" (force) sends', forced.ok === true, forced.message || forced.error);

  // another protocol: blocked
  await cmd({ cmd: 'device.connect', host: '127.0.0.1', port: 47820, code: await code(other) });
  const blocked = await cmd({ cmd: 'device.send', force: true });
  check('other protocol: blocked even with force', blocked.ok === false && blocked.blocked === true, (blocked.warnings || [])[0]);

  // back to the first player: the remembered code is used (no code given)
  const again2 = await cmd({ cmd: 'device.connect', host: '127.0.0.1', port: 47800 });
  check('remembered pairing code', again2.ok && again2.state === 'connected');
  await cmd({ cmd: 'device.window' });
  await sleep(800);
  const ed = await cmd({ cmd: 'screenshot', window: true });
  if (ed.ok && ed.path) fs.copyFileSync(ed.path, path.join(outDir, 'editor_devices_window.png'));
} catch (e) {
  check('test ran to the end', false, String(e));
} finally {
  for (const p of procs) try { p.kill(); } catch {}
  await sleep(500);
  if (apiBackup) fs.writeFileSync(apiFile, apiBackup);
  if (devicesBackup) fs.writeFileSync(devicesFile, devicesBackup); else fs.rmSync(devicesFile, { force: true });
  const sum = fails ? `FAILED ${fails} of ${passes + fails}` : `ALL PASSED (${passes} checks)`;
  report.push(sum);
  console.log(sum);
  fs.writeFileSync(path.join(outDir, 'live_test_report.txt'), report.join('\n') + '\n');
  process.exit(fails ? 1 : 0);
}
