"""Thumbnails of the web builder -> assets/brickit/thumbs_web.pack (read by BrickIt, Thumbs.cpp).

The web shows WebP thumbnails (240 x 192, transparent background, light studio): the base parts from
window.__VP3D_THUMBS in BrickIt_Builder.html (key = part.thumb of window.__VP3D_LIB), the Technic parts from
docs/brickit/web/technic/t_<sub-family>.json. They are stored as PNG (alpha kept) so the native app shows the very
same images.

Pack: b"BTW1", u32 count, then per part: u16 name length, name (utf-8), u32 PNG length, PNG bytes.
Usage: py tools/brickit/extract_web_thumbs.py
"""
import base64
import glob
import io
import json
import os
import re
import struct

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEB = os.path.join(ROOT, "docs", "brickit", "web")


def data_url_png(url):
    raw = base64.b64decode(url[url.index(",") + 1:])
    im = Image.open(io.BytesIO(raw)).convert("RGBA")
    out = io.BytesIO()
    im.save(out, "PNG", optimize=True)
    return out.getvalue()


def main():
    html = open(os.path.join(WEB, "BrickIt_Builder.html"), encoding="utf-8").read()
    dec = json.JSONDecoder()
    thumbs = dec.raw_decode(html, html.index("window.__VP3D_THUMBS") + len("window.__VP3D_THUMBS") + html[html.index("window.__VP3D_THUMBS"):].index("{") - len("window.__VP3D_THUMBS"))[0]
    lib = dec.raw_decode(html, html.index("window.__VP3D_LIB=") + len("window.__VP3D_LIB="))[0]
    images = {}
    for p in lib["parts"]:
        t = p.get("thumb") or ""
        url = t if t.startswith("data:") else thumbs.get(t, "")
        if url:
            images[p["name"]] = url
    for f in sorted(glob.glob(os.path.join(WEB, "technic", "t_*.json"))):
        for name, url in json.load(open(f, encoding="utf-8")).items():
            if url and name not in images:
                images[name] = url
    out = bytearray(b"BTW1" + struct.pack("<I", len(images)))
    for name, url in sorted(images.items()):
        png = data_url_png(url)
        nb = name.encode("utf-8")
        out += struct.pack("<H", len(nb)) + nb + struct.pack("<I", len(png)) + png
    dst = os.path.join(ROOT, "assets", "brickit", "thumbs_web.pack")
    open(dst, "wb").write(out)
    print(f"{len(images)} thumbnails, {len(out) / 1e6:.1f} MB -> {dst}")


if __name__ == "__main__":
    main()
