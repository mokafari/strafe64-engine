#!/usr/bin/env python3
"""animjerk.py LOG -- reads cg_skelTrace lines and reports where joints jump.

A joint's position relative to the player should change smoothly. For each frame
we take its velocity change (acceleration, u/s^2 in thousands) and count spikes,
grouped by the clip they happened in and whether the clip had just changed.
"""
import collections, re, sys
import numpy as np

names = re.findall(r'SKEL_(\w+),', open('engine/code/cgame/cg_skelclips.h').read())
rows = []
for l in open(sys.argv[1], errors='ignore'):
    if l.startswith('SKT '):
        p = l.split()
        if len(p) == 18:
            rows.append([float(x) for x in p[1:]])
a = np.array(rows)
a = a[a[:, 0] > a[0, 0] + 500]        # skip load-in
keep = np.r_[True, np.diff(a[:, 0]) >= 4]   # sub-4ms frames are timer noise
a = a[keep]
print('frames', len(a), 'median dt %.1f ms' % np.median(np.diff(a[:, 0])))
t = a[:, 0] / 1000.0
LIMIT = 40.0
for i, joint in enumerate(('head', 'hand_r', 'foot_l', 'foot_r')):
    p = a[:, 5 + i * 3:8 + i * 3]
    dt = np.maximum(np.diff(t), 1e-3)
    v = np.diff(p, axis=0) / dt[:, None]
    acc = np.linalg.norm(np.diff(v, axis=0), axis=1) / dt[1:] / 1000.0
    spikes = np.where(acc > LIMIT)[0]
    if '-v' in sys.argv and joint == 'head':
        for k in spikes:
            print('   t=%d %s spd=%.0f lean=%.1f,%.1f acc=%.0f' % (a[k + 1, 0], names[int(a[k + 1, 1])], a[k + 1, 2], a[k + 1, 3], a[k + 1, 4], acc[k]))
    by = collections.Counter()
    for k in spikes:
        c0, c1 = int(a[k, 1]), int(a[k + 2, 1])
        by[names[c1] + ('*' if c0 != c1 else '')] += 1
    print('%-7s p50 %6.1f p99 %7.1f max %8.1f spikes %4d (%.1f%%) %s' % (joint, np.percentile(acc, 50),
        np.percentile(acc, 99), acc.max(), len(spikes), 100 * len(spikes) / len(acc), by.most_common(5)))
lean = np.abs(np.diff(a[:, 3:5], axis=0)).max(1)
print('lean step deg/frame p99 %.2f max %.2f' % (np.percentile(lean, 99), lean.max()))
print('clip changes', int((np.diff(a[:, 1]) != 0).sum()), 'per min %.0f' % ((np.diff(a[:, 1]) != 0).sum() / (t[-1] - t[0]) * 60))
