#!/usr/bin/env python3
"""animcompare.py TAG [N] -- runs animtrace N times and prints median jerk stats,
because a single run varies too much to compare builds."""
import re, statistics as st, subprocess, sys
tag, n = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3
rows = {}
for i in range(n):
    out = subprocess.run(['sh', 'tools/modelgen/animtrace.sh', f'{tag}{i}'], capture_output=True, text=True).stdout
    scene = 'local'
    for l in out.splitlines():
        if 'spectated' in l:
            scene = 'bot'
        m = re.match(r'(head|hand_r|foot_l|foot_r)\s+p50\s+([\d.]+) p99\s+([\d.]+) max\s+([\d.]+) spikes\s+(\d+) \(([\d.]+)%\)', l)
        if m:
            rows.setdefault((scene, m[1]), []).append(tuple(float(x) for x in m.groups()[1:]))
        m = re.match(r'clip changes \d+ per min (\d+)', l)
        if m:
            rows.setdefault((scene, 'clips/min'), []).append((float(m[1]),) * 5)
print(f'{"":16}{"p50":>7}{"p99":>8}{"max":>8}{"spike%":>8}')
for k, v in rows.items():
    med = [st.median(x[c] for x in v) for c in (0, 1, 2, 4)]
    print('%-16s%7.1f%8.1f%8.1f%8.1f' % (k[0] + ' ' + k[1], *med))
