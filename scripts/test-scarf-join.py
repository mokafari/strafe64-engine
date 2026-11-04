#!/usr/bin/env python3
"""Check the baked cuff exit meets the runtime ribbon's pinned edge."""
from pathlib import Path
import re
import numpy as np
text=(Path(__file__).resolve().parents[1]/'engine/code/cgame/cg_robotscarf.h').read_text()
def vector(name):
 body=re.search(r'\b'+name+r'\s*=\s*\{([^}]+)',text).group(1)
 return np.array([float(v.strip().rstrip('f')) for v in body.split(',')])
root=vector('robotScarfSocket');side=vector('robotScarfSide')
body=text.split('robotScarfJoinVerts[] = {')[1].split('};')[0]
verts=np.array([[float(v.strip().rstrip('f')) for v in row.split(',')] for row in re.findall(r'\{([^}]+)\}',body)])
assert verts.shape==(16,3) and np.isfinite(verts).all()
np.testing.assert_allclose(verts[-4:].mean(0),root,atol=1e-6)
np.testing.assert_allclose(verts[[13,14]].mean(0),root+2.4*side,atol=1e-6)
np.testing.assert_allclose(verts[[12,15]].mean(0),root-2.4*side,atol=1e-6)
# A bone's rigid animation transforms both sides identically: a shared local
# boundary remains coincident after any rotation or translation.
print('Scarf join: cuff centre and both ribbon edges coincide in neck-bone coordinates.')
