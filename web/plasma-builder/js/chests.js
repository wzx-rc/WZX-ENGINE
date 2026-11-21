// Coffres et pack du Hub en dessin vectoriel plat (même famille : même socle, mêmes gris, une couleur d'accent par rang).
// Le couvercle est un groupe à part (.lid) : la classe .open sur le <svg> le soulève (CSS dans hub.css).
const G = { body: '#353941', side: '#2a2d33', lid: '#41464f', band: '#585e6a', rivet: '#7a808c', hi: 'rgba(255,255,255,.07)', bone: '#e9e3d6' };
const rivets = (pts) => pts.map(([x, y]) => `<circle cx="${x}" cy="${y}" r="3" fill="${G.rivet}"/>`).join('');

const PARTS = {
  // caisse de ferraille : couvercle plat, planches, loquet simple
  crate: {
    lid: `<rect x="28" y="64" width="184" height="34" rx="7" fill="${G.lid}"/>
      <rect x="28" y="64" width="184" height="6" rx="3" fill="${G.hi}"/>
      <rect x="40" y="64" width="14" height="34" fill="${G.band}"/><rect x="186" y="64" width="14" height="34" fill="${G.band}"/>`,
    box: `<rect x="32" y="96" width="176" height="84" rx="8" fill="${G.body}"/>
      <path d="M32 120h176M32 146h176" stroke="${G.side}" stroke-width="3"/>
      <rect x="40" y="96" width="14" height="84" fill="${G.band}"/><rect x="186" y="96" width="14" height="84" fill="${G.band}"/>
      <rect x="66" y="160" width="108" height="4" rx="2" fill="var(--lc)" opacity=".9"/>
      <rect x="106" y="92" width="28" height="30" rx="5" fill="${G.band}"/><rect x="114" y="102" width="12" height="10" rx="2" fill="none" stroke="var(--lc)" stroke-width="3"/>
      ${rivets([[47, 106], [47, 170], [193, 106], [193, 170]])}`,
  },
  // coffre de Nob : couvercle bombé, cerclages, serrure à crâne
  nob: {
    lid: `<path d="M26 98V82c0-20 28-34 94-34s94 14 94 34v16z" fill="${G.lid}"/>
      <path d="M40 66c18-10 44-14 80-14s62 4 80 14" stroke="${G.hi}" stroke-width="5" fill="none" stroke-linecap="round"/>
      <path d="M58 98V58h18v40zM164 98V58h18v40z" fill="${G.band}"/>
      <rect x="96" y="70" width="48" height="5" rx="2.5" fill="var(--lc)"/>`,
    box: `<rect x="30" y="96" width="180" height="84" rx="8" fill="${G.body}"/>
      <rect x="30" y="166" width="180" height="14" rx="6" fill="${G.side}"/>
      <rect x="58" y="96" width="18" height="84" fill="${G.band}"/><rect x="164" y="96" width="18" height="84" fill="${G.band}"/>
      <rect x="38" y="112" width="10" height="40" rx="5" fill="var(--lc)"/><rect x="192" y="112" width="10" height="40" rx="5" fill="var(--lc)"/>
      <rect x="98" y="100" width="44" height="50" rx="10" fill="${G.band}"/>
      <path d="M120 110c-9 0-14 6-14 13 0 5 3 8 6 9v6h16v-6c3-1 6-4 6-9 0-7-5-13-14-13z" fill="${G.bone}"/>
      <circle cx="115" cy="122" r="3" fill="var(--lc)"/><circle cx="125" cy="122" r="3" fill="var(--lc)"/>
      ${rivets([[67, 106], [67, 170], [173, 106], [173, 170]])}`,
  },
  // koffre du Warboss : grand couvercle à pointes, cornes, serrure en mâchoire
  boss: {
    lid: `<path d="M50 54l8-26 10 24zM88 46l9-30 10 28zM133 44l10-28 9 30zM172 52l10-24 8 26z" fill="${G.rivet}"/>
      <path d="M58 28l-2 6 6 0zM97 16l-2 7 6 0zM143 16l-2 7 6 0zM182 28l-2 6 6 0z" fill="var(--lc)"/>
      <path d="M22 98V80c0-24 30-40 98-40s98 16 98 40v18z" fill="${G.lid}"/>
      <path d="M38 62c20-12 46-16 82-16s62 4 82 16" stroke="${G.hi}" stroke-width="5" fill="none" stroke-linecap="round"/>
      <path d="M22 92h196" stroke="var(--lc)" stroke-width="4"/>`,
    box: `<path d="M24 112c-14-4-20-18-16-30 6 8 14 12 22 12zM216 112c14-4 20-18 16-30-6 8-14 12-22 12z" fill="${G.band}"/>
      <rect x="26" y="96" width="188" height="86" rx="8" fill="${G.body}"/>
      <rect x="26" y="168" width="188" height="14" rx="6" fill="${G.side}"/>
      <path d="M26 104h188" stroke="${G.band}" stroke-width="6"/>
      <rect x="86" y="108" width="68" height="54" rx="12" fill="${G.band}"/>
      <rect x="94" y="120" width="52" height="30" rx="6" fill="var(--lc)" opacity=".85"/>
      <path d="M94 120h52l-6 9-6-9-7 9-7-9-7 9-6-9-7 9zM94 150h52l-6-9-7 9-6-9-7 9-7-9-6 9-7-9z" fill="${G.bone}"/>
      ${rivets([[40, 116], [40, 166], [200, 116], [200, 166], [64, 116], [176, 116]])}`,
  },
};

export function chestSVG(tier, open = false) {
  const p = PARTS[tier] || PARTS.nob;
  return `<svg class="chestsvg ${open ? 'open' : ''}" viewBox="0 0 240 200" aria-hidden="true" stroke="#e6e7ea" stroke-opacity=".45" stroke-width="2" stroke-linejoin="round">
    <ellipse cx="120" cy="186" rx="98" ry="9" fill="#000" opacity=".45" stroke="none"/>
    <g class="inside" stroke="none"><path d="M60 96L20 0h200l-40 96z" fill="var(--lc)" opacity=".18"/><rect x="34" y="88" width="172" height="14" rx="6" fill="var(--lc)"/><rect x="60" y="91" width="120" height="6" rx="3" fill="#fff" opacity=".8"/></g>
    <g class="box">${p.box}</g>
    <g class="lid">${p.lid}</g>
  </svg>`;
}

// Pack Boyz : sachet à bords crantés, rayures d'accent, logo au centre
let packN = 0;
export function packSVG() {
  // bords crantés en haut et en bas
  const top = [], bot = [];
  for (let x = 8, i = 0; x <= 152; x += 8, i++) { top.push(`L${x} ${i % 2 ? 4 : 10}`); bot.unshift(`L${x} ${i % 2 ? 226 : 220}`); }
  const id = 'pk' + (++packN);
  return `<svg class="packsvg" viewBox="0 0 160 230" aria-hidden="true" stroke="none">
    <defs><clipPath id="${id}"><path d="M8 10 ${top.join(' ')} V220 ${bot.join(' ')} Z"/></clipPath></defs>
    <g clip-path="url(#${id})">
      <rect width="160" height="230" fill="var(--hot-deep, #1b0b14)"/>
      <path d="M-20 78l200-58v22L-20 100zM-20 210l200-58v12L-20 222z" fill="var(--pink, #ff2e88)" opacity=".8"/>
      <rect x="0" y="22" width="160" height="2" fill="var(--pink, #ff2e88)" opacity=".45"/>
      <rect x="0" y="206" width="160" height="2" fill="var(--pink, #ff2e88)" opacity=".45"/>
    </g>
    <image href="assets/brand/logo.svg" x="40" y="88" width="80" height="80"/>
  </svg>`;
}
