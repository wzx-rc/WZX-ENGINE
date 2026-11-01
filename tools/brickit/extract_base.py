"""Extracts the base BrickIt library (50 parts) from the web builder for BrickIt.exe.

    py tools/brickit/extract_base.py [docs/brickit/web/BrickIt_Builder.html] [assets/brickit]

Writes base.json (window.__VP3D_LIB: palette, parts and their metas, thumbnails removed) and base.bin
(window.__VP3D_BIN decoded: per part float32 x6 vertices (position, normal) then uint32 indices, offsets in base.json).
The Technic packs (docs/brickit/web/technic/g{n}.wasm) are read as they are by BrickIt.exe.
"""
import base64
import json
import os
import sys

root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "docs", "brickit", "web", "BrickIt_Builder.html")
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "assets", "brickit")

with open(src, "r", encoding="utf-8") as f:
    html = f.read()
dec = json.JSONDecoder()


def value(name):
    i = html.find("window.%s=" % name)
    if i < 0:
        raise SystemExit("%s not found in %s" % (name, src))
    v, _ = dec.raw_decode(html, i + len(name) + 8)
    return v


lib = value("__VP3D_LIB")
blob = base64.b64decode(value("__VP3D_BIN").strip())
for p in lib["parts"]:
    p.pop("thumb", None)
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "base.json"), "w", encoding="utf-8") as f:
    json.dump(lib, f, ensure_ascii=False, separators=(",", ":"))
with open(os.path.join(out, "base.bin"), "wb") as f:
    f.write(blob)
print("%d parts, %d colours, %d bytes of geometry -> %s" % (len(lib["parts"]), len(lib["palette"]), len(blob), out))
