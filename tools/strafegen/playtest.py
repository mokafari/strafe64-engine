#!/usr/bin/env python3
"""STRAFE 64 bot playtest harness (Phase 1, report-only).

Fans out headless `ioq3ded` instances with `g_playtest 1`, lets bots play a
map for a while, then aggregates the per-bot telemetry the modded qagame
writes (playtest/<tag>.jsonl) into a readable report: completion rate, death
causes, flow proxies, moveset usage, and void tension — per map and skill.

    ./playtest.py                                  # default sweep
    ./playtest.py --maps strafe64_1337 --skills 3,5 --runs 2 --dur 30
    ./playtest.py --bots 4 --parallel 6

Bots already use the STRAFE 64 moveset (bhop/wallrun/double-jump), so this
measures the real movement, not vanilla Q3 nav.
"""

import argparse, json, os, shutil, subprocess, sys, tempfile, time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor

ENGINE = "/Users/gustav/ioquake3/build/Release"
OA = "/Users/gustav/openarena-0.8.8"
DED = os.path.join(ENGINE, "ioq3ded")
BUILD_BASEQ3 = os.path.join(ENGINE, "baseq3")

DEFAULT_MAPS = ["strafe64_1337", "strafe64dm_1337", "strafe64_7"]
DEFAULT_SKILLS = [2, 4]

# fitness target bands (see the design): a metric outside its band is flagged
BANDS = {
    "completion_pct": (40, 85),   # not trivial, not impossible (skilled tier)
    "flow_pct":       (45, 100),  # course supports sustained momentum
    "void_share_pct": (25, 70),   # the floor is a real but not sole killer
}


def deploy_dylibs():
    """Make sure the latest modded qagame (with telemetry) is in baseoa."""
    for dll in ("qagame.dylib", "cgame.dylib", "ui.dylib"):
        src = os.path.join(BUILD_BASEQ3, dll)
        if os.path.exists(src):
            dst = os.path.join(OA, "baseoa", dll)
            shutil.copy2(src, dst)
            # re-sign: Apple Silicon SIGKILLs an invalid-signature dylib on load
            subprocess.run(["codesign", "-f", "-s", "-", dst], capture_output=True)


def run_job(job, batch_dir, dur, bots):
    """Launch one headless server, let bots play, quit cleanly, return records."""
    idx, mapname, skill, run = job
    tag = f"{mapname}_s{skill}_r{run}"
    home = os.path.join(batch_dir, tag)
    os.makedirs(os.path.join(home, "baseoa"), exist_ok=True)
    port = 27970 + idx
    logf = open(os.path.join(batch_dir, tag + ".log"), "w")

    args = [
        DED,
        "+set", "com_basegame", "baseoa",
        "+set", "fs_basepath", OA,
        "+set", "fs_homepath", home,
        "+set", "sv_pure", "0",
        "+set", "vm_game", "0",
        "+set", "dedicated", "1",
        "+set", "net_port", str(port),
        "+set", "sv_maxclients", "16",
        "+set", "bot_enable", "1",
        "+set", "g_spSkill", str(skill),
        "+set", "g_playtest", "1",
        "+set", "g_playtestTag", tag,
        "+set", "g_voidRise", "1",
        "+set", "timelimit", "0",
        "+set", "fraglimit", "0",
        "+map", mapname,
    ]
    # explicit +addbot fills reliably (bot_minplayers caps ~2 on dedicated)
    _BOTS = ["angelyss", "arachna", "ayumi", "beret", "dark", "gargoyle",
             "grism", "kyonshi", "merman", "penguin"]
    for i in range(bots):
        args += ["+addbot", _BOTS[i % len(_BOTS)], "4"]

    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=logf,
                            stderr=subprocess.STDOUT)
    time.sleep(dur)
    # clean shutdown so still-running bots flush a "timeout" summary
    try:
        proc.stdin.write(b"quit\n")
        proc.stdin.flush()
    except Exception:
        pass
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
    logf.close()

    out = os.path.join(home, "baseoa", "playtest", tag + ".jsonl")
    records = []
    if os.path.exists(out):
        with open(out) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    r = json.loads(line)
                    r["_map"], r["_skill"] = mapname, skill
                    records.append(r)
                except json.JSONDecodeError:
                    pass
    return tag, records


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else 0.0


def band_flag(name, value):
    lo, hi = BANDS.get(name, (None, None))
    if lo is None:
        return ""
    return "  <-- OUT OF BAND" if (value < lo or value > hi) else ""


def report(groups):
    print("\n" + "=" * 64)
    print("STRAFE 64 — BOT PLAYTEST REPORT")
    print("=" * 64)
    for (mapname, skill), recs in sorted(groups.items()):
        n = len(recs)
        if not n:
            print(f"\n[{mapname}  skill {skill}]  no telemetry (bots never spawned?)")
            continue
        evs = Counter(r.get("ev") for r in recs)
        fin = evs.get("finish", 0)
        deaths = [r for r in recs if r.get("ev") == "death"]
        completion = 100.0 * fin / n
        causes = Counter(r.get("mod", "?") for r in deaths)
        void_deaths = causes.get("MOD_FALLING", 0)
        void_share = 100.0 * void_deaths / len(deaths) if deaths else 0.0

        flow = mean([r.get("flowpct") for r in recs])
        air = mean([r.get("airpct") for r in recs])
        wr = mean([r.get("wallrunpct") for r in recs])
        maxspd = mean([r.get("maxspd") for r in recs])
        maxbhop = max([r.get("maxbhop", 0) for r in recs], default=0)
        used_wr = 100.0 * sum(1 for r in recs if r.get("wallrunpct", 0) > 0) / n
        used_wj = 100.0 * sum(1 for r in recs if r.get("wj", 0) > 0) / n
        used_dj = 100.0 * sum(1 for r in recs if r.get("dj", 0) > 0) / n
        fins = [r.get("racems") for r in recs
                if r.get("ev") == "finish" and r.get("racems", -1) > 0]
        best = min(fins) if fins else None
        near = min([r.get("minvoid") for r in recs
                    if r.get("minvoid", -1) >= 0], default=None)
        stuck = mean([r.get("stuckms") for r in recs])

        print(f"\n[{mapname}  skill {skill}]   {n} bot-runs "
              f"({fin} finish / {len(deaths)} death / {evs.get('timeout',0)} timeout)")
        print(f"  completion : {completion:5.1f}%{band_flag('completion_pct', completion)}")
        print(f"  flow uptime: {flow:5.1f}%{band_flag('flow_pct', flow)}"
              f"   airborne {air:.0f}%   wallrun {wr:.0f}%")
        print(f"  speed      : avg-max {maxspd:.0f} ups   best bhop chain x{maxbhop}")
        print(f"  moveset use: wallrun {used_wr:.0f}%   walljump {used_wj:.0f}%   "
              f"doublejump {used_dj:.0f}%")
        if deaths:
            cause_str = ", ".join(f"{m} {c}" for m, c in causes.most_common())
            print(f"  deaths     : {cause_str}")
            print(f"  void share : {void_share:5.1f}%{band_flag('void_share_pct', void_share)}")
        if best is not None:
            print(f"  best time  : {best//60000}:{(best//1000)%60:02d}.{best%1000:03d}"
                  f"  (par candidate)")
        if near is not None:
            print(f"  void tension: closest approach {near} units")
        if stuck > 500:
            print(f"  WARNING: avg {stuck:.0f}ms stuck — friction / nav trouble")
    print("\n" + "=" * 64 + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", default=",".join(DEFAULT_MAPS))
    ap.add_argument("--skills", default=",".join(map(str, DEFAULT_SKILLS)))
    ap.add_argument("--runs", type=int, default=1)
    ap.add_argument("--dur", type=int, default=25, help="seconds per run")
    ap.add_argument("--bots", type=int, default=3)
    ap.add_argument("--parallel", type=int, default=4)
    ap.add_argument("--keep", action="store_true", help="keep the batch tmp dir")
    args = ap.parse_args()

    maps = [m for m in args.maps.split(",") if m]
    skills = [int(s) for s in args.skills.split(",") if s]

    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED} — build the engine first")
    deploy_dylibs()

    batch_dir = tempfile.mkdtemp(prefix="strafe64_playtest_")
    jobs, idx = [], 0
    for m in maps:
        for s in skills:
            for r in range(args.runs):
                jobs.append((idx, m, s, r)); idx += 1

    print(f"running {len(jobs)} jobs  ({len(maps)} maps x {len(skills)} skills "
          f"x {args.runs} runs, {args.bots} bots, {args.dur}s each, "
          f"{args.parallel} parallel)")
    print(f"batch dir: {batch_dir}")

    groups = defaultdict(list)
    with ThreadPoolExecutor(max_workers=args.parallel) as ex:
        futs = [ex.submit(run_job, j, batch_dir, args.dur, args.bots) for j in jobs]
        for fu in futs:
            tag, recs = fu.result()
            print(f"  done: {tag}  ({len(recs)} bot-runs)")
            for r in recs:
                groups[(r["_map"], r["_skill"])].append(r)

    report(groups)
    if not args.keep:
        shutil.rmtree(batch_dir, ignore_errors=True)
    else:
        print(f"batch dir kept: {batch_dir}")


if __name__ == "__main__":
    main()
