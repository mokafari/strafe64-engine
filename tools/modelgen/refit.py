"""refit.py -- fits a Mixamo-rigged character mesh onto the UAL skeleton.

The clips, cuts, ragdoll and sword grip all run on that one skeleton, so a new
character keeps it and only its mesh moves: each bone's piece of the mesh is
turned onto the matching UAL bone and carried to its position, blended by the
character's own skin weights. Proportions follow the UAL body.
"""
import numpy as np

MAIN = {'Hips': 'Spine', 'Spine': 'Spine1', 'Spine1': 'Spine2', 'Spine2': 'Neck', 'Neck': 'Head'}
for S in ('Left', 'Right'):
    MAIN.update({S + 'Shoulder': S + 'Arm', S + 'Arm': S + 'ForeArm', S + 'ForeArm': S + 'Hand',
                 S + 'Hand': S + 'HandMiddle1', S + 'UpLeg': S + 'Leg', S + 'Leg': S + 'Foot',
                 S + 'Foot': S + 'ToeBase'})
    for f, F in (('index', 'Index'), ('middle', 'Middle'), ('ring', 'Ring'), ('pinky', 'Pinky'), ('thumb', 'Thumb')):
        for k in (1, 2):
            MAIN[f'{S}Hand{F}{k}'] = f'{S}Hand{F}{k + 1}'

def ual_name(m):
    """Mixamo bone -> UAL joint"""
    import retarget as rt
    for k, v in rt.BONES.items():
        if v == m:
            return k
    for S, s in (('Left', 'l'), ('Right', 'r')):
        for F, f in (('Index', 'index'), ('Middle', 'middle'), ('Ring', 'ring'), ('Pinky', 'pinky'), ('Thumb', 'thumb')):
            for k in (1, 2, 3):
                if m == f'{S}Hand{F}{k}':
                    return f'{f}_0{k}_{s}'
    return None

def arc(a, b):
    """shortest rotation taking unit a to unit b"""
    v = np.cross(a, b); c = a @ b
    if c < -0.9999:
        return np.diag([-1.0, 1.0, -1.0])
    k = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + k + k @ k / (1 + c)

def unit(v):
    return v / np.linalg.norm(v)

def refit(g, names, ibm_ual):
    """g: the character. names, ibm_ual: the UAL joints and inverse bind matrices.
    Returns P, N, U, JI, W in the UAL mesh space."""
    j = g.j; skin = j['skins'][0]
    bones = [j['nodes'][n]['name'].split(':')[-1] for n in skin['joints']]
    ibm = g.acc(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    src = {b: np.linalg.inv(ibm[i])[:3, 3] for i, b in enumerate(bones)}
    ual = {n: np.linalg.inv(ibm_ual[i])[:3, 3] for i, n in enumerate(names)}
    pr = j['meshes'][0]['primitives'][0]; at = pr['attributes']
    P = g.acc(at['POSITION']).astype(float); N = g.acc(at['NORMAL']).astype(float); U = g.acc(at['TEXCOORD_0'])
    JI = g.acc(at['JOINTS_0']).astype(int); W = g.acc(at['WEIGHTS_0']).astype(float)
    ind = g.acc(pr['indices']).reshape(-1, 3).astype(int)

    # stand it the same way round, at the same height
    a = src['RightArm'] - src['LeftArm']; b = ual['upperarm_r'] - ual['upperarm_l']
    a[1] = b[1] = 0
    A = arc(unit(a), unit(b))
    s = (ual['Head'][1] - ual['pelvis'][1]) / (src['Head'][1] - src['Hips'][1])
    P = (P @ A.T) * s; N = N @ A.T
    src = {k: (A @ v) * s for k, v in src.items()}
    print('refit: turned %.0f deg, scale %.3f' % (np.degrees(np.arccos(np.clip((np.trace(A) - 1) / 2, -1, 1))), s))

    # every bone: where it goes and how it turns
    parent = {c: p for p, nd in enumerate(j['nodes']) for c in nd.get('children', [])}
    node_of = {j['nodes'][n]['name'].split(':')[-1]: n for n in skin['joints']}
    target, R, T = np.zeros(len(bones), int), [], []
    for i, b in enumerate(bones):
        m = b
        while ual_name(m) is None:		# toe ends, finger tips: ride the nearest bone
            m = j['nodes'][parent[node_of[m]]]['name'].split(':')[-1]
        u = ual_name(m)
        target[i] = names.index(u)
        c = MAIN.get(m)
        r = np.eye(3)
        if m == 'Hips':
            r = np.eye(3)
        elif c and ual_name(c):
            r = arc(unit(src[c] - src[m]), unit(ual[ual_name(c)] - ual[u]))
        else:				# no child to aim at: keep the parent's turn
            pm = j['nodes'][parent[node_of[m]]]['name'].split(':')[-1]
            pc = MAIN.get(pm)
            if pc and ual_name(pc):
                r = arc(unit(src[pc] - src[pm]), unit(ual[ual_name(pc)] - ual[ual_name(pm)]))
        R.append(r); T.append(ual[u] - r @ src[m])
    R = np.array(R); T = np.array(T)
    Q = np.einsum('nk,nkab,nb->na', W, R[JI], P) + np.einsum('nk,nka->na', W, T[JI])
    M = np.einsum('nk,nkab,nb->na', W, R[JI], N)
    M /= np.linalg.norm(M, axis=1, keepdims=True)
    return Q, M, U, target[JI], W, ind[:, ::-1]		# Quake winds the other way from glTF

def tangents(P, N, U, tris):
    """per-vertex tangents from the UVs, with handedness, for the normal map"""
    t = np.zeros_like(P); b = np.zeros_like(P)
    a, c, d = tris[:, 0], tris[:, 1], tris[:, 2]
    e1, e2 = P[c] - P[a], P[d] - P[a]; u1, u2 = U[c] - U[a], U[d] - U[a]
    det = u1[:, 0] * u2[:, 1] - u2[:, 0] * u1[:, 1]
    f = np.where(np.abs(det) > 1e-9, 1 / np.where(det == 0, 1, det), 0)[:, None]
    fs = (e1 * u2[:, 1:] - e2 * u1[:, 1:]) * f; ft = (e2 * u1[:, :1] - e1 * u2[:, :1]) * f
    for v in (a, c, d):
        np.add.at(t, v, fs); np.add.at(b, v, ft)
    t -= N * np.sum(N * t, 1, keepdims=True)
    l = np.linalg.norm(t, axis=1, keepdims=True); bad = l[:, 0] < 1e-6
    t = t / np.where(l == 0, 1, l); t[bad] = [1, 0, 0]
    return np.c_[t, np.sign(np.sum(np.cross(N, t) * b, 1)) * 1.0 + (np.sum(np.cross(N, t) * b, 1) == 0)]
