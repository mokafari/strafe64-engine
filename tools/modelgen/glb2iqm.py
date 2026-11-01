#!/usr/bin/env python3
"""glb2iqm.py -- bake a skinned glTF 2.0 binary (.glb) to an IQM v2 model.

    glb2iqm.py out.iqm --mesh rig.glb --shader NAME --height 64 \
        --anims a.glb:Sprint_Loop,Roll b.glb:ClimbUp_1m

Takes the mesh, skin and rest pose from --mesh and resamples named clips at 30
fps from any .glb sharing that skeleton. glTF is Y-up in metres; Quake is Z-up
in game units, so everything goes through the cyclic axis swap (x,y,z)->(z,x,y).
"""
import argparse, json, struct
import numpy as np

FPS = 30.0
COMP = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

class Glb:
    def __init__(self, path):
        with open(path, 'rb') as f:
            f.read(12)
            n = struct.unpack('<I', f.read(4))[0]; f.read(4)
            self.j = json.loads(f.read(n))
            n = struct.unpack('<I', f.read(4))[0]; f.read(4)
            self.bin = f.read(n)

    def acc(self, i):
        a = self.j['accessors'][i]; v = self.j['bufferViews'][a['bufferView']]
        dt = np.dtype('<' + COMP[a['componentType']]); k = NUM[a['type']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', 0) or dt.itemsize * k
        raw = np.frombuffer(self.bin, np.uint8, stride * (a['count'] - 1) + dt.itemsize * k, off)
        out = np.stack([np.frombuffer(raw.tobytes(), dt, k, s) for s in range(0, stride * a['count'], stride)]) \
            if stride != dt.itemsize * k else np.frombuffer(raw.tobytes(), dt).reshape(-1, k)
        if a.get('normalized') and dt.kind != 'f':
            out = out / float(np.iinfo(dt).max)
        return out

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return np.array([aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx,
                     aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz])

def swap(v):  # glTF axes -> Quake axes (works on vec3 or quat xyz)
    return np.stack([v[..., 2], v[..., 0], v[..., 1]], -1)

def sample(times, vals, t, quat):
    if t <= times[0]: return vals[0]
    if t >= times[-1]: return vals[-1]
    i = int(np.searchsorted(times, t)) - 1
    f = (t - times[i]) / (times[i + 1] - times[i]); a, b = vals[i], vals[i + 1]
    if quat:
        if np.dot(a, b) < 0: b = -b
        r = a * (1 - f) + b * f
        return r / np.linalg.norm(r)
    return a * (1 - f) + b * f

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out'); ap.add_argument('--mesh', required=True)
    ap.add_argument('--shader', default='*white'); ap.add_argument('--height', type=float, default=64)
    ap.add_argument('--anims', nargs='*', default=[])
    o = ap.parse_args()
    g = Glb(o.mesh); j = g.j
    skin = j['skins'][0]; jn = skin['joints']; idx = {n: i for i, n in enumerate(jn)}
    parent = {c: p for p, nd in enumerate(j['nodes']) for c in nd.get('children', [])}
    names = [j['nodes'][n]['name'] for n in jn]

    # geometry, baked to Quake axes and scaled to the requested standing height
    P, N, U, JI, W, tris, meshes = [], [], [], [], [], [], []
    base = 0
    for pr in j['meshes'][0]['primitives']:
        at = pr['attributes']
        p = g.acc(at['POSITION']).astype(float); ind = g.acc(pr['indices']).reshape(-1, 3).astype(int)
        meshes.append((base, len(p), sum(len(t) for t in tris), len(ind)))
        P.append(p); N.append(g.acc(at['NORMAL'])); U.append(g.acc(at['TEXCOORD_0']))
        JI.append(g.acc(at['JOINTS_0'])); W.append(g.acc(at['WEIGHTS_0']))
        tris.append(ind + base); base += len(p)
    P = np.concatenate(P); N = np.concatenate(N); U = np.concatenate(U)
    JI = np.concatenate(JI).astype(np.uint8); W = np.concatenate(W); tris = np.concatenate(tris)
    # the renderer takes under SHADER_MAX_VERTEXES (4000) per surface: split
    # greedily, duplicating the vertices shared across a chunk boundary
    remap, new_tris, meshes = [], [], []
    seen, ft, nt = {}, 0, 0
    def flush():
        nonlocal seen, ft, nt
        meshes.append((len(remap) - len(seen), len(seen), ft, nt)); seen, ft, nt = {}, ft + nt, 0
    for t in tris:
        if len(seen) + 3 > 3990: flush()
        o3 = []
        for v in t:
            v = int(v)
            if v not in seen:
                seen[v] = len(remap); remap.append(v)
            o3.append(seen[v])
        new_tris.append(o3); nt += 1
    flush()
    P, N, U, JI, W = P[remap], N[remap], U[remap], JI[remap], W[remap]
    tris = np.array(new_tris)
    sc = o.height / (P[:, 1].max() - P[:, 1].min())
    print('bind height %.3f scale %.2f' % (P[:, 1].max() - P[:, 1].min(), sc))
    pos = swap(P) * sc; nrm = swap(N)
    w8 = np.round(W / W.sum(1, keepdims=True) * 255).astype(int)
    w8[np.arange(len(w8)), w8.argmax(1)] += 255 - w8.sum(1)
    tan = np.cross(nrm, np.array([0.0, 0.0, 1.0])); bad = np.linalg.norm(tan, axis=1) < 1e-4
    tan[bad] = [1, 0, 0]; tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    tan4 = np.c_[tan, np.ones(len(tan))]

    def rest(n):
        nd = j['nodes'][n]
        return (np.array(nd.get('translation', [0, 0, 0]), float),
                np.array(nd.get('rotation', [0, 0, 0, 1]), float),
                np.array(nd.get('scale', [1, 1, 1]), float))

    for n in jn:   # anything above the root would have to be baked in
        p = parent.get(n)
        if p is not None and p not in idx:
            t, r, s = rest(p)
            print('root parent', j['nodes'][p]['name'], t, r, s)

    frames, anims = [], []   # frames: per frame, joints x 10
    for spec in o.anims:
        path, clips = spec.rsplit(':', 1)
        a = Glb(path); ajn = {nd.get('name'): i for i, nd in enumerate(a.j['nodes'])}
        byname = {x['name']: x for x in a.j['animations']}
        for clip in clips.split(','):
            an = byname[clip]; tr = {}
            for c in an['channels']:
                s = an['samplers'][c['sampler']]
                nm = a.j['nodes'][c['target']['node']].get('name')
                tr[(nm, c['target']['path'])] = (a.acc(s['input'])[:, 0], a.acc(s['output']).astype(float))
            dur = max(v[0][-1] for v in tr.values())
            nf = max(2, int(round(dur * FPS)) + 1)
            first = len(frames)
            for f in range(nf):
                t = f / FPS; row = []
                for i, n in enumerate(jn):
                    rt, rr, rs = rest(n); nm = names[i]
                    tt = sample(*tr[(nm, 'translation')], t, False) if (nm, 'translation') in tr else rt
                    qq = sample(*tr[(nm, 'rotation')], t, True) if (nm, 'rotation') in tr else rr
                    ss = sample(*tr[(nm, 'scale')], t, False) if (nm, 'scale') in tr else rs
                    row.append(np.r_[swap(tt) * sc, swap(qq[:3]), qq[3], ss])
                frames.append(row)
            anims.append((clip, first, nf, 'Loop' in clip))
            print('clip %-22s %3d frames @%d' % (clip, nf, first))
    F = np.array(frames) if frames else np.zeros((1, len(jn), 10))
    for i in range(len(jn)):           # keep each quaternion channel continuous
        q = F[:, i, 3:7]
        for f in range(1, len(q)):
            if np.dot(q[f - 1], q[f]) < 0: q[f] = -q[f]

    text = b'\0'; 
    def s(x):
        nonlocal text
        o_ = len(text); text += x.encode() + b'\0'; return o_
    sh = s(o.shader); mname = s('body')
    jb = b''
    for i, n in enumerate(jn):
        t, r, sca = rest(n); p = parent.get(n)
        jb += struct.pack('<Ii10f', s(names[i]), idx.get(p, -1), *(swap(t) * sc), *swap(r[:3]), r[3], *sca)
    pb = b''
    off = F.min(0); span = F.max(0) - off; scale = span / 65535.0
    for i, n in enumerate(jn):
        pb += struct.pack('<iI20f', idx.get(parent.get(n), -1), 0x3ff, *off[i], *scale[i])
    fd = np.where(scale[None] > 0, np.round((F - off[None]) / np.where(scale > 0, scale, 1)[None]), 0).astype('<u2')
    ab = b''.join(struct.pack('<IIIfI', s(n), a, c, FPS, 1 if l else 0) for n, a, c, l in anims)
    mb = b''.join(struct.pack('<6I', mname, sh, *m) for m in meshes)
    bb = struct.pack('<8f', -32, -32, -8, 32, 32, 80, 48, 96) * len(F)

    chunks, cur = {}, 124
    def put(k, b):
        nonlocal cur
        chunks[k] = (cur, b); cur += len(b) + (-len(b) % 4)
    while len(text) % 4: text += b'\0'
    put('text', text); put('mesh', mb)
    va = [(0, 7, 3, pos.astype('<f4')), (1, 7, 2, U.astype('<f4')), (2, 7, 3, nrm.astype('<f4')),
          (3, 7, 4, tan4.astype('<f4')), (4, 1, 4, JI), (5, 1, 4, w8.astype(np.uint8))]
    vah = cur; cur += 20 * len(va); vad = b''
    for ty, fm, sz, arr in va:
        chunks['v%d' % ty] = None; 
    vb = []
    for ty, fm, sz, arr in va:
        vb.append((ty, 0, fm, sz, cur)); b = arr.tobytes(); chunks['v%d' % ty] = (cur, b); cur += len(b) + (-len(b) % 4)
    put('tri', tris.astype('<u4').tobytes()); put('joint', jb); put('pose', pb); put('anim', ab)
    put('frame', fd.tobytes()); put('bounds', bb)
    hdr = b'INTERQUAKEMODEL\0' + struct.pack('<27I', 2, cur, 0, len(text), chunks['text'][0], len(meshes), chunks['mesh'][0],
        len(va), len(pos), vah, len(tris), chunks['tri'][0], 0, len(jn), chunks['joint'][0], len(jn), chunks['pose'][0],
        len(anims), chunks['anim'][0], len(F), 10 * len(jn), chunks['frame'][0], chunks['bounds'][0], 0, 0, 0, 0)
    buf = bytearray(cur); buf[:len(hdr)] = hdr
    for k, c in chunks.items():
        buf[c[0]:c[0] + len(c[1])] = c[1]
    buf[vah:vah + 20 * len(va)] = b''.join(struct.pack('<5I', *v) for v in vb)
    open(o.out, 'wb').write(buf)
    print('wrote %s: %d verts %d tris %d joints %d frames %.1f KB' % (o.out, len(pos), len(tris), len(jn), len(F), cur / 1024))

if __name__ == '__main__':
    main()
