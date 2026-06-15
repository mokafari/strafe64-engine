#!/usr/bin/env python3
"""STRAFE 64 — the Bot Dojo: a reflective game-feel research loop.

Adapts the crypto-autoresearch *discipline* (sanity gate, provenance journal,
discrete outcome classes, no-regression acceptance, termination-as-deliverable)
to a CONTINUOUS metric: distance to a target dossier of "the gameplay we want".

Four dojo scenarios isolate one archetype each (speed / flow / ztrick / arena).
The loop runs the battery, gates each scenario (don't trust a broken dojo's
numbers), classifies each archetype against the dossier, and journals every
iteration to dojo_runs.jsonl.

    ./dojo.py                         # run battery, calibration report + journal
    ./dojo.py --label air-accel-1.3   # tag this iteration's config in the journal
    ./dojo.py --dur 30 --parallel 4

Phase 1 is human-in-the-loop: it recommends, you decide and commit the param
change, then re-run to confirm (no regression on other archetypes).
"""

import argparse, hashlib, json, os, shutil, subprocess, sys, tempfile, time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = "/Users/gustav/ioquake3/build/Release"
OA = "/Users/gustav/openarena-0.8.8"
DED = os.path.join(ENGINE, "ioq3ded")
BUILD_BASEQ3 = os.path.join(ENGINE, "baseq3")
JOURNAL = os.path.join(HERE, "dojo_runs.jsonl")
DOSSIER_PATH = os.path.join(HERE, "dojo_dossier.json")

# Each archetype: which map, void on/off, how many bots, and the metrics that
# define it. "agg" = how to reduce per-bot-run records to one scenario number.
# The dedicated server's bot_minplayers only fills ~2 bots; explicit +addbot
# fills reliably. Real OA bot characters (botfiles/bots/<name>_c.c).
BOT_NAMES = ["angelyss", "arachna", "ayumi", "beret", "dark", "gargoyle",
             "grism", "kyonshi", "merman", "penguin", "sergei", "tony"]

ARCHETYPES = {
    # bot_combatBhop defaults ON now (real DM/arena bots fight at speed); the
    # isolated MOVEMENT tests force it off so combat can't perturb pure traversal
    "speed":  {"map": "dojo_speed",  "void": 0, "bots": 5,
               "extra": ["bot_combatBhop", "0"]},
    "flow":   {"map": "dojo_flow",   "void": 0, "bots": 5,
               "extra": ["bot_combatBhop", "0"]},   # void off: measure flow
    "ztrick": {"map": "dojo_ztrick", "void": 0, "bots": 5,
               "extra": ["bot_combatBhop", "0"]},
    "arena":  {"map": "dojo_arena",  "void": 0, "bots": 12,
               "extra": ["g_vectorgun", "1"]},   # rail; combat-bhop on by default
    # clustertruck/trackmania sections (movement isolation, combat off)
    "slalom":  {"map": "dojo_slalom",  "void": 0, "bots": 5,
                "extra": ["bot_combatBhop", "0"]},
    "hurdles": {"map": "dojo_hurdles", "void": 0, "bots": 5,
                "extra": ["bot_combatBhop", "0"]},
    "movers":  {"map": "dojo_movers",  "void": 0, "bots": 5,
                "extra": ["bot_combatBhop", "0"]},
    "fork":    {"map": "dojo_fork",    "void": 0, "bots": 5,
                "extra": ["bot_combatBhop", "0"]},
    "hazard":  {"map": "dojo_hazard",  "void": 0, "bots": 5,
                "extra": ["bot_combatBhop", "0"]},
    "showcase": {"map": "dojo_showcase", "void": 0, "bots": 5,
                 "extra": ["bot_combatBhop", "0"]},
}

# The TARGET DOSSIER — what "good" is, as bands. These are design targets to
# review against the baseline and edit (written to dojo_dossier.json on first
# run). band = [lo, hi]; null end = unbounded.
# BOT dossier = traversal-quality regression baselines (set at current good
# bot behaviour; a change that makes bots stall/slow/stop fighting trips them).
# The HUMAN design targets are aspirational and bots structurally can't hit
# them (they don't strafe-jump to 800, wall-run, formally finish races, or
# frag at run speed) — those are validated by human playtest, documented in
# ROADMAP, NOT gated here.
DEFAULT_DOSSIER = {
    "speed":  {"maxspd": [400, None], "flowpct": [15, None]},
    "flow":   {"flowpct": [30, None], "maxbhop": [3, None],
               "stuckms": [None, 2000]},   # band set from observed good-behaviour
                                           # spread (850-1900); 2388+ = broken nav.
                                           # 1500 sat at the noise centre -> false
                                           # PARTIALs (iter21 recalibration)
    "ztrick": {"flowpct": [40, None], "maxspd": [350, None],
               "stuckms": [None, 1500]},   # ztrick reliably <1300, kept tight
    "arena":  {"frags_per_min": [0.5, None], "midair_pct": [15, None]},
    # provisional bands for the new sections (set from the tuning baseline;
    # traversal-quality regression guards, comparable to flow/ztrick)
    "slalom":  {"flowpct": [25, None], "maxspd": [350, None], "stuckms": [None, 4000]},
    "hurdles": {"flowpct": [40, None], "maxspd": [350, None], "stuckms": [None, 2500]},
    "movers":  {"flowpct": [40, None], "maxspd": [350, None], "stuckms": [None, 3000]},
    "fork":    {"flowpct": [45, None], "maxspd": [350, None], "stuckms": [None, 2500]},
    "hazard":  {"flowpct": [30, None], "maxspd": [350, None], "stuckms": [None, 3000]},
    "showcase": {"flowpct": [28, None], "maxspd": [350, None], "stuckms": [None, 3500]},
}


def sh(cmd):
    try:
        return subprocess.check_output(cmd, cwd=HERE, text=True).strip()
    except Exception:
        return "?"


def deploy_dylibs():
    # Apple Silicon SIGKILLs a dylib with an invalid ad-hoc signature on
    # dlopen ("Code Signature Invalid") — a plain cp can invalidate it, so
    # re-sign every deployed copy or the dedicated server dies on load.
    for dll in ("qagame.dylib", "cgame.dylib", "ui.dylib"):
        src = os.path.join(BUILD_BASEQ3, dll)
        if os.path.exists(src):
            dst = os.path.join(OA, "baseoa", dll)
            shutil.copy2(src, dst)
            subprocess.run(["codesign", "-f", "-s", "-", dst],
                           capture_output=True)


def run_scenario(arch, cfg, batch_dir, dur, idx):
    mapname, void, bots = cfg["map"], cfg["void"], cfg["bots"]
    tag = f"dojo_{arch}_{idx}"      # idx-isolated: unique home/jsonl/port per run
    home = os.path.join(batch_dir, tag)
    os.makedirs(os.path.join(home, "baseoa"), exist_ok=True)
    logf = open(os.path.join(batch_dir, tag + ".log"), "w")
    args = [
        DED,
        "+set", "com_basegame", "baseoa", "+set", "fs_basepath", OA,
        "+set", "fs_homepath", home, "+set", "sv_pure", "0",
        "+set", "vm_game", "0", "+set", "dedicated", "1",
        "+set", "net_port", str(27990 + idx), "+set", "sv_maxclients", "16",
        "+set", "bot_enable", "1",
        "+set", "g_spSkill", "4", "+set", "g_playtest", "1",
        "+set", "g_playtestTag", tag, "+set", "g_voidRise", str(void),
        "+set", "timelimit", "0", "+set", "fraglimit", "0",
    ]
    for k in range(0, len(cfg.get("extra", [])), 2):
        args += ["+set", cfg["extra"][k], cfg["extra"][k + 1]]
    args += ["+map", mapname]
    for i in range(bots):
        args += ["+addbot", BOT_NAMES[i % len(BOT_NAMES)], "4"]
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=logf,
                            stderr=subprocess.STDOUT)
    time.sleep(dur)
    try:
        proc.stdin.write(b"quit\n"); proc.stdin.flush()
    except Exception:
        pass
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try: proc.wait(timeout=4)
        except subprocess.TimeoutExpired: proc.kill()
    logf.close()

    out = os.path.join(home, "baseoa", "playtest", tag + ".jsonl")
    recs = []
    if os.path.exists(out):
        with open(out) as f:
            for line in f:
                line = line.strip()
                if line:
                    try: recs.append(json.loads(line))
                    except json.JSONDecodeError: pass
    return arch, recs


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else 0.0


def _median(xs):
    xs = sorted(xs)
    n = len(xs)
    if n == 0:
        return 0.0
    m = n // 2
    return xs[m] if n % 2 else (xs[m - 1] + xs[m]) / 2.0


def median_profiles(profiles):
    """Element-wise median across several single-run profiles — the variance
    guard: one unlucky run (a stuck bot, a frag-less window) can't flip a verdict
    when the other runs disagree."""
    return {k: _median([pr[k] for pr in profiles]) for k in profiles[0]}


def profile(arch, recs):
    """Reduce a scenario's bot-run records to its metric profile."""
    n = len(recs)
    evs = Counter(r.get("ev") for r in recs)
    fin = evs.get("finish", 0)
    durs = [r.get("durms", 0) for r in recs] or [1]
    p = {
        "n": n,
        "completion_pct": (100.0 * fin / n) if n else 0.0,
        "avgspd": mean([r.get("avgspd") for r in recs]),
        "maxspd": mean([r.get("maxspd") for r in recs]),
        "flowpct": mean([r.get("flowpct") for r in recs]),
        "airpct": mean([r.get("airpct") for r in recs]),
        "wallrunpct": mean([r.get("wallrunpct") for r in recs]),
        "maxbhop": max([r.get("maxbhop", 0) for r in recs], default=0),
        "stuckms": mean([r.get("stuckms") for r in recs]),
        "frags": sum(r.get("frags", 0) for r in recs),
        "midair": sum(r.get("midair", 0) for r in recs),
        "killspd": mean([r.get("killspd") for r in recs if r.get("frags", 0) > 0]),
    }
    total_min = sum(durs) / 60000.0
    p["frags_per_min"] = (p["frags"] / total_min) if total_min > 0 else 0.0
    p["midair_pct"] = (100.0 * p["midair"] / p["frags"]) if p["frags"] else 0.0
    return p


def sanity_gate(arch, p):
    """A scenario's numbers are trustworthy only if bots actually played it.
    Mirrors the synthetic-corpus gate: a NULL from a broken dojo poisons the
    journal. Returns (ok, reason)."""
    if p["n"] == 0:
        return False, "no telemetry (bots never spawned / map failed to load)"
    moved = p["avgspd"] > 80 or p["frags"] > 0 or p["completion_pct"] > 0
    if not moved:
        return False, f"bots inert (avgspd {p['avgspd']:.0f}, stuck {p['stuckms']:.0f}ms)"
    return True, "ok"


def classify(arch, p, dossier):
    """IN_DOSSIER / PARTIAL / OUT — per-archetype, vs the target bands."""
    bands = dossier.get(arch, {})
    if not bands:
        return "NO_DOSSIER", []
    misses = []
    for metric, (lo, hi) in bands.items():
        v = p.get(metric, 0.0)
        if (lo is not None and v < lo) or (hi is not None and v > hi):
            misses.append((metric, v, lo, hi))
    if not misses:
        return "IN_DOSSIER", []
    if len(misses) < len(bands):
        return "PARTIAL", misses
    return "OUT", misses


def fmt_band(lo, hi):
    if lo is not None and hi is not None: return f"[{lo}..{hi}]"
    if lo is not None: return f">={lo}"
    if hi is not None: return f"<={hi}"
    return "any"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archetypes", default="speed,flow,ztrick,arena")
    ap.add_argument("--dur", type=int, default=25, help="seconds per scenario")
    ap.add_argument("--parallel", type=int, default=4)
    ap.add_argument("--label", default="baseline", help="config tag for the journal")
    args = ap.parse_args()

    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")
    archs = [a for a in args.archetypes.split(",") if a in ARCHETYPES]

    # load (or seed) the target dossier
    if os.path.exists(DOSSIER_PATH):
        with open(DOSSIER_PATH) as f:
            dossier = json.load(f)
    else:
        dossier = DEFAULT_DOSSIER
        with open(DOSSIER_PATH, "w") as f:
            json.dump(dossier, f, indent=2)
        print(f"seeded target dossier -> {DOSSIER_PATH} (review/edit the bands)")

    deploy_dylibs()
    git = sh(["git", "rev-parse", "--short", "HEAD"])
    batch = tempfile.mkdtemp(prefix="strafe64_dojo_")
    print(f"dojo battery: {archs}  ({args.dur}s each, {args.parallel} parallel)")
    print(f"label={args.label}  git={git}")

    results = {}
    with ThreadPoolExecutor(max_workers=args.parallel) as ex:
        futs = [ex.submit(run_scenario, a, ARCHETYPES[a], batch, args.dur, i)
                for i, a in enumerate(archs)]
        for fu in futs:
            a, recs = fu.result()
            results[a] = profile(a, recs)
            print(f"  ran dojo_{a}: {results[a]['n']} bot-runs")

    # variance guard: a single short run's combat/stuck metrics are noisy enough
    # to false-flag a no-op change (seen iters 17/20/23/26). Re-run only the
    # archetypes that came back non-green (and actually played), then classify on
    # the MEDIAN across runs. Green archetypes stay single-run — keeps it fast.
    RERUN = 2
    runs_by_arch = {a: [results[a]] for a in archs}
    regrade = [a for a in archs
               if sanity_gate(a, results[a])[0]
               and classify(a, results[a], dossier)[0] != "IN_DOSSIER"]
    if regrade:
        print(f"\n  non-green {regrade} -> +{RERUN} runs each, median verdict (variance guard)")
        for r in range(RERUN):
            with ThreadPoolExecutor(max_workers=args.parallel) as ex:
                futs = [ex.submit(run_scenario, a, ARCHETYPES[a], batch, args.dur,
                                  100 + r * 10 + i)
                        for i, a in enumerate(regrade)]
                for fu in futs:
                    a, recs = fu.result()
                    runs_by_arch[a].append(profile(a, recs))
        for a in regrade:
            results[a] = median_profiles(runs_by_arch[a])
            print(f"  dojo_{a}: median of {len(runs_by_arch[a])} runs")

    # report + journal
    print("\n" + "=" * 66)
    print(f"STRAFE 64 — DOJO REPORT   (label={args.label}, git={git})")
    print("=" * 66)
    journal_rows = []
    for a in archs:
        p = results[a]
        ok, reason = sanity_gate(a, p)
        verdict, misses = (("BROKEN", []) if not ok else classify(a, p, dossier))
        print(f"\n[{a.upper()}]  {ARCHETYPES[a]['map']}  ->  {verdict}")
        if not ok:
            print(f"  SANITY GATE FAILED: {reason}")
            print(f"  (metrics below are NOT trustworthy until the scenario plays)")
        # show the archetype's dossier metrics, baseline vs band
        bands = dossier.get(a, {})
        for metric, (lo, hi) in bands.items():
            v = p.get(metric, 0.0)
            hit = not ((lo is not None and v < lo) or (hi is not None and v > hi))
            flag = "" if hit else "   <-- miss"
            print(f"    {metric:14s} {v:8.1f}   target {fmt_band(lo,hi):>10s}{flag}")
        # a few always-useful context numbers
        print(f"    .. avgspd {p['avgspd']:.0f}  maxspd {p['maxspd']:.0f}  "
              f"flow {p['flowpct']:.0f}%  air {p['airpct']:.0f}%  "
              f"wallrun {p['wallrunpct']:.0f}%  stuck {p['stuckms']:.0f}ms  "
              f"frags {p['frags']} (midair {p['midair']})")
        journal_rows.append({
            "archetype": a, "gate_ok": ok, "verdict": verdict,
            "metrics": {k: round(v, 1) for k, v in p.items()},
        })

    # append one journal line for this whole iteration (provenance)
    iteration = {
        "ts": int(time.time()), "label": args.label, "git": git,
        "dur_s": args.dur, "results": journal_rows,
    }
    with open(JOURNAL, "a") as f:
        f.write(json.dumps(iteration) + "\n")
    print(f"\njournaled -> {JOURNAL}")
    print("review the dossier bands in dojo_dossier.json against these "
          "baselines, then iterate one knob at a time.\n")
    shutil.rmtree(batch, ignore_errors=True)


if __name__ == "__main__":
    main()
