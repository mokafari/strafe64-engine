#!/usr/bin/env python3
"""hqcap.py -- 1080p slow-motion inspection of a real, played move.

    hqcap.py wall_left|wall_right|run|land [--out DIR] [--range N] [--angle DEG]

Plays a scripted move on the live local player with the clock at a tenth, takes a
1080p frame about every 17ms of game time, then writes:
  NAME.mp4       full clip at 30fps (about 2x slower than real time)
  NAME_sheet.jpg 1200px-tall strip, cropped to the character
With --angles 0,90,180,270 the move is replayed per camera angle (degrees around
the character, 0 = behind) into one audition grid, NAME_angles.jpg. --set "cvar val"
runs before the move, for comparing modes.
"""
import argparse, glob, os, subprocess
from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BIN = ROOT + '/engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3'
HOME = ROOT + '/generated/iqmtest'
MOVES = {
    # start pos, keys before filming, frames of lead-in at full speed, camera angle
    'wall_left':  ('-1050 464 40 0', '+forward; wait 60; +moveup; wait 2; -moveup; wait 8', 300),
    'wall_right': ('1050 464 40 180', '+forward; wait 60; +moveup; wait 2; -moveup; wait 8', 60),
    'flip':       ('-1250 800 40 0', '+forward; wait 70; +moveup; wait 2; -moveup; wait 16; +moveup; wait 2; -moveup', 90),
    'roll':       ('-300 800 400 0', '+forward; wait 48', 90),
    'slide':      ('-1250 800 40 0', '+forward; wait 70; +movedown; wait 12', 90),
    'crouch':     ('-1250 800 40 0', '+movedown; wait 170', 90),
    'crouchwalk': ('-1250 800 40 0', '+movedown; wait 170; +forward; wait 60', 90),
    'wallkick':   ('-1050 464 40 0', '+forward; wait 60; +moveup; wait 2; -moveup; wait 20; +moveup; wait 2; -moveup; wait 3', 90),
    'run':        ('-1250 800 40 0', '+forward; wait 90', 90),
    'land':       ('300 800 400 0', 'wait 48', 90),
}

def film(o, angle):
    """one run of the move from one angle; returns its frames"""
    pos, keys, _ = MOVES[o.move]
    d = HOME + '/baseoa/screenshots'
    for f in glob.glob(d + '/hq_*.jpg'):
        os.remove(f)
    cfg = ['g_hotFloor 0; g_timeBind 0; cg_draw2D 0; cg_drawGun 0; cg_thirdPerson 1; r_dof 0; cg_killcam 0',
           o.set, f'cg_thirdPersonRange {o.range}; cg_thirdPersonAngle {angle}; wait 40',
           f'setviewpos {pos}; wait 5; setviewpos {pos}; wait 10', keys, 'timescale 0.1']
    cfg += [f'wait 10; screenshotJPEG hq_{i:03d}' for i in range(o.shots)]
    cfg += ['timescale 1; wait 5; quit']
    open(HOME + '/baseoa/hq.cfg', 'w').write('\n'.join(cfg) + '\n')
    pid = HOME + '/baseoa/STRAFE 64.pid'
    if os.path.exists(pid):
        os.remove(pid)
    subprocess.run(['perl', '-e', 'alarm 150; exec @ARGV', BIN, '+set', 'com_basegame', 'baseoa',
        '+set', 'fs_basepath', ROOT + '/assets/openarena', '+set', 'fs_homepath', HOME, '+set', 'net_port', '27973',
        '+set', 'sv_pure', '0', '+set', 'vm_game', '0', '+set', 'vm_cgame', '0', '+set', 'vm_ui', '0',
        '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1', '+set', 'r_customwidth', '1920', '+set', 'r_customheight', '1080',
        '+set', 'r_jpegQuality', '95', '+set', 'r_ext_multisample', '4', '+set', 'com_maxfps', '60',
        '+devmap', 'strafe64_animlab', '+wait', '200', '+exec', 'hq.cfg'],
        stdout=open(HOME + '/hq.log', 'w'), stderr=subprocess.STDOUT, cwd=ROOT)
    if os.path.exists(pid):
        os.remove(pid)
    return sorted(glob.glob(d + '/hq_*.jpg'))

def run_angles(o):
    """rows = camera angles, columns = moments in the move"""
    os.makedirs(o.out, exist_ok=True)
    angles = [int(a) for a in o.angles.split(',')]
    cols, cw, ch = 7, 300, 300
    grid = Image.new('RGB', (cols * cw, len(angles) * ch), (16, 16, 20))
    dr = ImageDraw.Draw(grid)
    for r, ang in enumerate(angles):
        files = film(o, ang)
        for c in range(cols):
            f = files[min(len(files) - 1, 6 + c * (len(files) - 7) // (cols - 1))]
            im = Image.open(f).crop((560, 200, 1360, 1000)).resize((cw - 2, ch - 2))
            grid.paste(im, (c * cw, r * ch))
        dr.text((6, r * ch + 4), f'angle {ang}', fill=(255, 240, 80))
    tag = o.tag or o.move
    grid.save(f'{o.out}/{tag}_angles.jpg', quality=92)
    print('wrote', tag)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('move', choices=MOVES)
    ap.add_argument('--out', default=ROOT + '/generated/hq')
    ap.add_argument('--range', type=int, default=75)
    ap.add_argument('--angle', type=int)
    ap.add_argument('--shots', type=int, default=48)
    ap.add_argument('--angles', default='')
    ap.add_argument('--set', default='')
    ap.add_argument('--tag', default='')
    o = ap.parse_args()
    if o.angles:
        run_angles(o)
        return
    pos, keys, angle = MOVES[o.move]
    angle = angle if o.angle is None else o.angle
    os.makedirs(o.out, exist_ok=True)
    d = HOME + '/baseoa/screenshots'
    for f in glob.glob(d + '/hq_*.jpg'):
        os.remove(f)
    cfg = ['g_hotFloor 0; g_timeBind 0; cg_draw2D 0; cg_drawGun 0; cg_thirdPerson 1; r_dof 0; cg_killcam 0',
           f'cg_thirdPersonRange {o.range}; cg_thirdPersonAngle {angle}; wait 40',
           f'setviewpos {pos}; wait 5; setviewpos {pos}; wait 10', keys, 'timescale 0.1']
    cfg += [f'wait 10; screenshotJPEG hq_{i:03d}' for i in range(o.shots)]
    cfg += ['timescale 1; wait 5; quit']
    open(HOME + '/baseoa/hq.cfg', 'w').write('\n'.join(cfg) + '\n')
    pid = HOME + '/baseoa/STRAFE 64.pid'
    if os.path.exists(pid):
        os.remove(pid)
    subprocess.run(['perl', '-e', 'alarm 150; exec @ARGV', BIN, '+set', 'com_basegame', 'baseoa',
        '+set', 'fs_basepath', ROOT + '/assets/openarena', '+set', 'fs_homepath', HOME, '+set', 'net_port', '27973',
        '+set', 'sv_pure', '0', '+set', 'vm_game', '0', '+set', 'vm_cgame', '0', '+set', 'vm_ui', '0',
        '+set', 'r_fullscreen', '0', '+set', 'r_mode', '-1', '+set', 'r_customwidth', '1920', '+set', 'r_customheight', '1080',
        '+set', 'r_jpegQuality', '95', '+set', 'r_ext_multisample', '4', '+set', 'com_maxfps', '60',
        '+devmap', 'strafe64_animlab', '+wait', '200', '+exec', 'hq.cfg'],
        stdout=open(HOME + '/hq.log', 'w'), stderr=subprocess.STDOUT, cwd=ROOT)
    if os.path.exists(pid):
        os.remove(pid)
    files = sorted(glob.glob(d + '/hq_*.jpg'))
    print(len(files), 'frames')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', '30', '-pattern_type', 'glob', '-i', d + '/hq_*.jpg',
        '-c:v', 'libx264', '-crf', '16', '-pix_fmt', 'yuv420p', f'{o.out}/{o.move}.mp4'])
    pick = files[:: max(1, len(files) // 12)][:12]
    cw, ch = 500, 600
    sheet = Image.new('RGB', (cw * 4, ch * 3))
    for i, f in enumerate(pick):
        im = Image.open(f)
        im = im.crop((560, 280, 1360, 1080)).resize((ch, ch)).crop((50, 0, 550, 600))
        sheet.paste(im, ((i % 4) * cw, (i // 4) * ch))
    sheet.save(f'{o.out}/{o.move}_sheet.jpg', quality=90)
    print('wrote', o.out)

if __name__ == '__main__':
    main()
