# -*- coding: utf-8 -*-
"""
curve_cut.py - Decoupe d'un mesh ferme le long d'une courbe dessinee dans la vue.

Inspire du "Curved Cut" de CatPin (add-on Blender) : on trace une ligne dans
le viewport, la ligne est extrudee selon la direction de la camera pour former
une surface de coupe, et le mesh est separe en deux pieces fermees (cappees),
avec un ecart (gap) optionnel entre les deux pieces.

Maya 2022+ (Python 3). Utilise maya.cmds + OpenMaya 2.

Utilisation rapide (Script Editor, onglet Python) :
    import curve_cut
    curve_cut.show_ui()

Deux facons de couper :
  1. "Dessiner une decoupe" : selectionner le(s) mesh(es), cliquer le bouton,
     puis cliquer-glisser dans le viewport. La coupe est faite au relachement.
  2. "Couper avec la courbe selectionnee" : selectionner le(s) mesh(es) puis une
     courbe NURBS (EP / Pencil / CV...). La courbe est projetee depuis la camera
     du viewport actif (ou depuis la vue memorisee si la courbe a ete creee
     par l'outil de dessin), donc elle peut etre dessinee n'importe ou dans
     l'espace tant qu'elle "passe" par-dessus le mesh dans la vue.

Une courbe ouverte coupe tout le mesh (ses extremites sont prolongees).
Une courbe fermee decoupe "a l'emporte-piece" : piece interieure / exterieure.

Connecteur pin / socket (option) : un pin (cylindre ou carre) est ajoute sur
une piece et le trou correspondant, agrandi du jeu, est creuse dans l'autre.
Il est place automatiquement dans la zone la plus epaisse de la section de
coupe (ou a une position choisie le long du trait), et sa longueur est
limitee pour rester dans la matiere.

Le haut du fichier (jusqu'a "Partie Maya") est du Python pur, sans Maya,
pour pouvoir etre teste hors Maya.
"""
from __future__ import division

import json
import math

__version__ = "0.2.0"

# ============================================================================
# Partie mathematique pure (aucune dependance Maya)
# ============================================================================

EPS = 1e-9


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def mat_transform_point(m, p):
    """Point * matrice 4x4 (convention Maya : 16 floats, ligne par ligne,
    translation dans m[12], m[13], m[14])."""
    x, y, z = p
    w = x * m[3] + y * m[7] + z * m[11] + m[15]
    if abs(w) < EPS:
        w = 1.0
    return ((x * m[0] + y * m[4] + z * m[8] + m[12]) / w,
            (x * m[1] + y * m[5] + z * m[9] + m[13]) / w,
            (x * m[2] + y * m[6] + z * m[10] + m[14]) / w)


def mat_inverse(m):
    """Inverse d'une matrice 4x4 (16 floats) par Gauss-Jordan."""
    a = [list(m[i * 4:(i + 1) * 4]) + [1.0 if i == j else 0.0 for j in range(4)]
         for i in range(4)]
    for col in range(4):
        pivot = max(range(col, 4), key=lambda r: abs(a[r][col]))
        if abs(a[pivot][col]) < 1e-12:
            raise ValueError("Matrice non inversible")
        a[col], a[pivot] = a[pivot], a[col]
        pv = a[col][col]
        a[col] = [v / pv for v in a[col]]
        for r in range(4):
            if r != col:
                f = a[r][col]
                if f:
                    a[r] = [rv - f * cv for rv, cv in zip(a[r], a[col])]
    return [a[i][4 + j] for i in range(4) for j in range(4)]


class ViewProjector(object):
    """Passage monde <-> "plan image" de la camera.

    Espace camera Maya : la camera regarde vers -Z, X a droite, Y en haut.
    - perspective : uv = (x / d, y / d) avec d = -z (profondeur)
    - orthographique : uv = (x, y)
    Un point uv a la profondeur d se replace en (u*d, v*d, -d) ou (u, v, -d).
    """

    def __init__(self, cam_world_matrix, ortho=False):
        self.m = list(cam_world_matrix)
        self.inv = mat_inverse(self.m)
        self.ortho = bool(ortho)

    def to_camera(self, p):
        return mat_transform_point(self.inv, p)

    def to_image(self, p):
        """Retourne (u, v, profondeur)."""
        x, y, z = self.to_camera(p)
        d = -z
        if self.ortho:
            return x, y, d
        if d <= EPS:
            raise ValueError("Point derriere la camera")
        return x / d, y / d, d

    def from_image(self, u, v, d):
        if self.ortho:
            c = (u, v, -d)
        else:
            c = (u * d, v * d, -d)
        return mat_transform_point(self.m, c)

    def world_to_uv_length(self, length, depth):
        """Convertit une longueur monde en longueur dans le plan image."""
        if self.ortho:
            return length
        return length / max(depth, EPS)

    def to_dict(self):
        return {"matrix": self.m, "ortho": self.ortho}

    @classmethod
    def from_dict(cls, data):
        return cls(data["matrix"], data.get("ortho", False))


# --- polylignes 2D ----------------------------------------------------------

def dist2d(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def dedupe_polyline(pts, tol=1e-7, closed=False):
    out = []
    for p in pts:
        if not out or dist2d(out[-1], p) > tol:
            out.append((p[0], p[1]))
    if closed and len(out) > 2 and dist2d(out[0], out[-1]) <= tol:
        out.pop()
    return out


def polyline_length(pts, closed=False):
    total = sum(dist2d(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
    if closed and len(pts) > 1:
        total += dist2d(pts[-1], pts[0])
    return total


def resample_polyline(pts, count, closed=False):
    """Reechantillonne a pas constant (en longueur) avec `count` points."""
    pts = dedupe_polyline(pts, closed=closed)
    if len(pts) < 2:
        return pts
    src = pts + [pts[0]] if closed else pts
    seg = [dist2d(src[i], src[i + 1]) for i in range(len(src) - 1)]
    total = sum(seg)
    if total < EPS:
        return pts[:1]
    n = max(int(count), 2)
    steps = n if closed else n - 1
    out = []
    i, acc = 0, 0.0
    for k in range(n):
        target = total * k / steps
        while i < len(seg) - 1 and acc + seg[i] < target:
            acc += seg[i]
            i += 1
        t = 0.0 if seg[i] < EPS else min(max((target - acc) / seg[i], 0.0), 1.0)
        a, b = src[i], src[i + 1]
        out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def signed_area(poly):
    s = 0.0
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        s += x1 * y2 - x2 * y1
    return 0.5 * s


def ensure_ccw(poly):
    return list(poly) if signed_area(poly) >= 0 else list(reversed(poly))


def offset_polyline(pts, dist, closed=False, miter_limit=4.0):
    """Decale la polyligne de `dist` vers sa GAUCHE (dist < 0 = droite)."""
    n = len(pts)
    if n < 2 or abs(dist) < EPS:
        return list(pts)

    def left_normal(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1.0
        return (-dy / ln, dx / ln)

    out = []
    for i in range(n):
        if closed:
            prev_p, cur, next_p = pts[i - 1], pts[i], pts[(i + 1) % n]
            n1, n2 = left_normal(prev_p, cur), left_normal(cur, next_p)
        else:
            cur = pts[i]
            if i == 0:
                n1 = n2 = left_normal(pts[0], pts[1])
            elif i == n - 1:
                n1 = n2 = left_normal(pts[-2], pts[-1])
            else:
                n1, n2 = left_normal(pts[i - 1], cur), left_normal(cur, pts[i + 1])
        mx, my = n1[0] + n2[0], n1[1] + n2[1]
        ml = math.hypot(mx, my)
        if ml < EPS:
            mx, my, scale = n1[0], n1[1], 1.0
        else:
            mx, my = mx / ml, my / ml
            cosang = mx * n1[0] + my * n1[1]
            scale = min(1.0 / max(cosang, EPS), miter_limit)
        out.append((cur[0] + mx * dist * scale, cur[1] + my * dist * scale))
    return out


def _ray_circle_exit(p, d, c, r):
    """t > 0 tel que |p + t*d - c| = r (p a l'interieur du cercle)."""
    fx, fy = p[0] - c[0], p[1] - c[1]
    a = d[0] * d[0] + d[1] * d[1]
    b = 2 * (fx * d[0] + fy * d[1])
    cc = fx * fx + fy * fy - r * r
    disc = max(b * b - 4 * a * cc, 0.0)
    return (-b + math.sqrt(disc)) / (2 * a)


def region_polygon(pts, center, radius, side="left", arc_segments=96):
    """Polygone (CCW) de la region situee a gauche/droite d'une polyligne
    ouverte. Les extremites sont prolongees jusqu'a un grand cercle
    (center, radius) qui doit contenir toute la polyligne, puis on ferme par
    un arc de ce cercle."""
    if len(pts) < 2:
        raise ValueError("Il faut au moins 2 points")
    p0, p1 = pts[0], pts[1]
    d0 = (p0[0] - p1[0], p0[1] - p1[1])
    pe, pf = pts[-1], pts[-2]
    d1 = (pe[0] - pf[0], pe[1] - pf[1])
    t0 = _ray_circle_exit(p0, d0, center, radius)
    t1 = _ray_circle_exit(pe, d1, center, radius)
    start = (p0[0] + d0[0] * t0, p0[1] + d0[1] * t0)
    end = (pe[0] + d1[0] * t1, pe[1] + d1[1] * t1)

    a_start = math.atan2(start[1] - center[1], start[0] - center[0])
    a_end = math.atan2(end[1] - center[1], end[0] - center[0])
    two_pi = 2 * math.pi
    if side == "left":
        delta = (a_start - a_end) % two_pi          # CCW de end vers start
    else:
        delta = -((a_end - a_start) % two_pi)       # CW de end vers start
    if abs(delta) < 1e-6:
        delta = two_pi if side == "left" else -two_pi
    nseg = max(4, int(arc_segments * abs(delta) / two_pi) + 1)
    arc = []
    for k in range(1, nseg):
        a = a_end + delta * k / nseg
        arc.append((center[0] + radius * math.cos(a),
                    center[1] + radius * math.sin(a)))
    poly = [start] + list(pts) + [end] + arc
    return ensure_ccw(dedupe_polyline(poly, closed=True))


def _point_in_tri(p, a, b, c):
    def cr(o, u, v):
        return (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0])
    d1, d2, d3 = cr(a, b, p), cr(b, c, p), cr(c, a, p)
    neg = (d1 < -1e-12) or (d2 < -1e-12) or (d3 < -1e-12)
    pos = (d1 > 1e-12) or (d2 > 1e-12) or (d3 > 1e-12)
    return not (neg and pos)


def triangulate_polygon(poly):
    """Ear clipping d'un polygone simple CCW. Retourne des triplets d'indices
    (CCW)."""
    n = len(poly)
    if n < 3:
        return []
    idx = list(range(n))
    tris = []

    def cross_at(i0, i1, i2):
        a, b, c = poly[i0], poly[i1], poly[i2]
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    guard = 0
    while len(idx) > 3 and guard < n * n:
        guard += 1
        m = len(idx)
        clipped = False
        for k in range(m):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % m]
            cr = cross_at(i0, i1, i2)
            if cr <= 1e-14:
                continue  # sommet reflexe ou degenere
            a, b, c = poly[i0], poly[i1], poly[i2]
            inside = False
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                if _point_in_tri(poly[j], a, b, c):
                    inside = True
                    break
            if inside:
                continue
            tris.append((i0, i1, i2))
            idx.pop(k)
            clipped = True
            break
        if not clipped:
            # Polygone imparfait : on retire un sommet degenere (ou le moins
            # concave) pour ne pas bloquer.
            k = min(range(m), key=lambda kk: abs(cross_at(idx[kk - 1], idx[kk], idx[(kk + 1) % m])))
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % m]
            if cross_at(i0, i1, i2) > 0:
                tris.append((i0, i1, i2))
            idx.pop(k)
    if len(idx) == 3 and cross_at(*idx) > 0:
        tris.append(tuple(idx))
    return tris


def build_prism(poly, projector, d_near, d_far):
    """Solide ferme obtenu en extrudant `poly` (uv, CCW) entre les
    profondeurs d_near et d_far le long des rayons de la camera.
    Retourne (vertices monde, faces) ; normales vers l'exterieur."""
    poly = ensure_ccw(poly)
    n = len(poly)
    verts = [projector.from_image(u, v, d_near) for u, v in poly]
    verts += [projector.from_image(u, v, d_far) for u, v in poly]
    faces = []
    tris = triangulate_polygon(poly)
    for a, b, c in tris:
        faces.append([a, b, c])                    # cap proche (vers camera)
        faces.append([n + c, n + b, n + a])        # cap lointain (inverse)
    for i in range(n):
        j = (i + 1) % n
        faces.append([i, n + i, n + j, j])         # cotes
    return verts, faces


def mesh_signed_volume(verts, faces):
    vol = 0.0
    for f in faces:
        a = verts[f[0]]
        for k in range(1, len(f) - 1):
            b, c = verts[f[k]], verts[f[k + 1]]
            vol += v_dot(a, v_cross(b, c)) / 6.0
    return vol


def view_bounds(points_world, projector):
    """Centre/rayon dans le plan image + profondeurs min/max."""
    uvd = [projector.to_image(p) for p in points_world]
    us = [p[0] for p in uvd]
    vs = [p[1] for p in uvd]
    ds = [p[2] for p in uvd]
    cu, cv = (min(us) + max(us)) * 0.5, (min(vs) + max(vs)) * 0.5
    r = max(math.hypot(u - cu, v - cv) for u, v in zip(us, vs))
    return (cu, cv), r, min(ds), max(ds)


def plan_cutters(curve_world, mesh_points_world, projector, gap=0.0,
                 samples=200, closed=None, return_info=False):
    """Calcule les deux solides de coupe.

    Retourne une liste de (nom, operation, verts, faces) ou operation vaut
    "intersect" (piece = mesh ∩ cutter) ou "difference" (mesh - cutter).
    La premiere piece est toujours celle de GAUCHE de la courbe (ou
    l'interieur d'une courbe fermee) : c'est la piece "A" des connecteurs.
    Avec return_info=True, retourne (cutters, info) pour les connecteurs.
    """
    center, mesh_r, dmin, dmax = view_bounds(mesh_points_world, projector)
    curve_uvd = [projector.to_image(p) for p in curve_world]
    curve_uv = [(u, v) for u, v, _ in curve_uvd]
    if closed is None:
        span = polyline_length(curve_uv)
        closed = len(curve_uv) > 3 and dist2d(curve_uv[0], curve_uv[-1]) < 0.02 * max(span, EPS)
    curve_uv = resample_polyline(curve_uv, samples, closed=closed)
    if len(curve_uv) < 2:
        raise ValueError("Courbe trop courte dans la vue")

    depth_mid = 0.5 * (dmin + dmax)
    half_gap = projector.world_to_uv_length(gap * 0.5, depth_mid)

    span_d = max(dmax - dmin, EPS)
    if projector.ortho:
        d_near = dmin - 0.25 * span_d - 1.0
        d_far = dmax + 0.25 * span_d + 1.0
    else:
        d_near = max(dmin * 0.5, dmin - 0.25 * span_d)
        d_far = dmax + 0.25 * span_d + 1.0

    curve_r = max(dist2d(center, p) for p in curve_uv)
    radius = 1.5 * max(mesh_r, curve_r) + 4 * abs(half_gap) + 1e-3

    cutters = []
    if closed:
        poly = ensure_ccw(curve_uv)
        inner = offset_polyline(poly, half_gap, closed=True)    # gauche = interieur
        outer = offset_polyline(poly, -half_gap, closed=True)
        for name, op, p in (("inside", "intersect", inner),
                            ("outside", "difference", outer)):
            v, f = build_prism(p, projector, d_near, d_far)
            cutters.append((name, op, v, f))
    else:
        left = offset_polyline(curve_uv, half_gap)
        right = offset_polyline(curve_uv, -half_gap)
        for name, side, p in (("A", "left", left), ("B", "right", right)):
            poly = region_polygon(p, center, radius, side=side)
            v, f = build_prism(poly, projector, d_near, d_far)
            cutters.append((name, "intersect", v, f))
    if return_info:
        info = {"curve_uv": ensure_ccw(curve_uv) if closed else curve_uv,
                "closed": closed, "d_near": d_near, "d_far": d_far,
                "gap": gap}
        return cutters, info
    return cutters


# --- connecteurs pin / socket ------------------------------------------------

def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_scale(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def v_len(a):
    return math.sqrt(v_dot(a, a))


def v_norm(a):
    ln = v_len(a)
    return (a[0] / ln, a[1] / ln, a[2] / ln) if ln > EPS else (0.0, 0.0, 0.0)


def cut_surface_frame(projector, uv, tangent_uv, depth):
    """Point monde sur la surface de coupe + normale unitaire orientee vers la
    piece A (la gauche de la courbe dans la vue)."""
    tx, ty = tangent_uv
    tl = math.hypot(tx, ty) or 1.0
    tx, ty = tx / tl, ty / tl
    e = 1e-5
    u, v = uv
    p = projector.from_image(u, v, depth)
    pt = projector.from_image(u + e * tx, v + e * ty, depth)
    pr = projector.from_image(u, v, depth + max(1e-3 * abs(depth), 1e-4))
    pn = projector.from_image(u - e * ty, v + e * tx, depth)
    n = v_norm(v_cross(v_sub(pr, p), v_sub(pt, p)))
    if v_dot(n, v_sub(pn, p)) < 0:
        n = v_scale(n, -1.0)
    return p, n


def _hits_to_intervals(hits, tol=1e-6):
    clean = []
    for h in sorted(hits):
        if not clean or h - clean[-1] > tol:
            clean.append(h)
    return [(clean[i], clean[i + 1]) for i in range(0, len(clean) - 1, 2)]


def find_connector_spot(curve_uv, projector, ray_hits, d_near, d_far,
                        position=None, samples=64, depth_cells=64):
    """Cherche l'endroit le plus "epais" de la section de coupe.

    La section (surface de coupe ∩ mesh) est echantillonnee sur une grille
    (abscisse le long de la courbe, profondeur) par lancer de rayons
    camera ; une distance de chanfrein donne, pour chaque cellule, la
    distance au bord de la section. `ray_hits(origine, direction_unitaire)`
    renvoie les distances des intersections avec le mesh.
    `position` (0..1) force l'abscisse le long de la courbe.
    Retourne {"point", "normal", "max_radius", "t"} ou None.
    """
    pts = resample_polyline(curve_uv, samples)
    n = len(pts)
    if n < 3:
        return None
    m = depth_cells
    depths = [d_near + (d_far - d_near) * (j + 0.5) / m for j in range(m)]
    inside = []
    for i, (u, v) in enumerate(pts):
        a = projector.from_image(u, v, d_near)
        b = projector.from_image(u, v, d_far)
        dirv = v_sub(b, a)
        length = v_len(dirv)
        unit = v_scale(dirv, 1.0 / length)
        hits = [h for h in ray_hits(a, unit) if 0.0 <= h <= length]
        spans = [(d_near + (d_far - d_near) * h0 / length,
                  d_near + (d_far - d_near) * h1 / length)
                 for h0, h1 in _hits_to_intervals(hits)]
        inside.append([any(lo <= d <= hi for lo, hi in spans) for d in depths])

    # pas monde de la grille (au milieu de la profondeur)
    dmid = 0.5 * (d_near + d_far)
    mids = [projector.from_image(u, v, dmid) for u, v in pts]
    ds = sum(v_len(v_sub(mids[i + 1], mids[i])) for i in range(n - 1)) / (n - 1)
    c = pts[n // 2]
    dd = v_len(v_sub(projector.from_image(c[0], c[1], depths[1]),
                     projector.from_image(c[0], c[1], depths[0])))
    dg = math.hypot(ds, dd)

    big = 1e30
    dist = [[(big if inside[i][j] else 0.0) for j in range(m)] for i in range(n)]
    for i in range(n):            # passe avant
        for j in range(m):
            if dist[i][j] == 0.0:
                continue
            best = dist[i][j]
            best = min(best, (dist[i - 1][j] if i > 0 else 0.0) + ds)
            best = min(best, (dist[i][j - 1] if j > 0 else 0.0) + dd)
            best = min(best, (dist[i - 1][j - 1] if i > 0 and j > 0 else 0.0) + dg)
            best = min(best, (dist[i - 1][j + 1] if i > 0 and j < m - 1 else 0.0) + dg)
            dist[i][j] = best
    for i in range(n - 1, -1, -1):  # passe arriere
        for j in range(m - 1, -1, -1):
            if dist[i][j] == 0.0:
                continue
            best = dist[i][j]
            best = min(best, (dist[i + 1][j] if i < n - 1 else 0.0) + ds)
            best = min(best, (dist[i][j + 1] if j < m - 1 else 0.0) + dd)
            best = min(best, (dist[i + 1][j + 1] if i < n - 1 and j < m - 1 else 0.0) + dg)
            best = min(best, (dist[i + 1][j - 1] if i < n - 1 and j > 0 else 0.0) + dg)
            dist[i][j] = best

    if position is None:
        rows = range(n)
    else:
        rows = [min(max(int(round(position * (n - 1))), 0), n - 1)]
    best_val, bi, bj = 0.0, None, None
    for i in rows:
        for j in range(m):
            if dist[i][j] > best_val:
                best_val, bi, bj = dist[i][j], i, j
    if bi is None:
        return None
    # la distance de chanfrein compte depuis le centre de la cellule voisine
    max_r = max(best_val - 0.5 * min(ds, dd), 0.0)
    i0, i1 = max(bi - 1, 0), min(bi + 1, n - 1)
    tangent = (pts[i1][0] - pts[i0][0], pts[i1][1] - pts[i0][1])
    p, nrm = cut_surface_frame(projector, pts[bi], tangent, depths[bj])
    return {"point": p, "normal": nrm, "max_radius": max_r,
            "t": bi / float(n - 1)}


def cylinder_mesh(p0, p1, radius, segments=32, phase=0.0):
    """Cylindre ferme de p0 a p1 (normales sortantes)."""
    axis = v_norm(v_sub(p1, p0))
    ref = (1.0, 0.0, 0.0) if abs(axis[0]) < 0.9 else (0.0, 1.0, 0.0)
    a = v_norm(v_cross(axis, ref))
    b = v_cross(axis, a)                          # a x b = axis
    verts = []
    for base in (p0, p1):
        for k in range(segments):
            t = phase + 2 * math.pi * k / segments
            off = v_add(v_scale(a, radius * math.cos(t)), v_scale(b, radius * math.sin(t)))
            verts.append(v_add(base, off))
    c0, c1 = len(verts), len(verts) + 1
    verts += [p0, p1]
    faces = []
    for k in range(segments):
        k2 = (k + 1) % segments
        faces.append([k, k2, segments + k2, segments + k])
        faces.append([c0, k2, k])
        faces.append([c1, segments + k, segments + k2])
    return verts, faces


def _free_depth(ray_hits, center, direction, radius, rays=8):
    """Distance minimale avant de sortir du mesh, depuis `center` et un anneau
    de rayon `radius` autour, dans la direction `direction`."""
    ref = (1.0, 0.0, 0.0) if abs(direction[0]) < 0.9 else (0.0, 1.0, 0.0)
    a = v_norm(v_cross(direction, ref))
    b = v_cross(direction, a)
    origins = [center]
    for k in range(rays):
        t = 2 * math.pi * k / rays
        origins.append(v_add(center, v_add(v_scale(a, radius * math.cos(t)),
                                           v_scale(b, radius * math.sin(t)))))
    best = 1e30
    for o in origins:
        hits = [h for h in ray_hits(o, direction) if h > 1e-6]
        best = min(best, hits[0] if hits else 0.0)
    return best


def plan_connector(spot, radius=0.0, length=0.0, gap=0.0, clearance=0.02,
                   shape="cylinder", pin_side="A", segments=32, ray_hits=None):
    """Geometrie du pin (a unir a sa piece) et du socket (a soustraire de
    l'autre piece). radius/length <= 0 : automatique d'apres la section.
    Si `ray_hits` (voir find_connector_spot) est fourni, la longueur du pin
    est limitee pour qu'il reste dans la matiere des deux pieces.
    Retourne un dict avec "pin", "socket" (verts, faces), "pin_part"
    ("A" ou "B"), "radius", "length" et "warning" (ou None)."""
    p, n = spot["point"], spot["normal"]
    if pin_side == "B":
        n = v_scale(n, -1.0)                      # n pointe vers la piece du pin
    max_r = spot["max_radius"]
    warning = None
    if radius <= 0:
        radius = max(0.4 * max_r - clearance, 0.0)
    if radius <= 0:
        radius = max(0.4 * max_r, 1e-4)
        warning = "Section trop fine pour ce jeu."
    if radius + clearance > 0.9 * max_r:
        warning = ("Le connecteur (rayon %.3g + jeu) deborde presque de la "
                   "section (rayon max %.3g)." % (radius, max_r))
    if length <= 0:
        length = 2.0 * radius
    half = gap * 0.5
    embed = 0.5 * radius                          # ancrage dans sa piece
    if ray_hits is not None:
        ring = (radius + clearance) * (math.sqrt(2) if shape == "square" else 1.0)
        room = _free_depth(ray_hits, p, v_scale(n, -1.0), ring) - half
        fit = 0.85 * room - clearance
        if fit < length:
            length = max(fit, 0.0)
            if length < radius:
                warning = ("Peu de matiere derriere la coupe : pin raccourci a "
                           "%.3g." % length)
        own = _free_depth(ray_hits, p, n, radius) - half
        embed = max(min(embed, 0.5 * own), 1e-4)
        if length <= 0:
            return None
    if shape == "square":
        segs, ph, r_geo, c_geo = 4, math.pi / 4, radius * math.sqrt(2), clearance * math.sqrt(2)
    else:
        segs, ph, r_geo, c_geo = segments, 0.0, radius, clearance
    pin = cylinder_mesh(v_add(p, v_scale(n, half + embed)),
                        v_sub(p, v_scale(n, half + length)), r_geo, segs, ph)
    socket = cylinder_mesh(v_add(p, v_scale(n, half + embed)),
                           v_sub(p, v_scale(n, half + length + clearance)),
                           r_geo + c_geo, segs, ph)
    return {"pin": pin, "socket": socket, "pin_part": pin_side,
            "radius": radius, "length": length, "warning": warning}


# ============================================================================
# Partie Maya
# ============================================================================

try:
    import maya.cmds as cmds
    import maya.api.OpenMaya as om
    IN_MAYA = True
except ImportError:  # tests hors Maya
    cmds = None
    om = None
    IN_MAYA = False

VIEW_ATTR = "curveCutView"
WINDOW = "curveCutWindow"
CONTEXT = "curveCutDrawCtx"
PREVIEW_CURVE = "curveCut_stroke"

_settings = {
    "gap": 0.0,
    "samples": 200,
    "keep_original": True,
    "keep_cutters": False,
    "keep_curve": True,
    "connector": False,
    "conn_shape": "cylinder",     # "cylinder" ou "square"
    "conn_side": "A",             # piece qui porte le pin
    "conn_radius": 0.0,           # 0 = auto
    "conn_length": 0.0,           # 0 = auto (2 x rayon)
    "conn_clearance": 0.02,       # jeu, en unites de scene
    "conn_position": -1.0,        # 0..1 le long du trait, -1 = auto
}
_draw_state = {"meshes": [], "points": [], "camera": None, "curve": None}


def _warn(msg):
    cmds.warning("[CurveCut] " + msg)


def _info(msg):
    om.MGlobal.displayInfo("[CurveCut] " + msg)


def active_camera():
    panel = cmds.getPanel(withFocus=True)
    if not panel or cmds.getPanel(typeOf=panel) != "modelPanel":
        try:
            editor = cmds.playblast(activeEditor=True)
            return cmds.modelEditor(editor, q=True, camera=True)
        except Exception:
            return "persp"
    return cmds.modelPanel(panel, q=True, camera=True)


def projector_from_camera(cam):
    if cmds.nodeType(cam) == "camera":
        shape = cam
        cam = cmds.listRelatives(cam, parent=True, fullPath=True)[0]
    else:
        shape = cmds.listRelatives(cam, shapes=True, type="camera", fullPath=True)[0]
    m = cmds.xform(cam, q=True, worldSpace=True, matrix=True)
    ortho = cmds.camera(shape, q=True, orthographic=True)
    return ViewProjector(m, ortho)


def _shape_of(node, typ):
    if cmds.nodeType(node) == typ:
        return node
    shapes = cmds.listRelatives(node, shapes=True, type=typ, fullPath=True, noIntermediate=True)
    return shapes[0] if shapes else None


def _mesh_points(mesh):
    """Coins de la bounding box monde (suffisant pour cadrer le cutter)."""
    bb = cmds.exactWorldBoundingBox(mesh)
    xs, ys, zs = (bb[0], bb[3]), (bb[1], bb[4]), (bb[2], bb[5])
    return [(x, y, z) for x in xs for y in ys for z in zs]


def sample_curve(curve, count=400):
    """Points monde echantillonnes en longueur + booleen 'fermee'."""
    shape = _shape_of(curve, "nurbsCurve")
    sel = om.MSelectionList()
    sel.add(shape)
    fn = om.MFnNurbsCurve(sel.getDagPath(0))
    length = fn.length()
    closed = fn.form in (om.MFnNurbsCurve.kClosed, om.MFnNurbsCurve.kPeriodic)
    pts = []
    for i in range(count):
        param = fn.findParamFromLength(length * i / (count - 1))
        p = fn.getPointAtParam(param, om.MSpace.kWorld)
        pts.append((p.x, p.y, p.z))
    return pts, closed


def stored_projector(curve):
    if cmds.attributeQuery(VIEW_ATTR, node=curve, exists=True):
        try:
            return ViewProjector.from_dict(json.loads(cmds.getAttr(curve + "." + VIEW_ATTR)))
        except Exception:
            pass
    return None


def store_projector(curve, projector):
    if not cmds.attributeQuery(VIEW_ATTR, node=curve, exists=True):
        cmds.addAttr(curve, longName=VIEW_ATTR, dataType="string")
    cmds.setAttr(curve + "." + VIEW_ATTR, json.dumps(projector.to_dict()), type="string")


def create_mesh(verts, faces, name):
    points = om.MPointArray([om.MPoint(*v) for v in verts])
    counts = [len(f) for f in faces]
    connects = [i for f in faces for i in f]
    obj = om.MFnMesh().create(points, counts, connects)
    path = om.MFnDagNode(obj).fullPathName()
    path = cmds.rename(path, name)
    cmds.sets(path, edit=True, forceElement="initialShadingGroup")
    return path


def _face_count(mesh):
    try:
        n = cmds.polyEvaluate(mesh, face=True)
        return n if isinstance(n, int) else 0
    except Exception:
        return 0


def _boolean(mesh, cutter, op, name):
    """mesh (op) cutter sur des copies ; retourne le transform resultat."""
    a = cmds.duplicate(mesh, name=name + "_tmpA")[0]
    b = cmds.duplicate(cutter, name=name + "_tmpB")[0]
    code = {"union": 1, "difference": 2, "intersect": 3}[op]
    res = cmds.polyCBoolOp(a, b, operation=code, constructionHistory=False, name=name)[0]
    for n in (a, b):
        if cmds.objExists(n):
            cmds.delete(n)
    return res


def _mesh_ray_hits(mesh):
    """Fonction ray_hits(origine, direction) sur le mesh Maya (monde)."""
    sel = om.MSelectionList()
    sel.add(_shape_of(mesh, "mesh"))
    fn = om.MFnMesh(sel.getDagPath(0))
    accel = fn.autoUniformGridParams()

    def hits(origin, direction):
        try:
            res = fn.allIntersections(om.MFloatPoint(*origin), om.MFloatVector(*direction),
                                      om.MSpace.kWorld, 1e9, False,
                                      None, None, False, accel, 1e-6, True)
        except Exception:
            return []
        if not res or res[0] is None:
            return []
        return sorted(float(t) for t in res[1])
    return hits


def add_connector(mesh, part_a, part_b, info, projector, shape="cylinder",
                  side="A", radius=0.0, length=0.0, clearance=0.02,
                  position=-1.0):
    """Ajoute un pin sur une piece et le socket correspondant sur l'autre.
    Retourne (part_a, part_b) eventuellement renommes, ou None."""
    hits = _mesh_ray_hits(mesh)
    spot = find_connector_spot(info["curve_uv"], projector, hits,
                               info["d_near"], info["d_far"],
                               position=None if position < 0 else position)
    if not spot:
        _warn("Pas de place trouvee pour le connecteur.")
        return None
    plan = plan_connector(spot, radius=radius, length=length, gap=info["gap"],
                          clearance=clearance, shape=shape, pin_side=side,
                          ray_hits=hits)
    if not plan:
        _warn("Pas assez de matiere pour un connecteur a cet endroit.")
        return None
    if plan["warning"]:
        _warn(plan["warning"])
    own, other = (part_a, part_b) if side == "A" else (part_b, part_a)
    short_own, short_other = own.split("|")[-1], other.split("|")[-1]
    pin = create_mesh(plan["pin"][0], plan["pin"][1], short_own + "_pinTool")
    sock = create_mesh(plan["socket"][0], plan["socket"][1], short_other + "_socketTool")
    new_own = _boolean(own, pin, "union", short_own + "_pin")
    new_other = _boolean(other, sock, "difference", short_other + "_socket")
    cmds.delete(pin, sock, own, other)
    new_own = cmds.rename(new_own, short_own)
    new_other = cmds.rename(new_other, short_other)
    _info("Connecteur : rayon %.3g, longueur %.3g, jeu %.3g (position %.2f)."
          % (plan["radius"], plan["length"], clearance, spot["t"]))
    return (new_own, new_other) if side == "A" else (new_other, new_own)


def cut_mesh(mesh, curve_points, projector, gap=0.0, samples=200, closed=None,
             keep_original=True, keep_cutters=False, connector=False,
             conn_shape="cylinder", conn_side="A", conn_radius=0.0,
             conn_length=0.0, conn_clearance=0.02, conn_position=-1.0):
    """Coupe `mesh` (transform) selon les points monde de la courbe."""
    short = mesh.split("|")[-1]
    cutters, info = plan_cutters(curve_points, _mesh_points(mesh), projector,
                                 gap=gap, samples=samples, closed=closed,
                                 return_info=True)
    parts, cutter_nodes = [], []
    for label, op, verts, faces in cutters:
        cutter = create_mesh(verts, faces, "%s_cutter_%s" % (short, label))
        cutter_nodes.append(cutter)
        part = _boolean(mesh, cutter, op, "%s_part%s" % (short, label))
        if _face_count(part) == 0:
            cmds.delete(part)
            continue
        parts.append(part)
    if keep_cutters:
        grp = cmds.group(cutter_nodes, name=short + "_cutters_grp")
        cmds.setAttr(grp + ".visibility", 0)
    else:
        cmds.delete(cutter_nodes)
    if len(parts) < 2:
        _warn("%s : la courbe ne traverse pas le mesh dans cette vue." % short)
        if parts:
            cmds.delete(parts)
        return []
    if connector:
        res = add_connector(mesh, parts[0], parts[1], info, projector,
                            shape=conn_shape, side=conn_side, radius=conn_radius,
                            length=conn_length, clearance=conn_clearance,
                            position=conn_position)
        if res:
            parts = list(res)
    grp = cmds.group(parts, name=short + "_cut_grp")
    if keep_original:
        cmds.setAttr(mesh + ".visibility", 0)
    else:
        cmds.delete(mesh)
    _info("%s coupe en %d pieces (%s)." % (short, len(parts), grp))
    return [grp + "|" + p.split("|")[-1] for p in parts]


def _selected_meshes(nodes):
    return [n for n in nodes if _shape_of(n, "mesh")]


def cut_with_curve(curve, meshes, projector=None, **kw):
    projector = projector or stored_projector(curve) or projector_from_camera(active_camera())
    pts, closed = sample_curve(curve)
    results = []
    cmds.undoInfo(openChunk=True, chunkName="curveCut")
    try:
        for mesh in meshes:
            results += cut_mesh(mesh, pts, projector, closed=True if closed else None, **kw)
    finally:
        cmds.undoInfo(closeChunk=True)
    if results:
        cmds.select(results)
    return results


def cut_selected(**kw):
    """Selection : mesh(es) + une courbe NURBS."""
    opts = dict(_settings)
    opts.update(kw)
    keep_curve = opts.pop("keep_curve")
    sel = cmds.ls(selection=True, long=True, type="transform") or []
    curves = [n for n in sel if _shape_of(n, "nurbsCurve")]
    meshes = _selected_meshes(sel)
    if not curves or not meshes:
        _warn("Selectionner un ou plusieurs meshes puis une courbe.")
        return []
    res = cut_with_curve(curves[-1], meshes, **opts)
    if res and not keep_curve:
        cmds.delete(curves[-1])
    return res


# --- outil de dessin ---------------------------------------------------------

def _ctx_point():
    return cmds.draggerContext(CONTEXT, q=True, dragPoint=True)


def _on_press():
    panel = cmds.getPanel(withFocus=True)
    cam = None
    if panel and cmds.getPanel(typeOf=panel) == "modelPanel":
        cam = cmds.modelPanel(panel, q=True, camera=True)
    _draw_state["camera"] = cam or active_camera()
    _draw_state["points"] = [tuple(_ctx_point()[:3])]
    if cmds.objExists(PREVIEW_CURVE):
        cmds.delete(PREVIEW_CURVE)
    _draw_state["curve"] = None


def _on_drag():
    p = tuple(_ctx_point()[:3])
    pts = _draw_state["points"]
    if pts and sum((a - b) ** 2 for a, b in zip(p, pts[-1])) < 1e-10:
        return
    pts.append(p)
    if len(pts) < 2:
        return
    if _draw_state["curve"] and cmds.objExists(_draw_state["curve"]):
        cmds.curve(_draw_state["curve"], replace=True, degree=1, point=pts)
    else:
        _draw_state["curve"] = cmds.curve(degree=1, point=pts, name=PREVIEW_CURVE)
    cmds.refresh(currentView=True)


def _on_release():
    pts = _draw_state["points"]
    curve = _draw_state["curve"]
    meshes = [m for m in _draw_state["meshes"] if cmds.objExists(m)]
    if len(pts) < 3 or not curve:
        _warn("Trait trop court.")
        return
    projector = projector_from_camera(_draw_state["camera"])
    curve = cmds.rename(curve, "curveCut_curve#")
    store_projector(curve, projector)
    if not meshes:
        _warn("Aucun mesh memorise : courbe gardee (%s)." % curve)
        return
    opts = dict(_settings)
    keep_curve = opts.pop("keep_curve")
    cut_with_curve(curve, meshes, projector=projector, **opts)
    if keep_curve:
        cmds.setAttr(curve + ".visibility", 0)
    else:
        cmds.delete(curve)
    # on reste dans l'outil pour enchainer : on recoupe les nouvelles pieces
    # selectionnees au prochain trait.
    sel = cmds.ls(selection=True, long=True, type="transform") or []
    _draw_state["meshes"] = _selected_meshes(sel) or meshes


def draw_cut_tool():
    """Active l'outil : cliquer-glisser dans le viewport pour couper les
    meshes selectionnes. Echap / Q pour sortir."""
    sel = cmds.ls(selection=True, long=True, type="transform") or []
    meshes = _selected_meshes(sel)
    if not meshes:
        _warn("Selectionner au moins un mesh avant de dessiner.")
        return
    _draw_state["meshes"] = meshes
    if cmds.draggerContext(CONTEXT, exists=True):
        cmds.deleteUI(CONTEXT)
    cmds.draggerContext(CONTEXT, pressCommand=_on_press, dragCommand=_on_drag,
                        releaseCommand=_on_release, cursor="crossHair",
                        projection="viewPlane", space="world", undoMode="step")
    cmds.setToolTo(CONTEXT)
    _info("Cliquer-glisser dans le viewport pour couper %d mesh(es)." % len(meshes))


# --- interface ---------------------------------------------------------------

def _read_ui():
    _settings["gap"] = cmds.floatFieldGrp("ccGap", q=True, value1=True)
    _settings["samples"] = cmds.intFieldGrp("ccSamples", q=True, value1=True)
    _settings["keep_original"] = cmds.checkBox("ccKeepOrig", q=True, value=True)
    _settings["keep_cutters"] = cmds.checkBox("ccKeepCutters", q=True, value=True)
    _settings["keep_curve"] = cmds.checkBox("ccKeepCurve", q=True, value=True)
    _settings["connector"] = cmds.checkBox("ccConn", q=True, value=True)
    _settings["conn_shape"] = ("cylinder", "square")[
        cmds.optionMenuGrp("ccConnShape", q=True, select=True) - 1]
    _settings["conn_side"] = ("A", "B")[cmds.radioButtonGrp("ccConnSide", q=True, select=True) - 1]
    _settings["conn_radius"] = cmds.floatFieldGrp("ccConnRadius", q=True, value1=True)
    _settings["conn_length"] = cmds.floatFieldGrp("ccConnLength", q=True, value1=True)
    _settings["conn_clearance"] = cmds.floatFieldGrp("ccConnClear", q=True, value1=True)
    auto = cmds.checkBox("ccConnAuto", q=True, value=True)
    _settings["conn_position"] = -1.0 if auto else cmds.floatSliderGrp("ccConnPos", q=True, value=True)


def _toggle_connector_ui(*_):
    on = cmds.checkBox("ccConn", q=True, value=True)
    cmds.frameLayout("ccConnFrame", e=True, enable=on)
    auto = cmds.checkBox("ccConnAuto", q=True, value=True)
    cmds.floatSliderGrp("ccConnPos", e=True, enable=on and not auto)


def show_ui():
    if cmds.window(WINDOW, exists=True):
        cmds.deleteUI(WINDOW)
    cmds.window(WINDOW, title="Curve Cut %s" % __version__, widthHeight=(330, 470))
    cmds.columnLayout(adjustableColumn=True, rowSpacing=6, columnAttach=("both", 8))
    cmds.text(label="Selectionner le(s) mesh(es) ferme(s), puis :", align="left")
    cmds.button(label="Dessiner une decoupe", height=34,
                command=lambda *_: (_read_ui(), draw_cut_tool()))
    cmds.button(label="Couper avec la courbe selectionnee", height=28,
                command=lambda *_: (_read_ui(), cut_selected()))
    cmds.frameLayout(label="Coupe", collapsable=True, marginWidth=4, marginHeight=4)
    cmds.columnLayout(adjustableColumn=True, rowSpacing=4)
    cmds.floatFieldGrp("ccGap", label="Ecart (gap)", value1=_settings["gap"],
                       precision=3, columnWidth2=(120, 80))
    cmds.intFieldGrp("ccSamples", label="Precision courbe", value1=_settings["samples"],
                     columnWidth2=(120, 80))
    cmds.checkBox("ccKeepOrig", label="Garder l'original (cache)", value=_settings["keep_original"])
    cmds.checkBox("ccKeepCurve", label="Garder la courbe (cachee)", value=_settings["keep_curve"])
    cmds.checkBox("ccKeepCutters", label="Garder les cutters (debug)", value=_settings["keep_cutters"])
    cmds.setParent("..")
    cmds.setParent("..")
    cmds.checkBox("ccConn", label="Ajouter un connecteur pin / socket",
                  value=_settings["connector"], changeCommand=_toggle_connector_ui)
    cmds.frameLayout("ccConnFrame", label="Connecteur", collapsable=True,
                     marginWidth=4, marginHeight=4)
    cmds.columnLayout(adjustableColumn=True, rowSpacing=4)
    cmds.optionMenuGrp("ccConnShape", label="Forme", columnWidth2=(120, 120))
    cmds.menuItem(label="Cylindre")
    cmds.menuItem(label="Carre")
    cmds.optionMenuGrp("ccConnShape", e=True,
                       select=1 if _settings["conn_shape"] == "cylinder" else 2)
    cmds.radioButtonGrp("ccConnSide", label="Pin sur la piece", labelArray2=("A", "B"),
                        numberOfRadioButtons=2, columnWidth3=(120, 50, 50),
                        select=1 if _settings["conn_side"] == "A" else 2)
    cmds.floatFieldGrp("ccConnRadius", label="Rayon (0 = auto)", value1=_settings["conn_radius"],
                       precision=3, columnWidth2=(120, 80))
    cmds.floatFieldGrp("ccConnLength", label="Longueur (0 = auto)", value1=_settings["conn_length"],
                       precision=3, columnWidth2=(120, 80))
    cmds.floatFieldGrp("ccConnClear", label="Jeu", value1=_settings["conn_clearance"],
                       precision=3, columnWidth2=(120, 80))
    cmds.checkBox("ccConnAuto", label="Position automatique (zone la plus epaisse)",
                  value=_settings["conn_position"] < 0, changeCommand=_toggle_connector_ui)
    cmds.floatSliderGrp("ccConnPos", label="Position le long du trait", field=True,
                        minValue=0.0, maxValue=1.0, value=max(_settings["conn_position"], 0.5),
                        columnWidth3=(120, 50, 100))
    cmds.setParent("..")
    cmds.setParent("..")
    _toggle_connector_ui()
    cmds.showWindow(WINDOW)
