// Writes halo_ui/tools/atlas2.html: every icon of the Halo / Spartan UI in white, one grid per size (16, 19, 22, 40, 42, 96 px,
// cells of size + 4, 1024 px wide). Render it to PNG with headless Edge (see render_atlas.bat).
// The order below is the Icon enum of halo_ui/src/HaloUI.cpp and src/app/SpartanUI.cpp.
// Usage (from the repository root): node halo_ui/tools/build_atlas.js
const fs = require("fs");
const L = Object.assign({}, JSON.parse(fs.readFileSync("halo_ui/tools/lucide.json", "utf8")), JSON.parse(fs.readFileSync("halo_ui/tools/lucide_extra.json", "utf8")));
const rc = (n, crop) => { let s = fs.readFileSync("assets/ui/icons/recraft/" + n + ".svg", "utf8"); s = s.slice(s.indexOf("<svg")); return crop ? s.replace('viewBox="0 0 2048 2048"', 'viewBox="300 300 1448 1448"') : s; };
const names = ["map", "workflow", "palette", "package", "undo-2", "redo-2", "play", "hammer", "settings", "layers", "sun", "video", "search",
  "terminal", "chevron-down", "chevron-right", "globe", "eye", "mouse-pointer-2", "move", "rotate-3d", "scaling", "magnet", "camera",
  "sparkles", "more-horizontal", "move-3d", "clock-3", "folder-open", "box", "image", "@outpost", "@unit-helmet", "@grid-snap", "@brand",
  "plus", "zap", "git-branch", "hand", "maximize", "droplet", "puzzle", "crosshair", "shield", "repeat", "copy", "circle-play"];
const brand = '<svg viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="1.6"><path d="M12 2 21 7v10l-9 5-9-5V7z"/><path d="M12 7v10M7.5 9.5 12 12l4.5-2.5"/></svg>';
const svgs = names.map(n => n === "@brand" ? brand : n[0] === "@" ? rc(n.slice(1), true) : '<svg viewBox="0 0 24 24">' + L[n] + "</svg>");
const sizes = [16, 19, 22, 40, 42, 96], W = 1024;
let y = 0, out = "";
for (const S of sizes) {
  const C = S + 4, cols = Math.floor(W / C);
  svgs.forEach((c, i) => { const x = (i % cols) * C, yy = y + Math.floor(i / cols) * C; out += `<div style="position:absolute;left:${x + 2}px;top:${yy + 2}px;width:${S}px;height:${S}px">${c}</div>`; });
  y += Math.ceil(svgs.length / cols) * C;
}
fs.writeFileSync("halo_ui/tools/atlas2.html", `<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;background:transparent}div>svg{width:100%;height:100%;display:block;color:#fff}</style></head><body>${out}</body></html>`);
fs.writeFileSync("halo_ui/tools/atlas_names.txt", names.join("\n") + "\n");
console.log("icons", names.length, "height", y);
