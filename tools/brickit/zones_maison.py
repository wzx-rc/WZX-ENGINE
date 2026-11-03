"""Mean linear luminance per zone of the GI house views (47*_<view>.png, 1600 x 900), each image against PT 512.
usage: py zones_house.py <suivi dir> [prefix=47]"""
import sys
from PIL import Image

VIEWS = {
    "ext": {
        "interieur (porte)":   (0.57, 0.54, 0.59, 0.64),
        "facade":              (0.45, 0.40, 0.55, 0.53),
        "mur de cote":         (0.36, 0.44, 0.40, 0.62),
        "toit":                (0.44, 0.31, 0.62, 0.38),
        "ombre au sol":        (0.37, 0.82, 0.56, 0.93),
    },
    "porte": {
        "interieur":           (0.46, 0.52, 0.56, 0.72),
        "mur gauche":          (0.19, 0.56, 0.41, 0.88),
        "mur pres du seuil":   (0.61, 0.80, 0.70, 0.95),
        "sous le debord":      (0.19, 0.16, 0.87, 0.22),
        "marche":              (0.46, 0.91, 0.65, 0.97),
    },
    "pied": {
        "dans la fenetre":     (0.26, 0.53, 0.34, 0.84),
        "mur gauche":          (0.05, 0.58, 0.19, 0.89),
        "mur droit":           (0.65, 0.56, 0.78, 0.89),
        "sous le debord":      (0.65, 0.03, 0.92, 0.13),
    },
}
MODES = [("a_hq", "HQ sans GI"), ("b_hqgi", "HQ + GI douce"), ("c_rc", "RC")]

def lin(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

def zone_lum(path, zones):
    im = Image.open(path).convert('RGB')
    W, H = im.size
    out = {}
    for name, (x0, y0, x1, y1) in zones.items():
        px = list(im.crop((int(x0 * W), int(y0 * H), int(x1 * W), int(y1 * H))).resize((40, 20)).getdata())
        out[name] = sum(0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b) for r, g, b in px) / len(px)
    return out

d = sys.argv[1]
pre = sys.argv[2] if len(sys.argv) > 2 else "47"
grand = [0.0] * len(MODES); n = 0
for v, zones in VIEWS.items():
    pt = zone_lum(f"{d}\\{pre}d_pt_{v}.png", zones)
    rows = [zone_lum(f"{d}\\{pre}{m}_{v}.png", zones) for m, _ in MODES]
    print(f"--- {v}")
    print("zone".ljust(22) + "PT".rjust(8) + "".join(t.rjust(22) for _, t in MODES))
    for z in zones:
        line = z.ljust(22) + f"{pt[z]:8.4f}"
        for i, r in enumerate(rows):
            e = (r[z] - pt[z]) / max(pt[z], 1e-4) * 100
            grand[i] += abs(e)
            line += f"{r[z]:10.4f} ({e:+6.1f}%)"
        print(line)
        n += 1
print("ecart moyen |%|".ljust(30) + "".join(f"{g / n:22.1f}" for g in grand))
