#!/usr/bin/env python3
"""Run independent moving-roof traversal bots and preserve their transfer histories."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "engine/build/Release"
LINES = {"center": 1, "high": 2, "low": 3, "weave": 4}
EVENT = re.compile(r"^TRUCKTEST (\w+) (\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)$", re.M)


def trial(line, seed, args, maps):
    with tempfile.TemporaryDirectory(prefix="s64-line-") as tmp:
        base = Path(tmp) / "baseoa"
        base.mkdir()
        (base / "maps").symlink_to(maps, target_is_directory=True)
        mapname = f"truck_line_{seed}"
        library = base / "qagame.dylib"
        shutil.copy2(BUILD / "baseq3" / library.name, library)
        subprocess.run(["codesign", "-f", "-s", "-", library], check=True, capture_output=True)
        settings = dict(com_basegame="baseoa", fs_homepath=tmp, fs_basepath=args.assets,
                        net_enabled=0, sv_pure=0, vm_game=0, bot_enable=1, sv_fps=60,
                        g_timeBind=0, g_hotFloor=0, g_voidRise=0, g_truckRiders=0,
                        g_truckBots=0, bot_minplayers=0, g_gametype=0, fraglimit=0,
                        timelimit=0, g_truckTestLine=LINES[line], g_truckTestSeed=seed)
        cmd = [str(BUILD / "ioq3ded")]
        for name, value in settings.items():
            cmd.extend(["+set", name, str(value)])
        cmd.extend(["+map", mapname, "+addbot", "Assassin", "4"])
        with tempfile.TemporaryFile(mode="w+") as log:
            with subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT,
                                  text=True) as proc:
                try:
                    proc.wait(timeout=args.seconds)
                except subprocess.TimeoutExpired:
                    proc.stdin.write("quit\n")
                    proc.stdin.flush()
                    try:
                        proc.wait(5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
            if proc.returncode:
                raise RuntimeError(f"{line}: server exited {proc.returncode}")
            log.seek(0)
            output = log.read()
    events = [dict(event=e, ms=int(t), source=int(s), target=int(d),
                   source_lane=int(a), target_lane=int(b), contact=int(c)) for e,t,s,d,a,b,c in EVENT.findall(output)]
    counts = Counter(e["event"] for e in events)
    if not counts["attempt"]:
        raise RuntimeError(f"{line}: no measured traversal attempts\n{output[-2200:]}")
    # Pair each departure with exactly one outcome. A fall precedes its rescue.
    attempts = []
    active = None
    for event in events:
        kind = event["event"]
        if kind == "attempt":
            if active is not None:
                raise RuntimeError("Overlapping departures in telemetry")
            active = dict(start=event["ms"], source=event["source"], target=event["target"],
                          source_lane=event["source_lane"], target_lane=event["target_lane"])
            attempts.append(active)
        elif active is not None and kind in {"land", "wrong_deck", "fall", "rescue", "timeout", "respawn", "recycled"}:
            if kind == "land" and event["contact"] != active["target"]:
                raise RuntimeError("Landing has no matching target contact")
            active.update(outcome=kind, duration_ms=event["ms"]-active["start"], contact=event["contact"])
            active = None
    if active:
        active["outcome"] = "in_progress"
    return dict(line=line, seed=seed, counts=dict(counts), attempts=attempts, events=events)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lines", nargs="+", choices=LINES, default=list(LINES))
    parser.add_argument("--layout", choices=("fleet","building"), default="fleet")
    parser.add_argument("--difficulty", type=int, choices=(0, 1, 2), default=1)
    parser.add_argument("--assets", default=os.environ.get("OA", ROOT / "assets/openarena"))
    parser.add_argument("--seeds", nargs="+", type=int, default=[7])
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds < 3:
        parser.error("--seconds must be at least 3")
    with tempfile.TemporaryDirectory(prefix="truck-maps-") as tmp, ThreadPoolExecutor(max_workers=4) as pool:
        maps = Path(tmp)
        for seed in set(args.seeds):
            subprocess.run([sys.executable, ROOT / "tools/strafegen/strafegen.py", str(seed),
                            "--station-blocks" if args.layout == "building" else "--trucks", "--difficulty", str(args.difficulty), "--theme", "default",
                            "--name", f"truck_line_{seed}", "--out", maps], cwd=maps, check=True)
            if not (maps / f"truck_line_{seed}.aas").is_file():
                raise RuntimeError("Navigation build failed")
        jobs = [pool.submit(trial, line, seed, args, maps) for seed in args.seeds for line in args.lines]
        results = [job.result() for job in jobs]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(layout=args.layout, difficulty=args.difficulty, wall_seconds=args.seconds, trials=results), indent=2)+"\n")
    for row in results:
        print(row["line"], row["seed"], row["counts"])


if __name__ == "__main__":
    main()
