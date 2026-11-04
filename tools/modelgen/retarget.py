"""retarget.py -- moves Mixamo clips (FBX converted to .glb) onto the UAL skeleton.

Both rigs are compared in T-pose. For each mapped bone the source's world rotation
change from its bind pose is applied to the target's T-pose world rotation, then
turned back into a local rotation. Hips translation scales by hip height.
"""
import numpy as np

BONES = {'pelvis': 'Hips', 'spine_01': 'Spine', 'spine_02': 'Spine1', 'spine_03': 'Spine2',
         'neck_01': 'Neck', 'Head': 'Head'}
for s, S in (('l', 'Left'), ('r', 'Right')):
    BONES.update({'clavicle_' + s: S + 'Shoulder', 'upperarm_' + s: S + 'Arm', 'lowerarm_' + s: S + 'ForeArm',
                  'hand_' + s: S + 'Hand', 'thigh_' + s: S + 'UpLeg', 'calf_' + s: S + 'Leg',
                  'foot_' + s: S + 'Foot', 'ball_' + s: S + 'ToeBase'})

MX = np.diag([-1.0, 1.0, 1.0])

def q2m(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def m2q(m):
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1) * 2
        q = [(m[2,1]-m[1,2])/s, (m[0,2]-m[2,0])/s, (m[1,0]-m[0,1])/s, s/4]
    else:
        i = int(np.argmax(np.diag(m))); j, k = (i+1) % 3, (i+2) % 3
        s = np.sqrt(1 + m[i,i] - m[j,j] - m[k,k]) * 2
        q = [0, 0, 0, (m[k,j]-m[j,k])/s]
        q[i] = s/4; q[j] = (m[j,i]+m[i,j])/s; q[k] = (m[k,i]+m[i,k])/s
    q = np.array(q)
    return q / np.linalg.norm(q)

def trs(t, r, s):
    m = np.eye(4); m[:3,:3] = q2m(r) * np.array(s); m[:3,3] = t
    return m

def globals_at(g, tracks, t, sample):
    """world 4x4 of every node at time t"""
    nodes = g.j['nodes']; out = {}
    def walk(i, parent):
        nd = nodes[i]
        tt = sample(*tracks[(i, 'translation')], t, False) if (i, 'translation') in tracks else nd.get('translation', [0, 0, 0])
        rr = sample(*tracks[(i, 'rotation')], t, True) if (i, 'rotation') in tracks else nd.get('rotation', [0, 0, 0, 1])
        ss = sample(*tracks[(i, 'scale')], t, False) if (i, 'scale') in tracks else nd.get('scale', [1, 1, 1])
        out[i] = parent @ trs(tt, rr, ss)
        for c in nd.get('children', []):
            walk(c, out[i])
    kids = {c for n in nodes for c in n.get('children', [])}
    for i in range(len(nodes)):
        if i not in kids:
            walk(i, np.eye(4))
    return out

def tracks_of(g, anim):
    tr = {}
    for c in anim['channels']:
        s = anim['samplers'][c['sampler']]
        tr[(c['target']['node'], c['target']['path'])] = (g.acc(s['input'])[:, 0], g.acc(s['output']).astype(float))
    return tr

def retarget(src, tgt, jn, rest_tracks, sample, fps, bind=None, strip=(True, False, True), mirror=False, hold_y=False):
    """returns per frame, per joint: local (t xyz, q xyzw) arrays for tgt joints jn"""
    tn = tgt.j['nodes']
    names = [tn[n]['name'] for n in jn]
    tparent = {c: p for p, nd in enumerate(tn) for c in nd.get('children', [])}
    tg0 = globals_at(tgt, rest_tracks, 0.0, sample)
    T0 = {n: tg0[n][:3,:3] / np.linalg.norm(tg0[n][:3,0]) for n in jn}

    sn = src.j['nodes']; byname = {nd.get('name'): i for i, nd in enumerate(sn)}
    # clips downloaded without skin have no bind pose: read it from a skinned bot
    bg = src if 'skins' in src.j else bind
    skin = bg.j['skins'][0]; ibm = bg.acc(skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    sbind = {}
    hip0 = None
    for i, n in enumerate(skin['joints']):
        m = np.linalg.inv(ibm[i])
        nm = bg.j['nodes'][n].get('name')
        if nm in byname:
            sbind[byname[nm]] = m[:3,:3] / np.linalg.norm(m[:3,0])
        if nm == 'mixamorig:Hips':
            hip0 = m[:3,3]
    hips = byname['mixamorig:Hips']
    pel = jn[names.index('pelvis')]
    scale = tg0[pel][1,3] / hip0[1]

    anim = src.j['animations'][0]; tr = tracks_of(src, anim)
    dur = max(v[0][-1] for v in tr.values())
    nf = max(2, int(round(dur * fps)) + 1)
    frames = []
    hp = []
    for f in range(nf):
        G = globals_at(src, tr, f / fps, sample)
        hp.append(G[hips][:3,3].copy())
        world = {}
        row_t, row_q = [], []
        for n, nm in zip(jn, names):
            rest_w = T0[n]
            if nm in BONES and 'mixamorig:' + BONES[nm] in byname:
                other = BONES[nm]
                if mirror:
                    other = other.replace('Left', '#').replace('Right', 'Left').replace('#', 'Right')
                s = byname['mixamorig:' + other]
                r = G[s][:3,:3] / np.linalg.norm(G[s][:3,0])
                delta = r @ sbind[s].T
                if mirror:					# reflect the world rotation across X
                    delta = MX @ delta @ MX
                world[n] = delta @ rest_w
            else:
                p = tparent.get(n)
                pw = world.get(p, T0.get(p, np.eye(3)))
                pr = T0.get(p, np.eye(3))
                world[n] = pw @ pr.T @ rest_w		# unmapped: keep T-pose local
            p = tparent.get(n)
            pw = world.get(p, np.eye(3))
            row_q.append(m2q(pw.T @ world[n]))
            nd = tn[n]
            row_t.append(np.array(sample(*rest_tracks[(n, 'translation')], 0.0, False) if (n, 'translation') in rest_tracks else nd.get('translation', [0, 0, 0]), float))
        frames.append((row_t, row_q))
    hp = np.array(hp)
    # hips motion: drift removed per axis, the game moves the player
    d = hp - hip0
    tm = np.linspace(0, 1, nf)[:, None]
    for a in range(3):
        if strip[a]:
            d[:, a] -= d[0, a] + (d[-1, a] - d[0, a]) * tm[:, 0]
    if hold_y:						# the jump itself comes from the game
        d[:, 1] = 0
    if mirror:
        d[:, 0] = -d[:, 0]
    pi = jn.index(pel)
    ppar = tparent.get(pel)
    for f in range(nf):
        w = d[f] * scale
        pr = T0.get(ppar, np.eye(3))
        frames[f][0][pi] = frames[f][0][pi] + pr.T @ w
    print('retarget %d frames, hip scale %.3f, hip travel %s' % (nf, scale, np.round(hp[-1] - hp[0], 1)))
    return nf, names, frames
