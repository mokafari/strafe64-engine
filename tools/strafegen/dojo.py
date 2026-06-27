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
# Repo root = two up from tools/strafegen. After the in-tree port (commits
# 469c853 / 6a2265c) the engine + OA assets live INSIDE this repo; the old
# hardcoded /Users/gustav/ioquake3 + /Users/gustav/openarena-0.8.8 paths still
# exist on disk but build STALE pre-port binaries — the dojo was silently
# regression-testing the wrong engine. Default to the in-tree tree; override
# with DOJO_ENGINE / DOJO_OA env vars for an out-of-tree build.
REPO = os.path.dirname(os.path.dirname(HERE))
ENGINE = os.environ.get("DOJO_ENGINE", os.path.join(REPO, "engine/build/Release"))
OA = os.environ.get("DOJO_OA", os.path.join(REPO, "assets/openarena"))
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
    # ★ surf core-loop TRAVERSAL regression: bots surf the ramps (BotSurfControl,
    # iter36) at 445-646 ups but don't reliably finish laps — so this guards surf
    # QUALITY (they keep carving fast + airborne), not completion. surf_64 ships
    # item-bait midline + 4 spread spawns so bots ride the line.
    "surf":   {"map": "surf_64",     "void": 0, "bots": 5,
               "extra": ["bot_combatBhop", "0"]},
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
    # surf: calibrated from baseline (maxspd 440-477, air 72-80%, both STABLE across
    # runs). Guard maxspd + airpct only — a broken surf drops bots to the ground so
    # both crater (speed ~150, air low). stuckms is DROPPED: surf bots stall at ramp
    # transitions so it runs high AND erratic (2919-3532), a false-PARTIAL source,
    # not a break signal. Floors carry margin under the observed spread.
    "surf":   {"maxspd": [380, None], "airpct": [55, None]},
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
    # used only for best-effort metadata (git hash, etc.); a missing tool or
    # non-zero exit should degrade to "?" but never swallow Ctrl-C / interpreter exit
    try:
        return subprocess.check_output(cmd, cwd=HERE, text=True, timeout=30).strip()
    except (subprocess.SubprocessError, OSError):
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
    skill = cfg.get("skill", 4)    # 1-5; sweepable
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
        "+set", "g_spSkill", str(skill), "+set", "g_playtest", "1",
        "+set", "g_playtestTag", tag, "+set", "g_voidRise", str(void),
        "+set", "timelimit", "0", "+set", "fraglimit", "0",
        # DOJO measures REAL movement/combat: disable the bullet-time clock
        # (g_timeBind scales the world clock by movement intent — a still bot
        # near-freezes time, which warps speed / stuck-ms / frag timing).
        "+set", "g_timeBind", "0",
    ]
    for k in range(0, len(cfg.get("extra", [])), 2):
        args += ["+set", cfg["extra"][k], cfg["extra"][k + 1]]
    args += ["+map", mapname]
    for i in range(bots):
        args += ["+addbot", BOT_NAMES[i % len(BOT_NAMES)], str(skill)]
    proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=logf,
                            stderr=subprocess.STDOUT)
    time.sleep(dur)
    try:
        proc.stdin.write(b"quit\n"); proc.stdin.flush()
    except OSError:                       # broken pipe: engine already gone
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
        "slidepct": mean([r.get("slidepct") for r in recs]),
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


# --- weapon test modes: the (g_botSwordOnly, g_vectorgun) pair that selects the
#     combat ruleset. Use with --arena to probe one map under one weapon. ---
WEAPON_CVARS = {
    "sword":     ["g_botSwordOnly", "1", "g_vectorgun", "0",
                  "bot_swordBlock", "1", "bot_combatBhop", "1"],
    "vectorgun": ["g_botSwordOnly", "0", "g_vectorgun", "1",
                  "bot_combatBhop", "1"],
    "kit":       ["g_botSwordOnly", "0", "g_vectorgun", "0",
                  "bot_combatBhop", "1"],
}


def run_arena_probe(mapname, weapon, dur, bots):
    """Quick single-map combat probe under a chosen weapon ruleset. Prints frags /
    midair% / kill-speed so you can see whether bots actually FIGHT (vectorgun: a
    0-frag result = bots holding the rail but not shooting)."""
    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")
    deploy_dylibs()
    cfg = {"map": mapname, "void": 0, "bots": bots,
           "extra": WEAPON_CVARS[weapon]}
    batch = tempfile.mkdtemp(prefix="strafe64_arena_")
    print(f"arena probe: map={mapname}  weapon={weapon}  bots={bots}  dur={dur}s")
    _, recs = run_scenario(f"arena_{weapon}", cfg, batch, dur, 0)
    p = profile("arena", recs)
    print(f"  bot-runs:    {p['n']}")
    print(f"  frags:       {p['frags']}  ({p['frags_per_min']:.1f}/min)")
    print(f"  midair %:    {p['midair_pct']:.0f}")
    print(f"  kill-speed:  {p['killspd']:.0f} ups")
    print(f"  avg/max spd: {p['avgspd']:.0f} / {p['maxspd']:.0f}")
    print(f"  stuck ms:    {p['stuckms']:.0f}")
    if p["frags"] > 0:
        print(f"  => bots FIGHTING ({weapon})")
    else:
        hint = " (bots holding the rail but not firing?)" if weapon == "vectorgun" else ""
        print(f"  => NO FRAGS — bots not engaging{hint}")
    return p


def run_arena_both(mapname, dur, bots, repeats=1):
    """Probe BOTH weapons on a map, optionally REPEATED and MEDIAN-aggregated to
    beat the run-to-run noise (sword stuck / vg frags swing ~30% between identical
    runs). ALL weapon x repeat jobs run in parallel (distinct ports), so even
    repeats=3 finishes in ~one dur, not N x dur. Prints a median row + the spread
    so you can see how noisy each metric is before trusting a keep/revert."""
    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")
    deploy_dylibs()
    batch = tempfile.mkdtemp(prefix="strafe64_arena_")
    print(f"arena probe (both weapons x{repeats} reps, parallel): "
          f"map={mapname} bots={bots} dur={dur}s")
    jobs, idx = [], 0
    for w in ("vectorgun", "sword"):
        cfg = {"map": mapname, "void": 0, "bots": bots, "extra": WEAPON_CVARS[w]}
        for _ in range(repeats):
            jobs.append((w, idx, cfg)); idx += 1
    res = {"vectorgun": [], "sword": []}
    with ThreadPoolExecutor(max_workers=min(len(jobs), 6)) as ex:
        futs = {ex.submit(run_scenario, f"arena_{w}_{i}", cfg, batch, dur, i): w
                for (w, i, cfg) in jobs}
        for fu in futs:
            w = futs[fu]
            _, recs = fu.result()
            res[w].append(profile("arena", recs))
    out = {}
    for w in ("vectorgun", "sword"):
        profs = res[w]
        a = median_profiles(profs) if len(profs) > 1 else profs[0]
        out[w] = a
        fr = [p["frags_per_min"] for p in profs]
        st = [p["stuckms"] for p in profs]
        spread = (f"  [frg {min(fr):.1f}-{max(fr):.1f}  stuck {min(st):.0f}-{max(st):.0f}]"
                  if len(profs) > 1 else "")
        print(f"  {w:<10} reps={len(profs)}  frags/min={a['frags_per_min']:.1f}  "
              f"midair={a['midair_pct']:.0f}%  killspd={a['killspd']:.0f}  "
              f"avg/max={a['avgspd']:.0f}/{a['maxspd']:.0f}  stuck={a['stuckms']:.0f}{spread}")
    return out


def run_sweep(mapname, weapons, players, skills, dur, repeats, parallel):
    """Grid-sweep a map across weapon x player-count x skill, each config repeated
    and MEDIAN-aggregated so iteration calls rest on stable numbers (single short
    runs are too noisy). Prints a table and appends to sweep_results.jsonl."""
    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")
    deploy_dylibs()
    batch = tempfile.mkdtemp(prefix="strafe64_sweep_")
    grid = [(w, pl, sk) for w in weapons for pl in players for sk in skills]
    print(f"sweep: map={mapname}  {len(grid)} configs x {repeats} reps @ {dur}s  "
          f"(weapons={weapons} players={players} skills={skills}, parallel={parallel})")
    jobs, idx = [], 0
    for (w, pl, sk) in grid:
        for r in range(repeats):
            cfg = {"map": mapname, "void": 0, "bots": pl, "skill": sk,
                   "extra": WEAPON_CVARS[w]}
            jobs.append((w, pl, sk, r, cfg, idx)); idx += 1
    results = {}
    with ThreadPoolExecutor(max_workers=parallel) as ex:
        futs = {ex.submit(run_scenario, f"sw_{w}_{pl}_{sk}_{r}", cfg, batch, dur, ix):
                (w, pl, sk) for (w, pl, sk, r, cfg, ix) in jobs}
        for fu in futs:
            key = futs[fu]
            _, recs = fu.result()
            results.setdefault(key, []).append(profile("arena", recs))
    hdr = (f"{'weapon':<10}{'pl':>3}{'sk':>3}  {'runs':>5}{'frg/min':>8}"
           f"{'midair%':>8}{'killspd':>8}{'avgspd':>7}{'maxspd':>7}{'stuck':>7}")
    print("\n" + hdr); print("-" * len(hdr))
    outp = os.path.join(HERE, "sweep_results.jsonl")
    with open(outp, "a") as fout:
        for (w, pl, sk) in grid:
            profs = results.get((w, pl, sk), [])
            if not profs:
                continue
            a = median_profiles(profs)
            print(f"{w:<10}{pl:>3}{sk:>3}  {int(a['n']):>5}{a['frags_per_min']:>8.1f}"
                  f"{a['midair_pct']:>8.0f}{a['killspd']:>8.0f}{a['avgspd']:>7.0f}"
                  f"{a['maxspd']:>7.0f}{a['stuckms']:>7.0f}")
            rec = {"map": mapname, "weapon": w, "players": pl, "skill": sk,
                   "dur": dur, "repeats": repeats}
            rec.update({k: a[k] for k in ("frags", "frags_per_min", "midair_pct",
                        "killspd", "avgspd", "maxspd", "stuckms", "n")})
            fout.write(json.dumps(rec) + "\n")
    print(f"\nsaved -> {outp}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--archetypes", default="speed,flow,ztrick,arena")
    ap.add_argument("--dur", type=int, default=45, help="seconds per scenario (longer = stabler)")
    ap.add_argument("--parallel", type=int, default=4)
    ap.add_argument("--label", default="baseline", help="config tag for the journal")
    ap.add_argument("--arena", default=None,
                    help="quick combat probe on this map (e.g. dojo_arena or "
                         "strafe64kb_8224) instead of the battery")
    ap.add_argument("--weapon", default="vectorgun",
                    choices=("sword", "vectorgun", "kit"),
                    help="weapon ruleset for --arena")
    ap.add_argument("--bots", type=int, default=8, help="bots for --arena probe")
    ap.add_argument("--both", action="store_true",
                    help="with --arena: probe BOTH weapons in parallel")
    ap.add_argument("--sweep", default=None,
                    help="grid-sweep this map across weapons x players x skill")
    ap.add_argument("--weapons", default="sword,vectorgun",
                    help="sweep weapons (comma list of sword|vectorgun|kit)")
    ap.add_argument("--players", default="6,12",
                    help="sweep bot counts (comma list)")
    ap.add_argument("--skills", default="3,5",
                    help="sweep bot skill levels 1-5 (comma list)")
    ap.add_argument("--repeats", type=int, default=2,
                    help="repeats per config (median-aggregated)")
    args = ap.parse_args()

    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")

    if args.sweep:
        run_sweep(args.sweep,
                  [w.strip() for w in args.weapons.split(",") if w.strip()],
                  [int(p) for p in args.players.split(",") if p.strip()],
                  [int(s) for s in args.skills.split(",") if s.strip()],
                  args.dur, args.repeats, args.parallel)
        return

    if args.arena:
        if args.both:
            run_arena_both(args.arena, args.dur, args.bots, args.repeats)
        else:
            run_arena_probe(args.arena, args.weapon, args.dur, args.bots)
        return

    # The LATTICE mode's health lives in Kill-line OUTCOMES (telefrag-free spawns,
    # the rival-trail mechanic, heats resolving), not the movement telemetry the
    # four archetypes classify on. So `--archetypes lattice` delegates to the
    # self-contained lattice gate (lattice_sweep.regress) and returns BEFORE the
    # movement battery — it shares the no-regression discipline without entangling
    # with the profile pipeline. Combine: `--archetypes speed,flow,ztrick,arena,lattice`.
    req = args.archetypes.split(",")
    if "lattice" in req:
        import lattice_sweep
        lat_ok = lattice_sweep.regress(max(args.dur, 60))
        req = [a for a in req if a != "lattice"]
        if not req:
            sys.exit(0 if lat_ok else 1)

    archs = [a for a in req if a in ARCHETYPES]

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
              f"wallrun {p['wallrunpct']:.0f}%  slide {p['slidepct']:.0f}%  stuck {p['stuckms']:.0f}ms  "
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
