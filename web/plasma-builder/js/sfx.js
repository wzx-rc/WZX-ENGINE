// Petits sons du Builder, synthétisés en Web Audio (aucun fichier) : on prend une pièce, elle passe d'un socket
// à l'autre, elle s'accroche à l'ork, on l'enlève. Volume bas, légères variations pour ne jamais sonner pareil.
// Coupé ou remis dans Réglages › Sons (localStorage 'plasmaork.sfx' = 'off' | 'on').
const KEY = 'plasmaork.sfx';
let ctx = null, out = null;
export const sfxOn = () => { try { return localStorage.getItem(KEY) !== 'off'; } catch { return true; } };
export const setSfx = on => { try { localStorage.setItem(KEY, on ? 'on' : 'off'); } catch { /* stockage indisponible */ } };

function audio() {
  if (!sfxOn()) return null;
  if (!ctx) {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    ctx = new AC();
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -18; comp.ratio.value = 4;
    out = ctx.createGain(); out.gain.value = 0.32;
    out.connect(comp).connect(ctx.destination);
  }
  if (ctx.state === 'suspended') ctx.resume();
  return ctx;
}
const jit = (v, p = 0.04) => v * (1 + (Math.random() * 2 - 1) * p);

// note : oscillateur avec enveloppe percussive, glissando optionnel
function tone(a, { f, f2 = f, type = 'sine', t = 0, dur = 0.2, vol = 0.3, att = 0.004 }) {
  const t0 = a.currentTime + t, o = a.createOscillator(), g = a.createGain();
  o.type = type;
  o.frequency.setValueAtTime(f, t0);
  if (f2 !== f) o.frequency.exponentialRampToValueAtTime(f2, t0 + dur * 0.8);
  g.gain.setValueAtTime(0.0001, t0);
  g.gain.exponentialRampToValueAtTime(vol, t0 + att);
  g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
  o.connect(g).connect(out);
  o.start(t0); o.stop(t0 + dur + 0.02);
}
// bruit filtré : le « clac » de plastique/résine
let noiseBuf = null;
function click(a, { t = 0, f = 2600, q = 3, dur = 0.05, vol = 0.35 }) {
  if (!noiseBuf) {
    noiseBuf = a.createBuffer(1, a.sampleRate * 0.3, a.sampleRate);
    const d = noiseBuf.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  }
  const t0 = a.currentTime + t, s = a.createBufferSource(), bp = a.createBiquadFilter(), g = a.createGain();
  s.buffer = noiseBuf;
  bp.type = 'bandpass'; bp.frequency.value = f; bp.Q.value = q;
  g.gain.setValueAtTime(vol, t0);
  g.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);
  s.connect(bp).connect(g).connect(out);
  s.start(t0, Math.random() * 0.2); s.stop(t0 + dur + 0.02);
}

// hauteur du petit carillon selon le type de pièce (do majeur, la tête sonne plus grave et plus « ta-da »)
const CHIME = { head: [392, 494, 587, 784], weapon: [587, 880], back: [440, 659], shoulder: [523, 784], chest: [494, 740], belt: [659, 988], base: [330, 494] };

export const sfx = {
  // la pièce arrive dans la main
  pickup() {
    const a = audio(); if (!a) return;
    tone(a, { f: jit(260), f2: jit(520), type: 'sine', dur: 0.16, vol: 0.12, att: 0.02 });
  },
  // la pièce passe d'un socket à l'autre pendant qu'on la tient
  hover() {
    const a = audio(); if (!a) return;
    tone(a, { f: jit(1500, 0.06), type: 'triangle', dur: 0.04, vol: 0.05, att: 0.002 });
  },
  // la pièce s'accroche : clac + petit choc sourd + carillon
  place(cat) {
    const a = audio(); if (!a) return;
    const head = cat === 'head';
    click(a, { f: jit(head ? 1800 : 2600), dur: 0.045, vol: 0.4 });
    tone(a, { f: jit(head ? 150 : 190), f2: head ? 70 : 95, dur: head ? 0.18 : 0.12, vol: head ? 0.5 : 0.35, att: 0.002 });
    const notes = CHIME[cat] || CHIME.weapon;
    notes.forEach((n, i) => {
      tone(a, { f: n, t: 0.05 + i * (head ? 0.07 : 0.06), type: 'triangle', dur: 0.35, vol: 0.12 });
      tone(a, { f: n * 2, t: 0.05 + i * (head ? 0.07 : 0.06), type: 'sine', dur: 0.25, vol: 0.035 });
    });
  },
  // la pièce est enlevée : pop qui descend
  remove() {
    const a = audio(); if (!a) return;
    click(a, { f: jit(1200), q: 1.5, dur: 0.06, vol: 0.25 });
    tone(a, { f: jit(520), f2: 210, type: 'sine', dur: 0.18, vol: 0.2, att: 0.004 });
  },
};
