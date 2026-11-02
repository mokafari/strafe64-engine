"""Isolated Windows game smoke run, with local-only console control and captures.

Example: py -3.13 tools/strafegen/windows_smoke.py --tag audio --sound --duration 30
Build first. Copies binaries and native game DLLs so later builds remain possible.
Uses existing OpenArena assets and copies only an installed license setting into
the test profile. Does not modify the player's profile or game configuration.
"""
import argparse
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', default='smoke')
    parser.add_argument('--map', default='strafe64_showcase')
    parser.add_argument('--renderer', choices=('opengl1', 'opengl2'), default='opengl2')
    parser.add_argument('--duration', type=float, default=20)
    parser.add_argument('--sound', action='store_true')
    parser.add_argument('--menu', action='store_true', help='capture the main menu instead of a map')
    parser.add_argument('--set', action='append', default=[], metavar='NAME=VALUE')
    parser.add_argument('--command', action='append', default=[])
    parser.add_argument('--exec-file', action='append', default=[], type=Path,
                        help='copy a local cfg into the test profile and execute after map readiness')
    parser.add_argument('--at', action='append', default=[], metavar='SECONDS:COMMAND')
    args = parser.parse_args()
    scheduled = []
    for entry in args.at:
        seconds, sep, text = entry.partition(':')
        if not sep:
            parser.error('--at expects SECONDS:COMMAND')
        scheduled.append((float(seconds), text))
    scheduled.sort()
    if os.name != 'nt':
        parser.error('this runner requires Windows')
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.tag) or not 1 <= args.duration <= 3600:
        parser.error('use a simple tag and a duration from 1 to 3600 seconds')
    root = Path(__file__).resolve().parents[2]
    release = root / 'engine/build/Release'
    output = root / 'engine/build' / f'polish-{args.tag}-{time.time_ns()}'
    binary = output / 'bin'
    home = output / 'home'
    baseoa = home / 'baseoa'
    binary.mkdir(parents=True)
    baseoa.mkdir(parents=True)
    for pattern in ('*.exe', '*.dll'):
        for src in release.glob(pattern):
            shutil.copy2(src, binary / src.name)
    for src in (release / 'baseq3').glob('*.dll'):
        shutil.copy2(src, baseoa / src.name)
    for src in Path(__file__).parent.glob('*.cfg'):
        shutil.copy2(src, baseoa / src.name)
    for index, src in enumerate(args.exec_file):
        name = f'smoke-extra-{index}.cfg'
        shutil.copy2(src, baseoa / name)
        args.command.insert(index, f'exec {name}')
    profile = Path(os.environ['APPDATA']) / 'Quake3/baseoa/q3config.cfg'
    if profile.is_file():
        lines = [line for line in profile.read_text(errors='replace').splitlines()
                 if re.fullmatch(r'seta cl_licenseKey "[^"]+"', line)]
        (baseoa / 'q3config.cfg').write_text('\n'.join(lines) + '\n')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    password = secrets.token_hex(16)
    cvars = dict(com_basegame='baseoa', fs_basepath=str(root / 'assets/openarena'),
                 fs_homepath=str(home), sv_pure='0', vm_game='0', vm_cgame='0', vm_ui='0',
                 r_fullscreen='0', r_mode='4', logfile='2', con_notifytime='0', cl_renderer=args.renderer,
                 s_initsound=str(int(args.sound)), s_volume='0.02', s_musicVolume='0.02',
                 net_ip='127.0.0.1', net_enabled='1', net_port=str(port),
                 rconPassword=password, com_maxfps='120', com_maxfpsUnfocused='120')
    for value in args.set:
        key, sep, val = value.partition('=')
        if not sep:
            parser.error('--set expects NAME=VALUE')
        cvars[key] = val
    command = [str(binary / 'ioquake3.exe')]
    for key, value in cvars.items():
        command += ['+set', key, value]
    if args.menu:
        # Console startup commands are processed after renderer/UI startup.
        command += ['+wait', '180', '+screenshotJPEG', f'{args.tag}-menu', '+wait', '30', '+quit']
    else:
        command += ['+devmap', args.map]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    process = subprocess.Popen(command, cwd=root, startupinfo=startup)
    log = baseoa / 'qconsole.log'
    print(f'Output: {output}', flush=True)

    def console(text):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(0.25)
            sock.sendto(b'\xff' * 4 + f'rcon {password} {text}\n'.encode(), ('127.0.0.1', port))
            replies = []
            try:
                while True:
                    packet = sock.recv(65535)
                    if packet.startswith(b'\xff' * 4):
                        packet = packet[4:]
                    replies.append(packet.decode(errors='replace'))
            except socket.timeout:
                pass
            with (output / 'commands.log').open('a', encoding='utf-8') as report:
                report.write(f'COMMAND: {text}\n' + '\n'.join(replies) + '\n')

    try:
        if args.menu:
            process.wait(timeout=60)
            capture = baseoa / 'screenshots' / f'{args.tag}-menu.jpg'
            if process.returncode or not capture.is_file():
                raise RuntimeError('main menu capture failed')
            content = log.read_text(errors='replace')
            if re.search(r'^ERROR:', content, re.MULTILINE):
                raise RuntimeError('main menu reported an engine error')
            print(f'Finished: main menu captured; exit {process.returncode}', flush=True)
            return
        deadline = time.monotonic() + 60
        while process.poll() is None:
            content = log.read_text(errors='replace') if log.exists() else ''
            # Team games initially place the pilot in spectator mode and do
            # not print "entered the game" until a team is chosen.
            if 'CL_InitCGame:' in content:
                break
            if time.monotonic() > deadline:
                raise RuntimeError('map did not become playable within 60 seconds')
            time.sleep(0.25)
        if process.poll() is not None:
            raise RuntimeError(f'game exited before map readiness: {process.returncode}')
        for cmd in args.command:
            console(cmd)
            time.sleep(0.3)
        started = time.monotonic()
        next_capture = 0
        while time.monotonic() - started < args.duration:
            if process.poll() is not None:
                raise RuntimeError(f'game exited during smoke run: {process.returncode}')
            content = log.read_text(errors='replace')
            errors = re.findall(r'^ERROR:.*$', content, re.MULTILINE)
            if errors:
                raise RuntimeError(errors[-1])
            elapsed = time.monotonic() - started
            while scheduled and elapsed >= scheduled[0][0]:
                _, text = scheduled.pop(0)
                console(text)
            if elapsed >= next_capture:
                console(f'screenshotJPEG {args.tag}-{int(elapsed):04d}')
                print(f'{elapsed:.0f}s: game running; capture requested', flush=True)
                next_capture += 60
            time.sleep(0.5)
        console(f'screenshotJPEG {args.tag}-final')
        time.sleep(0.5)
        console('quit')
        process.wait(timeout=15)
        print(f'Finished: exit {process.returncode}', flush=True)
        if process.returncode:
            raise RuntimeError('nonzero game exit')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


if __name__ == '__main__':
    main()
