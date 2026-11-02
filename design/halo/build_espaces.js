// Builds design/halo/mockup-halo-espaces.html: the Scène mockup + workspaces.fragment.html (Graph, Matériaux, Prefab),
// with every icon inlined (Lucide bodies from halo_ui/tools/*.json, Recraft SVGs) so it opens without Internet.
// Usage (from the repository root): node design/halo/build_espaces.js
const fs = require("fs");
let h = fs.readFileSync("design/halo/mockup-halo.html", "utf8");
const frag = fs.readFileSync("design/halo/workspaces.fragment.html", "utf8");
const L = Object.assign({}, JSON.parse(fs.readFileSync("halo_ui/tools/lucide.json", "utf8")), JSON.parse(fs.readFileSync("halo_ui/tools/lucide_extra.json", "utf8")));
const rc = n => { const s = fs.readFileSync("assets/ui/icons/recraft/" + n + ".svg", "utf8"); return s.slice(s.indexOf("<svg")); };
const R = {helmet: rc("unit-helmet"), outpost: rc("outpost"), grid: rc("grid-snap")};
h = h.replace(/<script src="[^"]*iconify[^"]*"><\/script>\n?/, "");
h = h.replace(/<iconify-icon([^>]*)><\/iconify-icon>/g, (m, attrs) => {
  const name = (attrs.match(/icon="lucide:([a-z0-9-]+)"/) || [])[1];
  const cls = (attrs.match(/class="([^"]*)"/) || [, ""])[1];
  const st = (attrs.match(/style="([^"]*)"/) || [])[1];
  return `<i class="ic${cls ? " " + cls : ""}" data-i="${name}"${st ? ` style="${st}"` : ""}></i>`;
});
h = h.replace("<title>WZX Halo maquette</title>", "<title>WZX Halo espaces</title>");
h = h.replace('<main class="body">', '<main class="body" data-ws="0">');
let wi = 0;
h = h.replace(/<div class="ws">([\s\S]*?)<\/div>/, (m, inner) => '<div class="ws">' + inner.replace(/<a( class="on")?>/g, (mm, on) => `<a data-go="${wi++}"${on || ""}>`) + "</div>");
h = h.replace(/(<main class="body" data-ws="0">[\s\S]*?<\/main>)/, "$1\n" + frag);
const script = `<script>
const LUC=${JSON.stringify(L)};
const REC=${JSON.stringify(R)};
document.querySelectorAll("[data-i]").forEach(e=>{const n=e.dataset.i;e.innerHTML=REC[n]?REC[n]:(LUC[n]?'<svg viewBox="0 0 24 24">'+LUC[n]+'</svg>':"");});
const show=i=>{document.querySelectorAll("main.body").forEach(m=>m.hidden=m.dataset.ws!=String(i));document.querySelectorAll(".ws a").forEach(a=>a.classList.toggle("on",a.dataset.go==String(i)));};
document.querySelectorAll(".ws a").forEach(a=>a.addEventListener("click",()=>show(a.dataset.go)));
const pick=(sel,group,cls)=>document.querySelectorAll(sel).forEach(e=>e.addEventListener("click",()=>{e.closest(group).querySelectorAll(sel).forEach(o=>o.classList.remove(cls));e.classList.add(cls);}));
pick(".rail .r",".rail","on");pick(".tabs .tab",".tabs","on");pick(".tree .row",".tree","sel");pick(".comp",".panel","sel");pick(".asset",".assets","sel");
const q=new URLSearchParams(location.search).get("ws");if(q)show(q);
</script>`;
h = h.replace("</body>", script + "\n</body>");
fs.writeFileSync("design/halo/mockup-halo-espaces.html", h);
console.log("ok", h.length, "tabs", wi);
