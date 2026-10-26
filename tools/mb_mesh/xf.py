# Matrices monde (rig au repos) de la tête, des dents et des yeux, et tweaks pt des yeux/dents, via mb2glb.py.
import os, sys, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'mb2glb'))  # lecteur du rig (tools/mb2glb)
import mb2glb as M
mf = M.MayaFile(sys.argv[1])
rest = M.T(M.dbl(M.attr(mf.node('BASEMESH_parentConstraint1')[3], 'rst'), 3))
ov = {('BASEMESH', '|ORK_BOY|GEO|HEAD_ITEM_CTRL'): rest, ('OTHERS', 'GEO'): np.eye(4)}
out = {}
for shape, xf in [('HEAD_geoShape', 'HEAD_geo'), ('DENTSShape', 'DENTS'), ('EYESShape', 'EYES')]:
    _, _, _, sa = mf.node(shape, 'DMSH')
    pt = next((v for t, n, v in sa if n.startswith('pt[')), None)
    out[xf] = M.world_matrix(mf, xf, ov)
    out[xf + '_pt'] = np.frombuffer(pt, '>f4', offset=1).reshape(-1, 3).astype(np.float32) if pt is not None else np.zeros((0, 3), np.float32)
    print(xf, out[xf].round(3).tolist(), len(out[xf + '_pt']))
np.savez('out/xf.npz', **out)
