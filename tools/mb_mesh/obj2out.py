"""OBJ (v/vt/vn, faces quads ou n-gones) -> out/meta.json + .pos/.uv/.idx pour simplify.mjs.
Les sommets sont dédoublés par paire (position, uv). Usage : python obj2out.py fichier.obj nom [udim]"""
import sys, json, os
import numpy as np

src, name = sys.argv[1], sys.argv[2]
udim = int(sys.argv[3]) if len(sys.argv) > 3 else 1001
V, T, F = [], [], []
with open(src, 'r') as f:
    for line in f:
        if line.startswith('v '):
            V.append(line[2:].split()[:3])
        elif line.startswith('vt '):
            T.append(line[3:].split()[:2])
        elif line.startswith('f '):
            F.append([tuple(int(x or 0) for x in (c.split('/') + ['', ''])[:2]) for c in line[2:].split()])
V = np.array(V, dtype=np.float32); T = np.array(T, dtype=np.float32)
tri = []
for face in F:
    for i in range(1, len(face) - 1):
        tri += [face[0], face[i], face[i + 1]]
tri = np.array(tri, dtype=np.int64)            # (n, 2) : indices v, vt (base 1)
key = tri[:, 0] * (len(T) + 1) + tri[:, 1]
uniq, inv = np.unique(key, return_inverse=True)
vi = (uniq // (len(T) + 1)) - 1; ti = (uniq % (len(T) + 1)) - 1
os.makedirs('out', exist_ok=True)
V[vi].tofile(f'out/{name}.pos'); T[ti].tofile(f'out/{name}.uv'); inv.astype(np.uint32).tofile(f'out/{name}.idx')
json.dump([{'name': name, 'udim': udim, 'tris': len(inv) // 3}], open('out/meta.json', 'w'))
print(name, len(vi), 'sommets', len(inv) // 3, 'triangles', 'y', V[:, 1].min(), V[:, 1].max(), file=sys.stderr)
