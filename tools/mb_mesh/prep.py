# Sommets uniques par (position, uv), décalage UDIM ramené dans [0,1], mise à l'échelle commune.
import numpy as np, sys, json
names = ['polySurfaceShape1', 'polySurfaceShape2', 'DENTSShape', 'EYESShape']
labels = {'polySurfaceShape1': 'body', 'polySurfaceShape2': 'head', 'DENTSShape': 'teeth', 'EYESShape': 'eyes'}
meta = []
X = np.load('out/xf.npz')
XF = {'DENTSShape': ('DENTS', True), 'EYESShape': ('EYES', True)}
for n in names:
    d = dict(np.load(f'out/{n}.npz'))
    if n in XF:
        xf, tweak = XF[n]
        P = d['pos'].astype(np.float64)
        if tweak and len(X[xf + '_pt']) == len(P): P = P + X[xf + '_pt']
        Mx = X[xf]
        d['pos'] = (P @ Mx[:3, :3].T + Mx[:3, 3]).astype(np.float32)
    tv, tuv = d['tri_v'].ravel(), d['tri_uv'].ravel()
    key = tv.astype(np.int64) * (len(d['uv']) + 1) + tuv
    uniq, inv = np.unique(key, return_inverse=True)
    v = (uniq // (len(d['uv']) + 1)).astype(np.int64); t = (uniq % (len(d['uv']) + 1)).astype(np.int64)
    pos = d['pos'][v].astype(np.float32); uv = d['uv'][t].astype(np.float32)
    udim = int(np.floor(np.median(uv[:, 0]))) 
    uv[:, 0] -= udim
    idx = inv.astype(np.uint32)
    pos.tofile(f'out/{labels[n]}.pos'); uv.tofile(f'out/{labels[n]}.uv'); idx.tofile(f'out/{labels[n]}.idx')
    meta.append({'name': labels[n], 'verts': len(pos), 'tris': len(idx) // 3, 'udim': 1001 + udim})
    print(meta[-1])
json.dump(meta, open('out/meta.json', 'w'))
