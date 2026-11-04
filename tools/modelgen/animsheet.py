#!/usr/bin/env python3
"""animsheet.py -- contact sheets of every mannequin clip, rendered in-engine on
an isolated test model in strafe64_animlab, plus the wall-run gait.

    animsheet.py [--home DIR] [--out DIR] [--only wall|clips] [--cols N]

DIR needs the model, game modules and lab pk3 (default generated/iqmtest).
"""
import argparse, glob, math, os, re, subprocess
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = ROOT + '/engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3'

def clips():
    h = open(ROOT + '/engine/code/cgame/cg_skelclips.h').read()
    return [(n, int(f), int(c)) for f, c, n in re.findall(r'\{ (\d+), (\d+), \w+ \},\s*// (\S+)', h)]

def capture(home, tag, shots, setup):
    """shots: [(label, console commands)]"""
    d = home + '/baseoa/screenshots'
    os.makedirs(d, exist_ok=True)
    for f in glob.glob(f'{d}/{tag}_*.jpg'):
        os.remove(f)
    lines = ['g_timeBind 0; g_hotFloor 0; cg_draw2D 0; cg_drawGun 0; wait 30; noclip; wait 5', setup]
    for i, (_, cmd) in enumerate(shots):
        lines.append(f'{cmd}; wait 6; screenshotJPEG {tag}_{i:03d}; wait 2')
    lines.append('wait 10; quit')
    open(home + '/baseoa/sheet.cfg', 'w').write('\n'.join(lines) + '\n')
    pid = home + '/baseoa/STRAFE 64.pid'
    if os.path.exists(pid):
        os.remove(pid)
    subprocess.run(['perl', '-e', 'alarm 240; exec @ARGV', BIN, '+set', 'com_basegame', 'baseoa',
        '+set', 'fs_basepath', ROOT + '/assets/openarena', '+set', 'fs_homepath', home, '+set', 'net_port', '27972',
        '+set', 'sv_pure', '0', '+set', 'vm_game', '0', '+set', 'vm_cgame', '0', '+set', 'vm_ui', '0',
        '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1', '+set', 'r_customwidth', '640', '+set', 'r_customheight', '480',
        '+devmap', 'strafe64_animlab', '+wait', '200', '+exec', 'sheet.cfg'],
        stdout=open(home + '/sheet.log', 'w'), stderr=subprocess.STDOUT, cwd=ROOT)
    if os.path.exists(pid):
        os.remove(pid)
    return [f'{d}/{tag}_{i:03d}.jpg' for i in range(len(shots))]

def sheet(files, labels, cols, out, cw=200, ch=200):
    rows = math.ceil(len(files) / cols)
    c = Image.new('RGB', (cols * cw, rows * ch), (20, 20, 24))
    dr = ImageDraw.Draw(c)
    for i, (f, l) in enumerate(zip(files, labels)):
        x, y = (i % cols) * cw, (i // cols) * ch
        if os.path.exists(f):
            im = Image.open(f).crop((120, 40, 520, 440)).resize((cw - 2, ch - 2))
            c.paste(im, (x, y))
        dr.text((x + 4, y + 3), l, fill=(255, 240, 80))
    c.save(out)
    print('wrote', out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--home', default=ROOT + '/generated/iqmtest')
    ap.add_argument('--out', default=ROOT + '/generated/animsheets')
    ap.add_argument('--only', default='')
    ap.add_argument('--cols', type=int, default=6)
    ap.add_argument('--match', default='', help='only clips whose name contains this')
    o = ap.parse_args()
    os.makedirs(o.out, exist_ok=True)
    base = 'setviewpos -200 800 -26 0; wait 5; setviewpos -200 800 -26 0; wait 5; testmodel models/s64/mannequin.iqm; wait 5; setviewpos -190 760 32 24; wait 10'
    if o.only != 'wall':
        shots = []
        for n, first, count in clips():
            if o.match and not any(m in n for m in o.match.split(',')):
                continue
            for k in range(o.cols):
                f = first + (count - 1) * k // (o.cols - 1)
                shots.append((f'{n} {f - first}/{count}', f'setviewpos -190 760 32 24; set cg_testFrame {f}'))
        files = capture(o.home, 'clip', shots, base)
        sheet(files, [l for l, _ in shots], o.cols, o.out + '/clips.png')
    if o.only != 'clips':
        wall = 'setviewpos 100 465 0 180; wait 5; setviewpos 100 465 0 180; wait 5; testmodel models/s64/mannequin.iqm; wait 10'
        sprint = next(c for c in clips() if c[0] == 'Sprint_Loop')
        shots = []
        for view, pos in (('side', '-40 340 60 72'), ('along', '-130 410 50 18')):
            for k in range(8):
                a = 2 * math.pi * k / 8
                shots.append((f'{view} {k * 45} deg', f'setviewpos {pos}; set cg_testFrame {sprint[1] + sprint[2] * k // 8}; set cg_testWallRun {a:.3f}'))
        files = capture(o.home, 'wall', shots, wall)
        sheet(files, [l for l, _ in shots], 8, o.out + '/wallrun.png')

if __name__ == '__main__':
    main()
