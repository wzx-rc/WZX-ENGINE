"""Lecteur minimal des maillages d'un fichier Maya binaire (.mb, IFF 64 bits FOR8).
Extrait chaque noeud mesh (DMSH) : positions, faces (par liste d'arêtes) et UV map1.
Usage : python mb_mesh.py fichier.mb dossier_sortie [nom1 nom2 ...]
Sortie par mesh : <nom>.npz (pos float32 Nx3, uv float32 Mx2, tri_v uint32 Tx3, tri_uv uint32 Tx3)."""
import struct, sys, os
import numpy as np

def chunks(data, off, end):
    base = off
    while off + 16 <= end:
        tag = data[off:off + 4]
        size = struct.unpack('>Q', data[off + 8:off + 16])[0]
        body = off + 16
        if tag in (b'FOR8', b'LIS8', b'CAT8'):
            yield ('G', data[body:body + 4], body + 4, body + size)
            off = body + size
            continue
        yield ('L', tag, body, size)
        off = body + size
        off = base + ((off - base + 7) & ~7)

def meshes(data):
    out = []
    def walk(off, end):
        for kind, tag, a, b in chunks(data, off, end):
            if kind == 'G':
                if tag == b'DMSH': out.append(dmsh(a, b))
                else: walk(a, b)
    def dmsh(a, b):
        node = {'name': None, 'parent': None, 'mesh': None, 'io': False}
        for kind, tag, body, size in chunks(data, a, b):
            if kind != 'L': continue
            if tag == b'CREA':
                parts = data[body:body + size].split(b'\0')
                node['name'] = parts[0][1:].decode('latin1'); node['parent'] = parts[1].decode('latin1')
            elif tag == b'DBLE' and data[body:body + 3] == b'io\0':
                node['io'] = True
            elif tag == b'MESH':
                node['mesh'] = (body, size)
        return node
    walk(20, len(data))
    return out

def parse_mesh(data, body, size):
    m = memoryview(data)[body:body + size]
    p = bytes(m[:32]).index(b'\0') + 2
    def u32():
        nonlocal p
        v = struct.unpack('>I', m[p:p + 4])[0]; p += 4; return v
    def arr(n, dt):
        nonlocal p
        a = np.frombuffer(m[p:p + 4 * n], dtype=dt).copy(); p += 4 * n; return a
    nf = u32(); pos = arr(nf, '>f4').astype(np.float32).reshape(-1, 3)
    ne = u32(); edges = (arr(ne, '>u4').astype(np.int64) & 0x1fffffff).reshape(-1, 2)  # bit 31 : arête dure
    nfe = u32(); fe = arr(nfe, '>u4').astype(np.int64)
    nn = u32(); arr(nn, '>f4')
    u32(); nsets = u32(); u32()
    uv = None; fuv = None
    if nsets:
        e = bytes(m[p:p + 256]).index(b'\0'); p += e + 1
        nu = u32(); uv = arr(nu, '>f4').astype(np.float32).reshape(-1, 2)
        nfu = u32(); fuv = arr(nfu, '>u4').astype(np.int64)
    rev = (fe >> 31) & 1
    last = (fe >> 30) & 1
    ei = fe & 0x1fffffff
    corner_v = np.where(rev == 1, edges[ei, 1], edges[ei, 0])
    # faces : indices de début de chaque face
    ends = np.nonzero(last)[0]
    starts = np.concatenate([[0], ends[:-1] + 1])
    counts = ends - starts + 1
    # triangulation en éventail
    tri_idx = []
    for k in range(3, counts.max() + 1):
        sel = starts[counts == k]
        for j in range(1, k - 1):
            tri_idx.append(np.stack([sel, sel + j, sel + j + 1], 1))
    tri_idx = np.concatenate(tri_idx)
    tri_v = corner_v[tri_idx].astype(np.uint32)
    tri_uv = fuv[tri_idx].astype(np.uint32) if fuv is not None and len(fuv) == len(fe) else None
    return pos, uv, tri_v, tri_uv, len(counts)

if __name__ == '__main__':
    src, outdir = sys.argv[1], sys.argv[2]
    want = set(sys.argv[3:])
    data = open(src, 'rb').read()
    os.makedirs(outdir, exist_ok=True)
    for node in meshes(data):
        if not node['mesh']: continue
        print(node['name'], 'parent=' + str(node['parent']), 'io=' + str(node['io']), end=' ')
        if want and node['name'] not in want: print('(ignoré)'); continue
        pos, uv, tv, tuv, nfaces = parse_mesh(data, *node['mesh'])
        print(f'{len(pos)} sommets, {nfaces} faces, {len(tv)} triangles, uv={None if uv is None else len(uv)}')
        np.savez(os.path.join(outdir, node['name'] + '.npz'), pos=pos, uv=uv if uv is not None else np.zeros((0, 2), np.float32), tri_v=tv, tri_uv=tuv if tuv is not None else np.zeros((0, 3), np.uint32))
