"""ma2glb : lit une scène Maya ASCII (.ma) du rig Ork (ORK_V3 et suivants) sans Maya, et en sort le GLB riggé de
PlasmaBuilder (web/plasma-builder/assets/ork_boy.glb).

Ce qui est lu dans le .ma :
  - le squelette (joints sous SKELETON, matrices de bind du skinCluster ou .bps) ;
  - le basemesh low du corps (shape Orig du skinCluster) avec ses UV et ses poids ;
  - la tête low, les yeux et les dents (tout ce qui est sous le groupe HEAD), rigides sur le joint « head »
    (comme la parentConstraint HEAD_follow_head du rig).
Les high-poly viennent du dossier des OBJ ZBrush (même espace que les low) :
  - BODY.obj : poids transférés du low par point le plus proche sur la surface (interpolation barycentrique) ;
  - HEAD.obj : rigide sur « head ».
Les blendShapes du low (correctifs) sont écrits en morph targets s'il y en a, et reportés sur le high par le
même transfert barycentrique.

    python ma2glb.py ORK_V3.ma "HIGHT MESH" ork_boy.glb

Pour une mise à jour : réexporter les OBJ high depuis ZBrush (mêmes noms) et/ou sauver le .ma, relancer.
Dépendances : numpy, scipy.
"""
import argparse, json, math, os, re, struct, sys, time
import numpy as np

SKIP_ROOTS = {'TEXTURE', 'PLASMA_Info', 'Riffle_001_PLASMA_Info', 'persp', 'top', 'front', 'side'}


# ---------------------------------------------------------------- lecture .ma
class MaScene:
    """Nœuds (type, parent, attributs bruts) et connexions d'un .ma. Les nœuds sous SKIP_ROOTS sont ignorés
    (le maillage TEXTURE de la scène fait à lui seul la majorité du fichier)."""

    def __init__(self, path):
        self.nodes = {}
        self.conns = []
        skipped = set()
        cur = None
        buf = []
        with open(path, 'r', encoding='latin1') as f:
            for line in f:
                buf.append(line)
                if not line.rstrip().endswith(';'):
                    continue
                st = ''.join(buf).strip()
                buf = []
                if st.startswith('createNode '):
                    m = re.match(r'createNode (\w+)(.*?)-n "([^"]+)"(?: -p "([^"]+)")?', st)
                    typ, _, name, parent = m.groups()
                    if name in SKIP_ROOTS or (parent and parent.split('|')[-1] in skipped):
                        skipped.add(name)
                        cur = None
                        continue
                    cur = dict(type=typ, name=name, parent=parent.split('|')[-1] if parent else None, attrs=[])
                    self.nodes[name] = cur
                elif st.startswith('setAttr ') and cur is not None:
                    cur['attrs'].append(st)
                elif st.startswith('connectAttr '):
                    m = re.match(r'connectAttr "([^"]+)" "([^"]+)"', st)
                    if m:
                        self.conns.append(m.groups())
                    cur = None
                elif not st.startswith(('setAttr', 'addAttr', 'rename', 'lockNode')):
                    cur = None

    def node(self, name):
        return self.nodes[name]

    def children(self, name, typ=None):
        return [n for n in self.nodes.values() if n['parent'] == name and (typ is None or n['type'] == typ)]


SET_RE = re.compile(r'setAttr\s+((?:-\w+\s+(?:\d+|on|off|yes|no)\s+)*)"([^"]+)"\s*(?:-type\s+"([^"]+)")?(.*);\s*$', re.S)


def parse_set(st):
    m = SET_RE.match(st)
    if not m:
        return None
    return m.group(2), m.group(3), m.group(4)


def attr_values(node, name, n=None):
    """Valeurs numériques du setAttr exact « .name »."""
    for st in node['attrs']:
        p = parse_set(st)
        if p and p[0] == '.' + name:
            v = np.array(p[2].split(), float) if p[2].strip() else None
            return v
    return None


def ranged(node, prefix, width, size=None):
    """Assemble les setAttr « .prefix[a:b] » (et « .prefix[a] ») en un tableau (n, width)."""
    rx = re.compile(r'^\.' + re.escape(prefix) + r'\[(\d+)(?::(\d+))?\]$')
    chunks, hi = [], -1
    for st in node['attrs']:
        p = parse_set(st)
        if not p:
            continue
        m = rx.match(p[0])
        if not m:
            continue
        a = int(m.group(1)); b = int(m.group(2)) if m.group(2) else a
        chunks.append((a, b, p[2]))
        hi = max(hi, b)
    n = size if size is not None else hi + 1
    out = np.zeros((n, width))
    for a, b, txt in chunks:
        v = np.array(txt.split(), float)
        out[a:b + 1] = v.reshape(b - a + 1, width)
    return out


def decode_mesh(node, tweaks=True):
    """Sommets, triangles (indices de sommets) et UV par coin (indices dans le tableau d'UV)."""
    P = ranged(node, 'vt', 3)
    if tweaks:
        pt = ranged(node, 'pt', 3)
        if len(pt):
            P[:len(pt)] += pt[:len(P)]
    E = ranged(node, 'ed', 3).astype(np.int64)[:, :2]
    UV = ranged(node, 'uvst[0].uvsp', 2)
    fv, fu = [], []
    for st in node['attrs']:
        p = parse_set(st)
        if not p or not p[0].startswith('.fc['):
            continue
        tok = p[2].split()
        i = 0
        while i < len(tok):
            t = tok[i]
            if t == 'f':
                n = int(tok[i + 1]); e = [int(x) for x in tok[i + 2:i + 2 + n]]
                fv.append([E[x, 0] if x >= 0 else E[-x - 1, 1] for x in e]); fu.append(None)
                i += 2 + n
            elif t == 'mu':
                n = int(tok[i + 2])
                if tok[i + 1] == '0':
                    fu[-1] = [int(x) for x in tok[i + 3:i + 3 + n]]
                i += 3 + n
            elif t in ('h', 'mc', 'mf'):
                n = int(tok[i + 1]) if t == 'h' else int(tok[i + 2])
                i += (2 if t == 'h' else 3) + n
            else:
                i += 1
    tv, tu = [], []
    for v, u in zip(fv, fu):
        u = u or [0] * len(v)
        for k in range(1, len(v) - 1):
            tv.append([v[0], v[k], v[k + 1]]); tu.append([u[0], u[k], u[k + 1]])
    return P, np.array(tv, np.int64), UV, np.array(tu, np.int64)


def read_obj(path):
    P, UV, tv, tu = [], [], [], []
    with open(path) as f:
        for line in f:
            if line.startswith('v '):
                P.append(line.split()[1:4])
            elif line.startswith('vt '):
                UV.append(line.split()[1:3])
            elif line.startswith('f '):
                c = [x.split('/') for x in line.split()[1:]]
                v = [int(x[0]) - 1 for x in c]
                u = [int(x[1]) - 1 if len(x) > 1 and x[1] else 0 for x in c]
                for k in range(1, len(v) - 1):
                    tv.append([v[0], v[k], v[k + 1]]); tu.append([u[0], u[k], u[k + 1]])
    return np.array(P, float), np.array(tv, np.int64), np.array(UV, float), np.array(tu, np.int64)


# ---------------------------------------------------------------- transforms Maya (matrices 4x4, colonnes)
def T(t):
    M = np.eye(4); M[:3, 3] = t; return M


def euler_xyz(r):
    cx, cy, cz = np.cos(r); sx, sy, sz = np.sin(r)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    M = np.eye(4); M[:3, :3] = Z @ Y @ X; return M


def local_matrix(node, t=None, r=None):
    def g(k, dfl):
        v = attr_values(node, k)
        return v if v is not None else np.array(dfl, float)
    t = g('t', [0, 0, 0]) if t is None else t
    r = np.radians(g('r', [0, 0, 0])) if r is None else r
    s = g('s', [1, 1, 1])
    rp, sp, rpt, spt = g('rp', [0] * 3), g('sp', [0] * 3), g('rpt', [0] * 3), g('spt', [0] * 3)
    return T(t) @ T(rp + rpt) @ euler_xyz(r) @ T(-rp) @ T(sp + spt) @ np.diag([*s, 1]) @ T(-sp)


def mat16(v):
    return v.reshape(4, 4).T


# ---------------------------------------------------------------- transfert low -> high
def closest_on_tris(p, a, b, c):
    """Point le plus proche sur des triangles (Ericson), vectorisé : barycentriques et distance²."""
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = (ab * ap).sum(-1), (ac * ap).sum(-1)
    bp = p - b; d3, d4 = (ab * bp).sum(-1), (ac * bp).sum(-1)
    cp = p - c; d5, d6 = (ab * cp).sum(-1), (ac * cp).sum(-1)
    va, vb, vc = d3 * d6 - d5 * d4, d5 * d2 - d1 * d6, d1 * d4 - d3 * d2
    safe = lambda x: np.where(np.abs(x) < 1e-20, 1e-20, x)
    den = safe(va + vb + vc)
    v, w = vb / den, vc / den
    bary = np.c_[1 - v - w, v, w]
    done = (d1 <= 0) & (d2 <= 0); bary[done] = [1, 0, 0]
    m = (d3 >= 0) & (d4 <= d3) & ~done; bary[m] = [0, 1, 0]; done |= m
    m = (d6 >= 0) & (d5 <= d6) & ~done; bary[m] = [0, 0, 1]; done |= m
    t = d1 / safe(d1 - d3); m = (vc <= 0) & (d1 >= 0) & (d3 <= 0) & ~done; bary[m] = np.c_[1 - t, t, 0 * t][m]; done |= m
    t = d2 / safe(d2 - d6); m = (vb <= 0) & (d2 >= 0) & (d6 <= 0) & ~done; bary[m] = np.c_[1 - t, 0 * t, t][m]; done |= m
    t = (d4 - d3) / safe((d4 - d3) + (d5 - d6)); m = (va <= 0) & (d4 - d3 >= 0) & (d5 - d6 >= 0) & ~done; bary[m] = np.c_[0 * t, 1 - t, t][m]
    q = bary[:, :1] * a + bary[:, 1:2] * b + bary[:, 2:] * c
    return bary, ((p - q) ** 2).sum(-1)


def closest_map(Pt, Ps, Ts, K=12, chunk=50000):
    """Pour chaque point cible : triangle source le plus proche et barycentriques."""
    from scipy.spatial import cKDTree
    tree = cKDTree(Ps[Ts].mean(1))
    tri = np.zeros(len(Pt), np.int64); bary = np.zeros((len(Pt), 3)); dist = np.zeros(len(Pt))
    for s in range(0, len(Pt), chunk):
        p = Pt[s:s + chunk]
        _, nn = tree.query(p, K)
        best = np.full(len(p), np.inf); bb = np.zeros((len(p), 3)); bt = np.zeros(len(p), np.int64)
        for k in range(K):
            t = Ts[nn[:, k]]
            b, d = closest_on_tris(p, Ps[t[:, 0]], Ps[t[:, 1]], Ps[t[:, 2]])
            m = d < best; best[m] = d[m]; bb[m] = b[m]; bt[m] = nn[m, k]
        tri[s:s + chunk] = bt; bary[s:s + chunk] = bb; dist[s:s + chunk] = np.sqrt(best)
    return tri, bary, dist


def interp(values, Ts, tri, bary):
    t = Ts[tri]
    return bary[:, :1] * values[t[:, 0]] + bary[:, 1:2] * values[t[:, 1]] + bary[:, 2:] * values[t[:, 2]]


# ---------------------------------------------------------------- géométrie finale (sommets séparés aux coutures d'UV)
def split_uv(P, Tv, UV, Tu):
    """Un sommet par paire (position, UV) ; normales lisses calculées sur les positions (pas de couture d'ombrage)."""
    key = Tv.reshape(-1) * (len(UV) + 1) + Tu.reshape(-1)
    uniq, inv = np.unique(key, return_inverse=True)
    vi = uniq // (len(UV) + 1); ui = uniq % (len(UV) + 1)
    fn = np.cross(P[Tv[:, 1]] - P[Tv[:, 0]], P[Tv[:, 2]] - P[Tv[:, 0]])
    N = np.zeros_like(P)
    for k in range(3):
        np.add.at(N, Tv[:, k], fn)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-20)
    return vi, UV[ui], N[vi], inv.reshape(-1, 3)


# ---------------------------------------------------------------- GLB
def quat(R):
    tr = R[0, 0] + R[1, 1] + R[2, 2]
    if tr > 0:
        S = math.sqrt(tr + 1) * 2; q = [(R[2, 1] - R[1, 2]) / S, (R[0, 2] - R[2, 0]) / S, (R[1, 0] - R[0, 1]) / S, S / 4]
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        S = math.sqrt(1 + R[0, 0] - R[1, 1] - R[2, 2]) * 2; q = [S / 4, (R[0, 1] + R[1, 0]) / S, (R[0, 2] + R[2, 0]) / S, (R[2, 1] - R[1, 2]) / S]
    elif R[1, 1] > R[2, 2]:
        S = math.sqrt(1 + R[1, 1] - R[0, 0] - R[2, 2]) * 2; q = [(R[0, 1] + R[1, 0]) / S, S / 4, (R[1, 2] + R[2, 1]) / S, (R[0, 2] - R[2, 0]) / S]
    else:
        S = math.sqrt(1 + R[2, 2] - R[0, 0] - R[1, 1]) * 2; q = [(R[0, 2] + R[2, 0]) / S, (R[1, 2] + R[2, 1]) / S, S / 4, (R[1, 0] - R[0, 1]) / S]
    q = np.array(q); return (q / np.linalg.norm(q)).tolist()


def trs(M):
    s = np.linalg.norm(M[:3, :3], axis=0)
    return dict(translation=M[:3, 3].tolist(), rotation=quat(M[:3, :3] / s), scale=s.tolist())


def top4(W):
    """4 influences max par sommet, en octets normalisés dont la somme fait exactement 255."""
    idx = np.argsort(-W, axis=1)[:, :4]
    w = np.take_along_axis(W, idx, 1)
    w = w / np.maximum(w.sum(1, keepdims=True), 1e-12)
    q = np.round(w * 255).astype(np.int32)
    q[:, 0] += 255 - q.sum(1)
    idx[q == 0] = 0
    return idx.astype(np.uint8), q.astype(np.uint8)


def write_glb(path, joints, parents, bind, meshes, extras):
    buf = bytearray(); views = []; accs = []

    def add(a, target, comp, typ, minmax=False, normalized=False):
        while len(buf) % 4: buf.append(0)
        b = np.ascontiguousarray(a).tobytes()
        v = dict(buffer=0, byteOffset=len(buf), byteLength=len(b))
        if target: v['target'] = target
        views.append(v); buf.extend(b)
        acc = dict(bufferView=len(views) - 1, componentType=comp, count=len(a), type=typ)
        if normalized: acc['normalized'] = True
        if minmax: acc['min'] = a.min(0).tolist(); acc['max'] = a.max(0).tolist()
        accs.append(acc); return len(accs) - 1

    nodes = []
    for i, n in enumerate(joints):
        p = parents[i]
        L = bind[i] if p < 0 else np.linalg.inv(bind[p]) @ bind[i]
        nodes.append(dict(name=n, **trs(L)))
        ch = [j for j in range(len(joints)) if parents[j] == i]
        if ch: nodes[i]['children'] = ch
    ibm = add(np.stack([np.linalg.inv(m).T.reshape(-1) for m in bind]).astype(np.float32), None, 5126, 'MAT4')
    root = parents.index(-1)
    mats = []
    gm, mesh_nodes = [], []
    for m in meshes:
        j, w = top4(m['W'])
        attrs = dict(POSITION=add(m['P'].astype(np.float32), 34962, 5126, 'VEC3', True),
                     NORMAL=add(m['N'].astype(np.float32), 34962, 5126, 'VEC3'),
                     TEXCOORD_0=add(np.c_[m['UV'][:, 0], 1 - m['UV'][:, 1]].astype(np.float32), 34962, 5126, 'VEC2'),
                     JOINTS_0=add(j, 34962, 5121, 'VEC4'), WEIGHTS_0=add(w, 34962, 5121, 'VEC4', normalized=True))
        big = m['T'].max() > 65535
        ind = add(m['T'].astype(np.uint32 if big else np.uint16).reshape(-1), 34963, 5125 if big else 5123, 'SCALAR')
        if m['mat'] not in mats: mats.append(m['mat'])
        prim = dict(attributes=attrs, indices=ind, material=mats.index(m['mat']))
        mesh = dict(name=m['name'], primitives=[prim])
        if m.get('morphs'):
            prim['targets'] = [dict(POSITION=add(d.astype(np.float32), 34962, 5126, 'VEC3', True)) for _, d in m['morphs']]
            mesh['weights'] = [0.0] * len(m['morphs'])
            mesh['extras'] = dict(targetNames=[n for n, _ in m['morphs']])
        gm.append(mesh)
        nodes.append(dict(name=m['name'], mesh=len(gm) - 1, skin=0, extras=dict(part=m['part'])))
        mesh_nodes.append(len(nodes) - 1)
    nodes.append(dict(name='ORK_BOY', children=[root] + mesh_nodes, extras=extras))
    while len(buf) % 4: buf.append(0)
    g = dict(asset=dict(version='2.0', generator='PlasmaBuilder ma2glb'), scene=0, scenes=[dict(nodes=[len(nodes) - 1])],
             nodes=nodes, meshes=gm, skins=[dict(joints=list(range(len(joints))), inverseBindMatrices=ibm, skeleton=root)],
             materials=[dict(name=n, pbrMetallicRoughness=dict(baseColorFactor=[0.56, 0.57, 0.59, 1], metallicFactor=0, roughnessFactor=0.56)) for n in mats],
             accessors=accs, bufferViews=views, buffers=[dict(byteLength=len(buf))])
    js = json.dumps(g, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(buf)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(buf), 0x004E4942)); f.write(bytes(buf))


# ---------------------------------------------------------------- scène ORK
def world_of(sc, name, over):
    n = sc.node(name)
    L = over.get(name)
    if L is None:
        L = local_matrix(n)
    return (world_of(sc, n['parent'], over) if n['parent'] in sc.nodes else np.eye(4)) @ L


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ma'); ap.add_argument('high_dir'); ap.add_argument('glb')
    ap.add_argument('--skin', default=None, help='skinCluster (par défaut : le seul de la scène)')
    a = ap.parse_args()
    t0 = time.time()
    sc = MaScene(a.ma)
    print('lu : %d nœuds, %d connexions (%.1fs)' % (len(sc.nodes), len(sc.conns), time.time() - t0))

    # ---- skinCluster : influences, bind, maillage d'origine, poids
    skn = a.skin or next(n for n, d in sc.nodes.items() if d['type'] == 'skinCluster')
    sk = sc.node(skn)
    infl = {}
    for src, dst in sc.conns:
        m = re.match(re.escape(skn) + r'\.ma\[(\d+)\]$', dst)
        if m: infl[int(m.group(1))] = src.rsplit('.', 1)[0]
    pm = {}
    for st in sk['attrs']:
        p = parse_set(st)
        m = p and re.match(r'\.pm\[(\d+)\]$', p[0])
        if m: pm[int(m.group(1))] = np.linalg.inv(mat16(np.array(p[2].split(), float)))
    geo_out = next(dst.rsplit('.', 1)[0] for src, dst in sc.conns if src == skn + '.og[0]')
    orig_src = next(src for src, dst in sc.conns if dst == skn + '.ip[0].ig')
    # remonte jusqu'à la shape Orig (groupParts entre les deux)
    node = orig_src.rsplit('.', 1)[0]
    while sc.nodes.get(node, {}).get('type') != 'mesh':
        node = next(src for src, dst in sc.conns if dst.startswith(node + '.i')).rsplit('.', 1)[0]
    body_shape = sc.node(node)
    body_xf = body_shape['parent']
    print('skin %s : %d influences, low %s' % (skn, len(infl), node))

    # ---- squelette : tous les joints sous la racine du rig ; bind = matrice du skin, sinon .bps
    joints_all = [n for n, d in sc.nodes.items() if d['type'] == 'joint']
    root_j = next(n for n in joints_all if sc.node(n)['parent'] not in joints_all)
    bps = {n: mat16(attr_values(sc.node(n), 'bps')) for n in joints_all if attr_values(sc.node(n), 'bps') is not None}
    by_name = {v: pm[k] for k, v in infl.items()}
    # la racine « root » à l'identité est sautée : le bassin devient la racine du squelette glTF
    top = root_j
    kids = [n for n in joints_all if sc.node(n)['parent'] == top]
    if len(kids) == 1 and top not in by_name and np.allclose(bps.get(top, np.eye(4)), np.eye(4)):
        top = kids[0]
    joints = []
    def visit(n):
        joints.append(n)
        for c in joints_all:
            if sc.node(c)['parent'] == n: visit(c)
    visit(top)
    parents = [joints.index(sc.node(n)['parent']) if sc.node(n)['parent'] in joints else -1 for n in joints]
    # joints sans poids (bouts, racine) : position relative à leur parent prise dans les .bps, rattachée au bind du parent
    bind = []
    for i, n in enumerate(joints):
        if n in by_name: bind.append(by_name[n]); continue
        p = joints[parents[i]] if parents[i] >= 0 else None
        if p is None or p not in bps: bind.append(bps[n]); continue
        bind.append(bind[parents[i]] @ np.linalg.inv(bps[p]) @ bps[n])
    jidx = {n: i for i, n in enumerate(joints)}
    nj = len(joints)
    head = jidx['head']

    # ---- low du corps
    P, Tv, UV, Tu = decode_mesh(body_shape, tweaks=False)
    W = np.zeros((len(P), nj))
    rx = re.compile(r'^\.wl\[(\d+)(?::(\d+))?\]\.w$')
    for st in sk['attrs']:
        p = parse_set(st)
        m = p and rx.match(p[0])
        if not m: continue
        a0 = int(m.group(1)); a1 = int(m.group(2)) if m.group(2) else a0
        tok = p[2].split(); k = 0
        for vi in range(a0, a1 + 1):
            c = int(tok[k]); k += 1
            for _ in range(c):
                W[vi, jidx[infl[int(tok[k])]]] += float(tok[k + 1]); k += 2
    Mb = world_of(sc, body_xf, {})
    P = (Mb @ np.c_[P, np.ones(len(P))].T).T[:, :3]
    body_low = (P, Tv, UV, Tu, W)
    print('BODY_low : %d sommets, %d tris' % (len(P), len(Tv)))

    # ---- groupe tête (contraint sur le joint head) : au repos, translation rst / rotation rsrr de la contrainte
    head_grp = next(n for n, d in sc.nodes.items() if d['type'] == 'parentConstraint' and any(dst == n + '.tg[0].tpm' and src.startswith('head.') for src, dst in sc.conns))
    hg = sc.node(head_grp)['parent']
    rst = attr_values(sc.node(head_grp), 'rst'); rsrr = attr_values(sc.node(head_grp), 'rsrr')
    over = {hg: local_matrix(sc.node(hg), t=rst if rst is not None else np.zeros(3), r=np.radians(rsrr) if rsrr is not None else np.zeros(3))}
    rigid = {'HEAD_low': [], 'EYES': [], 'TEETH': []}
    def walk(n):
        for c in sc.children(n):
            if c['type'] == 'mesh' and not any(st.startswith('setAttr ".io" yes') for st in c['attrs']):
                p_, tv_, uv_, tu_ = decode_mesh(c)
                M = world_of(sc, n, over)
                p_ = (M @ np.c_[p_, np.ones(len(p_))].T).T[:, :3]
                key = 'EYES' if 'EYE' in n.upper() else 'TEETH' if n.startswith('polySurface') or 'DENT' in n.upper() or 'TEETH' in n.upper() else 'HEAD_low'
                rigid[key].append((p_, tv_, uv_, tu_))
            elif c['type'] == 'transform':
                walk(c['name'])
    walk(hg)

    def merge(parts):
        Ps, Tvs, UVs, Tus, o, ou = [], [], [], [], 0, 0
        for p_, tv_, uv_, tu_ in parts:
            Ps.append(p_); Tvs.append(tv_ + o); UVs.append(uv_ if len(uv_) else np.zeros((1, 2))); Tus.append(tu_ + ou)
            o += len(p_); ou += len(UVs[-1])
        return np.vstack(Ps), np.vstack(Tvs), np.vstack(UVs), np.vstack(Tus)

    def pack(name, part, mat, P, Tv, UV, Tu, W, morphs=None):
        vi, uv, N, Tn = split_uv(P, Tv, UV, Tu)
        return dict(name=name, part=part, mat=mat, P=P[vi], N=N, UV=uv, T=Tn, W=W[vi],
                    morphs=[(n, d[vi]) for n, d in morphs] if morphs else None, _vi=vi)

    meshes = [pack('BODY_low', 'low', 'body', *body_low)]
    for key, part, mat in [('HEAD_low', 'low', 'head'), ('TEETH', 'head', 'teeth'), ('EYES', 'head', 'eyes')]:
        if not rigid[key]:
            print('  (pas de %s)' % key); continue
        P_, Tv_, UV_, Tu_ = merge(rigid[key])
        Wr = np.zeros((len(P_), nj)); Wr[:, head] = 1
        meshes.append(pack(key, part, mat, P_, Tv_, UV_, Tu_, Wr))
        print('%s : %d sommets, %d tris' % (key, len(P_), len(Tv_)))

    # ---- high-poly (OBJ ZBrush)
    Pl, Tl = body_low[0], body_low[1]
    for fname, label, mat, rigid_head in [('BODY.obj', 'BODY_high', 'body', False), ('HEAD.obj', 'HEAD_high', 'head', True)]:
        path = os.path.join(a.high_dir, fname)
        if not os.path.exists(path):
            print('  (pas de %s)' % path); continue
        t1 = time.time()
        Ph, Th, UVh, Tuh = read_obj(path)
        if rigid_head:
            Wh = np.zeros((len(Ph), nj)); Wh[:, head] = 1
        else:
            tri, bary, dist = closest_map(Ph, Pl, Tl)
            Wh = interp(body_low[4], Tl, tri, bary)
            print('  écart high/low : moyen %.3f cm, max %.3f cm' % (dist.mean(), dist.max()))
        meshes.append(pack(label, 'high', mat, Ph, Th, UVh, Tuh, Wh))
        print('%s : %d sommets, %d tris (%.1fs)' % (label, len(Ph), len(Th), time.time() - t1))

    eyes = next((m['P'] for m in meshes if m['name'] == 'EYES'), None)
    for m in meshes: m.pop('_vi')
    extras = dict(units='cm', source=os.path.basename(a.ma), rig='v3', floor=float(min(m['P'][:, 1].min() for m in meshes)),
                  eyeHeight=float(eyes[:, 1].mean()) if eyes is not None else None,
                  textures=dict(body='BODY', head='HEAD'))
    write_glb(a.glb, joints, parents, bind, meshes, extras)
    print('écrit %s (%.1f Mo, %.1fs)' % (a.glb, os.path.getsize(a.glb) / 1e6, time.time() - t0))


if __name__ == '__main__':
    main()
