# The icons of the touch HUD of BrickIt (Tiny Glade style, Wzx 05/11/2026): bold rounded white shapes, thick strokes
# with round caps and joins, a soft dark drop shadow baked in. Drawn at 4 x then reduced (anti-aliased), written as
# 160 px RGBA PNG into assets/brickit/hud (read by BrickUi.cpp, drawn at ~48-56 px).
# Usage: py tools/brickit/hud_icons.py
import math, os
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "brickit", "hud")
S = 640           # drawing size (4 x 160)
W = 52            # stroke width at S (~ 4.5 px at 56 px)


def canvas():
    return Image.new("L", (S, S), 0)


def stroke(d, pts, w=W, closed=False):
    if closed: pts = pts + [pts[0]]
    for a, b in zip(pts, pts[1:]): d.line([a, b], fill=255, width=w)
    for p in pts: d.ellipse([p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2], fill=255)


def arc(cx, cy, r, a0, a1, n=64):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def rounded_rect(d, x0, y0, x1, y1, r, w=W):
    pts = arc(x1 - r, y0 + r, r, -90, 0, 12) + arc(x1 - r, y1 - r, r, 0, 90, 12) + arc(x0 + r, y1 - r, r, 90, 180, 12) + arc(x0 + r, y0 + r, r, 180, 270, 12)
    stroke(d, pts, w, closed=True)


def turn(mirror):
    m = canvas(); d = ImageDraw.Draw(m)
    c, r = S / 2, S * 0.27
    a0, a1 = -200, 70   # an open circle, the arrow head at its end
    pts = arc(c, c + 10, r, a0, a1)
    stroke(d, pts)
    ex, ey = pts[-1]
    t = math.radians(a1 + 90)   # tangent at the end
    hl = S * 0.15
    for da in (150, -150):
        ang = t + math.radians(da)
        stroke(d, [(ex, ey), (ex + hl * math.cos(ang), ey + hl * math.sin(ang))])
    return m.transpose(Image.FLIP_LEFT_RIGHT) if mirror else m


def dup():
    m = canvas(); d = ImageDraw.Draw(m)
    rounded_rect(d, S * 0.36, S * 0.20, S * 0.80, S * 0.64, S * 0.08)
    # the back square: its part not hidden by the front one (top left, left, bottom, bottom right), a gap at the front
    r = S * 0.08
    x0, y0, x1, y1 = S * 0.20, S * 0.36, S * 0.64, S * 0.80
    stroke(d, [(S * 0.28, y0)] + arc(x0 + r, y0 + r, r, 270, 180, 10) + arc(x0 + r, y1 - r, r, 180, 90, 10) + arc(x1 - r, y1 - r, r, 90, 0, 10) + [(x1, S * 0.72)])
    # plus
    cx, cy = S * 0.58, S * 0.42
    stroke(d, [(cx - S * 0.09, cy), (cx + S * 0.09, cy)], int(W * 0.9))
    stroke(d, [(cx, cy - S * 0.09), (cx, cy + S * 0.09)], int(W * 0.9))
    return m


def move():
    m = canvas(); d = ImageDraw.Draw(m)
    c = S / 2
    L, h, aw = S * 0.32, S * 0.10, S * 0.11   # arm length, head length, head half width
    for k in range(4):
        ang = math.radians(90 * k)
        ux, uy = math.cos(ang), math.sin(ang)
        tip = (c + ux * L, c + uy * L)
        stroke(d, [(c, c), tip])
        base = (tip[0] - ux * h, tip[1] - uy * h)
        stroke(d, [tip, (base[0] - uy * aw, base[1] + ux * aw)])
        stroke(d, [tip, (base[0] + uy * aw, base[1] - ux * aw)])
    return m


def more():
    m = canvas(); d = ImageDraw.Draw(m)
    c, r = S / 2, S * 0.33
    stroke(d, arc(c, c, r, 0, 360, 96), int(W * 0.85))
    for k in (-1, 0, 1):
        x = c + k * S * 0.13
        d.ellipse([x - S * 0.045, c - S * 0.045, x + S * 0.045, c + S * 0.045], fill=255)
    return m


def hexagon():
    m = canvas(); d = ImageDraw.Draw(m)
    c, r = S / 2, S * 0.30
    pts = [(c + r * math.cos(math.radians(60 * k - 90)), c + r * math.sin(math.radians(60 * k - 90))) for k in range(6)]
    d.polygon(pts, fill=255)
    stroke(d, pts, int(W * 0.9), closed=True)   # rounded corners
    return m


def palette():   # the entry of the colour arc: a paint drop
    m = canvas(); d = ImageDraw.Draw(m)
    c = S / 2
    pts = arc(c, c + S * 0.08, S * 0.20, -30, 210, 48)
    pts = [(c, c - S * 0.30)] + pts[::-1][::-1]
    d.polygon([(c, c - S * 0.30)] + arc(c, c + S * 0.08, S * 0.21, -35, 215, 48), fill=255)
    return m


def finish(mask, name):
    sh = Image.new("RGBA", (S, S), (16, 18, 26, 0))
    blur = mask.filter(ImageFilter.GaussianBlur(S * 0.035))
    a = blur.point(lambda v: int(v * 0.50))
    sh.putalpha(Image.new("L", (S, S), 0))
    shadow = Image.new("RGBA", (S, S), (16, 18, 26, 255))
    shadow.putalpha(a)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.alpha_composite(shadow, (0, int(S * 0.025)))
    white = Image.new("RGBA", (S, S), (255, 255, 255, 255))
    white.putalpha(mask)
    out.alpha_composite(white)
    out = out.resize((160, 160), Image.LANCZOS)
    out.save(os.path.join(OUT, name + ".png"))


os.makedirs(OUT, exist_ok=True)
finish(turn(False), "turn_right")
finish(turn(True), "turn_left")
finish(dup(), "duplicate")
finish(move(), "move")
finish(more(), "more")
finish(hexagon(), "hexagon")
finish(palette(), "colour")
print("ok", OUT)
