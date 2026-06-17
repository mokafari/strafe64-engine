#!/usr/bin/env python3
"""Auto PAR calibration — the MISSION REPORT's medal target, from the best bot run.

Runs bots across skill tiers on a map and takes the best peak speed (maxspd, from
g_playtest telemetry) as that map's PAR. The client registers a per-map archived
cvar `cgp_<map>` (cg_main.c) and `CG_DrawMissionReport` grades its S/A/B/C rank
against it (S = beat par; A/B at 0.85/0.70 par) — so a tight map's S-rank is earned
at a lower speed than a fast one's, instead of the old absolute 900/700/500.

    ./par_calibrate.py surf_64                 # print PAR + the `seta` line
    ./par_calibrate.py dojo_arena --dur 20 --out strafe64_par.cfg   # append a cfg
"""
import json, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ENGINE = os.environ.get("DOJO_ENGINE", os.path.join(REPO, "engine/build/Release"))
OA = os.environ.get("DOJO_OA", os.path.join(REPO, "assets/openarena"))
DED = os.path.join(ENGINE, "ioq3ded")
BOT_NAMES = ["angelyss", "arachna", "ayumi", "beret", "dark", "gargoyle",
             "grism", "kyonshi", "merman", "penguin"]


def run_tier(mapname, skill, dur, bots=5):
    home = tempfile.mkdtemp(prefix=f"par_{skill}_")
    os.makedirs(os.path.join(home, "baseoa"), exist_ok=True)
    tag = f"par_{mapname}_{skill}"
    logf = open(os.path.join(home, "con.log"), "w")
    args = [
        DED, "+set", "com_basegame", "baseoa", "+set", "fs_basepath", OA,
        "+set", "fs_homepath", home, "+set", "sv_pure", "0", "+set", "vm_game", "0",
        "+set", "dedicated", "1", "+set", "net_port", str(29000 + skill),
        "+set", "sv_maxclients", "16", "+set", "bot_enable", "1",
        "+set", "g_spSkill", str(skill), "+set", "g_playtest", "1",
        "+set", "g_playtestTag", tag, "+set", "g_voidRise", "0",
        "+set", "timelimit", "0", "+set", "fraglimit", "0", "+map", mapname,
    ]
    for i in range(bots):
        args += ["+addbot", BOT_NAMES[i % len(BOT_NAMES)], str(skill)]
    p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=logf,
                         stderr=subprocess.STDOUT)
    time.sleep(dur)
    try:
        p.stdin.write(b"quit\n"); p.stdin.flush()
    except Exception:
        pass
    try:
        p.wait(timeout=8)
    except subprocess.TimeoutExpired:
        p.terminate()
        try: p.wait(timeout=4)
        except subprocess.TimeoutExpired: p.kill()
    logf.close()
    jsonl = os.path.join(home, "baseoa", "playtest", tag + ".jsonl")
    best = 0
    if os.path.exists(jsonl):
        for line in open(jsonl):
            line = line.strip()
            if not line:
                continue
            try:
                best = max(best, json.loads(line).get("maxspd", 0))
            except json.JSONDecodeError:
                pass
    shutil.rmtree(home, ignore_errors=True)
    return best


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("-"):
        print("usage: par_calibrate.py <map> [--dur N] [--out file.cfg]")
        return
    mapname, dur, out, i = a[0], 25, None, 1
    while i < len(a):
        if a[i] == "--dur":
            dur = int(a[i + 1]); i += 2
        elif a[i] == "--out":
            out = a[i + 1]; i += 2
        else:
            i += 1
    print(f"PAR calibration  map={mapname}  skills 1-4  {dur}s each\n")
    best = 0
    for sk in (1, 2, 3, 4):
        b = run_tier(mapname, sk, dur)
        print(f"  skill {sk}: best maxspd {b}")
        best = max(best, b)
    par = int(round(best))
    line = f"seta cgp_{mapname} {par}"
    print(f"\nPAR ({mapname}) = {par} ups  (best bot peak across tiers)")
    if out:
        with open(out, "a") as f:
            f.write(line + "\n")
        print(f"appended `{line}` -> {out}")
    else:
        print(f"apply with: {line}")


if __name__ == "__main__":
    main()
