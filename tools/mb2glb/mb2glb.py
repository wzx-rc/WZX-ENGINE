"""mb2glb : lit un fichier Maya binaire (.mb, IFF 64 bits) sans Maya et en sort un GLB riggé pour PlasmaBuilder.

Ce qui est lu : les meshes (sommets, faces, UV), les joints, le skinCluster (poids, preBindMatrix, influences),
les transforms et les parentConstraints (positions de repos). Ce qui est écrit :
  - le basemesh low skinné tel quel (60 influences, 4 par sommet pour le web) ;
  - la tête, les dents et les yeux, rigides sur le joint Head (comme les contraintes du rig Maya) ;
  - les high-poly (non riggés dans Maya) réduits, avec des poids transférés du low par point le plus proche sur
    la surface (interpolation barycentrique), la tête high rigide sur Head.

    python mb2glb.py BOY_32mm.mb ork_boy.glb [--body-tris 220000] [--head-tris 120000]

Dépendances : numpy, scipy, fast-simplification.
"""
import argparse, json, math, struct, sys, time
import numpy as np

GROUPS = {b'FOR8', b'LIS8', b'CAT8', b'PROP', b'FOR4'}


# ---------------------------------------------------------------- IFF Maya
def parse_iff(d, start, end):
    """Chunks : tag(4) + 4 octets + taille(8, big endian). Les groupes ont un type(4) puis leurs enfants ;
    les données sont alignées sur 8 octets à partir du début des enfants du groupe."""
    out, p, base = [], start, start
    while p + 16 <= end:
        tag = d[p:p + 4]
        size = struct.unpack('>Q', d[p + 8:p + 16])[0]
        body = p + 16
        if tag in GROUPS:
            out.append((tag, d[body:body + 4], parse_iff(d, body + 4, body + size)))
            p = body + size
        else:
            out.append((tag, None, (body, size)))
            p = base + (body + size - base + 7) // 8 * 8
    return out


class MayaFile:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        self.nodes = []      # (type, name, parent, [(chunk, attr, data)])
        self.conns = []      # (source plug, destination plug)
        for tag, typ, kids in parse_iff(d, 0, len(d))[0][2]:
            if not isinstance(kids, list):
                continue
            if typ == b'CONS':
                for _, _, conn in kids:
                    for _, _, (b, s) in conn:
                        plugs = [x.decode('latin1') for x in d[b:b + s].split(b'\0') if x and x[0] > 1]
                        if len(plugs) >= 2:
                            self.conns.append((plugs[0], plugs[1]))
                continue
            name = parent = None
            attrs = []
            for t2, typ2, k2 in kids:
                if typ2 is not None:
                    continue
                b, s = k2
                raw = d[b:b + s]
                if t2 == b'CREA':
                    parts = raw[1:].split(b'\0')
                    name = parts[0].decode('latin1')
                    parent = parts[1].decode('latin1') if len(parts) > 2 and parts[1] else None
                else:
                    i = raw.find(b'\0')
                    i = len(raw) if i < 0 else i
                    attrs.append((t2.decode('latin1'), raw[:i].decode('latin1'), raw[i + 1:]))
            self.nodes.append((typ.decode('latin1'), name, parent, attrs))

    def node(self, name, typ=None, parent_has=None):
        for t, n, p, a in self.nodes:
            if n == name and (typ is None or t == typ) and (parent_has is None or (p and parent_has in p)):
                return t, n, p, a
        raise KeyError(name)


def attr(a, name):
    for t, n, v in a:
        if n == name:
            return v
    return None


def dbl(v, n):  # 1 octet de drapeaux puis n doubles big endian
    return np.array(struct.unpack('>%dd' % n, v[1:1 + 8 * n]))


def decode_mesh(v):
    """Données de mesh Maya : sommets (float), arêtes (paires, bit 31 = arête douce), faces (indices d'arêtes,
    bit 31 = arête inversée, bits 29-30 = dernière arête de la face), normales, puis UV et indices UV."""
    p = 1

    def arr(dt):
        nonlocal p
        c = struct.unpack('>I', v[p:p + 4])[0]
        p += 4
        a = np.frombuffer(v, dtype=dt, count=c, offset=p)
        p += c * 4
        return a
    P = arr('>f4').astype(np.float64).reshape(-1, 3)
    E = (arr('>u4').astype(np.int64) & 0x7FFFFFFF).reshape(-1, 2)
    F = arr('>u4').astype(np.int64)
    arr('>f4')
    idx = F & 0x1FFFFFFF
    fv = np.where((F >> 31) & 1 == 1, E[idx, 1], E[idx, 0])
    ends = np.nonzero(((F >> 29) & 3) == 3)[0]
    starts = np.r_[0, ends[:-1] + 1]
    counts = ends - starts + 1
    tris = []
    for k in range(1, counts.max() - 1):
        s = starts[counts > k + 1]
        tris.append(np.c_[s, s + k, s + k + 1])
    return P, fv[np.concatenate(tris)]


# ---------------------------------------------------------------- transforms Maya -> matrices 4x4 (colonnes)
def T(t):
    M = np.eye(4); M[:3, 3] = t; return M


def euler_xyz(r):
    cx, cy, cz = np.cos(r); sx, sy, sz = np.sin(r)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    M = np.eye(4); M[:3, :3] = Z @ Y @ X; return M


def local_matrix(a):
    def g(k, dfl):
        v = attr(a, k)
        return dbl(v, 3) if v is not None else np.array(dfl, float)
    t, r, s = g('t', [0, 0, 0]), g('r', [0, 0, 0]), g('s', [1, 1, 1])
    rp, sp, rpt, spt = g('rp', [0] * 3), g('sp', [0] * 3), g('rpt', [0] * 3), g('spt', [0] * 3)
    return T(t) @ T(rp + rpt) @ euler_xyz(r) @ T(-rp) @ T(sp + spt) @ np.diag([*s, 1]) @ T(-sp)


def world_matrix(mf, name, overrides, parent_has=None):
    t, n, par, a = mf.node(name, 'XFRM', parent_has)
    L = overrides.get((n, par), local_matrix(a))
    if par is None:
        return L
    parts = par.split('|')
    up = '|'.join(parts[:-1]) if len(parts) > 2 else None
    return world_matrix(mf, parts[-1], overrides, up.split('|')[-1] if up else None) @ L


# ---------------------------------------------------------------- transfert de poids
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


def transfer_weights(Pt, Ps, Ts, Ws, K=12, chunk=50000):
    from scipy.spatial import cKDTree
    tree = cKDTree(Ps[Ts].mean(1))
    out = np.zeros((len(Pt), Ws.shape[1]))
    for s in range(0, len(Pt), chunk):
        p = Pt[s:s + chunk]
        _, nn = tree.query(p, K)
        best = np.full(len(p), np.inf); bb = np.zeros((len(p), 3)); bt = np.zeros(len(p), np.int64)
        for k in range(K):
            t = Ts[nn[:, k]]
            bary, d = closest_on_tris(p, Ps[t[:, 0]], Ps[t[:, 1]], Ps[t[:, 2]])
            m = d < best; best[m] = d[m]; bb[m] = bary[m]; bt[m] = nn[m, k]
        t = Ts[bt]
        out[s:s + chunk] = bb[:, :1] * Ws[t[:, 0]] + bb[:, 1:2] * Ws[t[:, 1]] + bb[:, 2:] * Ws[t[:, 2]]
    return out


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
    gm, mesh_nodes = [], []
    for label, P, Tr, W, part in meshes:
        j, w = top4(W)
        attrs = dict(POSITION=add(P.astype(np.float32), 34962, 5126, 'VEC3', True),
                     JOINTS_0=add(j, 34962, 5121, 'VEC4'), WEIGHTS_0=add(w, 34962, 5121, 'VEC4', normalized=True))
        big = Tr.max() > 65535
        ind = add(Tr.astype(np.uint32 if big else np.uint16).reshape(-1), 34963, 5125 if big else 5123, 'SCALAR')
        gm.append(dict(name=label, primitives=[dict(attributes=attrs, indices=ind, material=0)]))
        nodes.append(dict(name=label, mesh=len(gm) - 1, skin=0, extras=dict(part=part)))
        mesh_nodes.append(len(nodes) - 1)
    nodes.append(dict(name='ORK_BOY', children=[root] + mesh_nodes, extras=extras))
    while len(buf) % 4: buf.append(0)
    g = dict(asset=dict(version='2.0', generator='PlasmaBuilder mb2glb'), scene=0, scenes=[dict(nodes=[len(nodes) - 1])],
             nodes=nodes, meshes=gm, skins=[dict(joints=list(range(len(joints))), inverseBindMatrices=ibm, skeleton=root)],
             materials=[dict(name='resine', pbrMetallicRoughness=dict(baseColorFactor=[0.56, 0.57, 0.59, 1], metallicFactor=0, roughnessFactor=0.56))],
             accessors=accs, bufferViews=views, buffers=[dict(byteLength=len(buf))])
    js = json.dumps(g, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    with open(path, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 28 + len(js) + len(buf)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack('<II', len(buf), 0x004E4942)); f.write(bytes(buf))


# ---------------------------------------------------------------- BOY_32mm
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mb'); ap.add_argument('glb')
    ap.add_argument('--body-tris', type=int, default=220000)
    ap.add_argument('--head-tris', type=int, default=120000)
    a = ap.parse_args()
    t0 = time.time()
    mf = MayaFile(a.mb)
    print('lu : %d nœuds, %d connexions (%.1fs)' % (len(mf.nodes), len(mf.conns), time.time() - t0))

    # skinCluster : influences (connexions .wm -> .ma[i]), preBindMatrix, poids creux par sommet
    _, _, _, sk = mf.node('skinCluster1', 'FSCL')
    infl = {}
    for src, dst in mf.conns:
        if dst.startswith('skinCluster1.ma['):
            infl[int(dst[16:-1])] = src.rsplit('.', 1)[0]
    joints = [infl[i] for i in range(len(infl))]
    nj = len(joints)
    bind = [np.linalg.inv(dbl(attr(sk, 'pm[%d]' % i), 16).reshape(4, 4).T) for i in range(nj)]
    _, _, _, orig = mf.node('BASEMESHShapeOrig', 'DMSH')
    Pb, Tb = decode_mesh(attr(orig, 'o'))
    wl = next(v for t, n, v in sk if n.startswith('wl[') and n.endswith('.w'))
    W = np.zeros((len(Pb), nj)); p = 1
    for vi in range(len(Pb)):
        c = struct.unpack('>I', wl[p:p + 4])[0]; p += 4
        for _ in range(c):
            j, w = struct.unpack('>Id', wl[p:p + 12]); p += 12
            W[vi, j] = w

    # hiérarchie : parent Maya s'il est une influence ; les doigts sont sous des contrôleurs de main dans le rig,
    # on les rattache au joint de main du même côté.
    hand = {'Left': 'QuickRigCharacter_LeftHand', 'Right': 'QuickRigCharacter_LeftHand1'}
    parents = []
    for n in joints:
        par = mf.node(n, 'JOIN')[2]
        if par in joints: parents.append(joints.index(par))
        elif par and par.startswith('Finger_'): parents.append(joints.index(hand[par.split('_')[1]]))
        else: parents.append(-1)
    head = joints.index('QuickRigCharacter_Head')

    # tête / dents / yeux : contraints sur HEAD_ROT ; au repos, le BASEMESH de la tête est à sa position de repos
    # (rst) et OTHERS à l'identité.
    def rest_of(constraint):
        return T(dbl(attr(mf.node(constraint)[3], 'rst'), 3))
    ov = {('BASEMESH', '|ORK_BOY|GEO|HEAD_ITEM_CTRL'): rest_of('BASEMESH_parentConstraint1'), ('OTHERS', 'GEO'): np.eye(4)}
    rigid = []
    for label, shape, xf, ph in [('HEAD_low', 'HEAD_geoShape', 'HEAD_geo', None), ('TEETH', 'DENTSShape', 'DENTS', None), ('EYES', 'EYESShape', 'EYES', None)]:
        _, _, _, sa = mf.node(shape, 'DMSH')
        data = attr(sa, 'o') or attr(sa, 'ci')
        P, Tr = decode_mesh(data)
        pt = next((v for t, n, v in sa if n.startswith('pt[')), None)
        if pt is not None: P = P + np.frombuffer(pt, '>f4', offset=1).reshape(-1, 3)
        M = world_matrix(mf, xf, ov)
        P = (M @ np.c_[P, np.ones(len(P))].T).T[:, :3]
        Wr = np.zeros((len(P), nj)); Wr[:, head] = 1
        rigid.append((label, P, Tr, Wr, 'head'))

    meshes = [('BODY_low', Pb, Tb, W, 'low')] + [(l, P, Tr, Wr, 'low' if l == 'HEAD_low' else part) for l, P, Tr, Wr, part in rigid]
    import fast_simplification
    for shape, label, target, is_head in [('polySurfaceShape1', 'BODY_high', a.body_tris, False), ('polySurfaceShape2', 'HEAD_high', a.head_tris, True)]:
        t1 = time.time()
        P, Tr = decode_mesh(attr(mf.node(shape, 'DMSH')[3], 'o'))
        P2, T2 = fast_simplification.simplify(np.ascontiguousarray(P, np.float32), np.ascontiguousarray(Tr, np.int32), target_reduction=max(0.0, 1 - target / len(Tr)))
        P2 = P2.astype(np.float64); T2 = T2.astype(np.int64)
        if is_head:
            Wh = np.zeros((len(P2), nj)); Wh[:, head] = 1
        else:
            Wh = transfer_weights(P2, Pb, Tb, W)
        print('%s : %d tris -> %d (%.1fs)' % (label, len(Tr), len(T2), time.time() - t1))
        meshes.append((label, P2, T2, Wh, 'high'))

    eyes = next(P for l, P, *_ in meshes if l == 'EYES')
    write_glb(a.glb, joints, parents, bind, meshes,
              dict(units='cm', source=a.mb.split('/')[-1], floor=float(min(m[1][:, 1].min() for m in meshes)), eyeHeight=float(eyes[:, 1].mean())))
    print('écrit %s (%.1f Mo, %.1fs)' % (a.glb, len(open(a.glb, 'rb').read()) / 1e6, time.time() - t0))


if __name__ == '__main__':
    main()
