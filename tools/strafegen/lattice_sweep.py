#!/usr/bin/env python3
"""LATTICE 16-pilot heat sweep — does a bigger g_latticeRadius make RIVAL-trail
kills appear, or just more self-tangle?

Headless (dedicated → no player models → no Hunk_Alloc crash, no client to
overflow, so +addbot at launch fills 16 cleanly). Parses the server console log
obituary STRINGS, which are the only thing that separates a self-tangle
("tangled in their own lattice") from a rival kill ("caught in X's lattice") —
both are MOD_LATTICE so deaths_by_cause can't tell them apart.

    ./lattice_sweep.py                 # radii 40 90 160, 30s each
    ./lattice_sweep.py 40 120 --dur 40
"""
import os, re, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
ENGINE = os.environ.get("DOJO_ENGINE", os.path.join(REPO, "engine/build/Release"))
OA = os.environ.get("DOJO_OA", os.path.join(REPO, "assets/openarena"))
DED = os.path.join(ENGINE, "ioq3ded")
BOT_NAMES = ["angelyss", "arachna", "ayumi", "beret", "dark", "gargoyle",
             "grism", "kyonshi", "merman", "penguin", "sergei", "tony",
             "orbb", "liz", "major", "sarge"]

# Ground truth = the "Kill: <att> <vic> <mod>: Name killed Name by MOD_X" log
# lines (the human-readable obituary prints get truncated by log buffering and
# can't be relied on). self-tangle = attacker client-num == victim client-num.
RE_KILL = re.compile(r"^\s*(?:\d+:\d\d\s+)?Kill:\s+(\d+)\s+(\d+)\s+\d+:\s+.*?\bby\s+(MOD_\w+)", re.M)
RE_EXIT = re.compile(r"^\s*(\d+):(\d\d)\s+Exit:", re.M)   # games.log heat-end ts = duration
RE_WIN  = re.compile(r"wins the heat|IS THE LAST PILOT|lattice took everyone", re.I)


def run(radius, dur, selfms=700, health=60, damage=9, mapname="lattice_arena_64",
        nbots=16, voidrise=0, voiddelay=15, voidrate=48):
    home = tempfile.mkdtemp(prefix=f"lat_{radius}_")
    os.makedirs(os.path.join(home, "baseoa"), exist_ok=True)
    logp = os.path.join(home, "console.log")
    logf = open(logp, "w")
    port = 28100 + (radius + health * 7 + damage * 31) % 400
    args = [
        DED, "+set", "com_basegame", "baseoa", "+set", "fs_basepath", OA,
        "+set", "fs_homepath", home, "+set", "sv_pure", "0",
        "+set", "vm_game", "0", "+set", "dedicated", "1",
        "+set", "net_port", str(port), "+set", "sv_maxclients", "16",
        "+set", "bot_enable", "1", "+set", "g_spSkill", "4",
        "+set", "g_lattice", "1", "+set", "g_voidRise", str(voidrise),
        "+set", "g_latticeRadius", str(radius),
        "+set", "g_latticeSelfMs", str(selfms),
        "+set", "g_latticeHealth", str(health),
        "+set", "g_latticeDamage", str(damage),
        "+set", "g_latticeVoidDelay", str(voiddelay),
        "+set", "g_latticeVoidRise", str(voidrate),
        "+set", "g_log", "games.log", "+set", "g_logSync", "1",
        "+set", "timelimit", "0", "+set", "fraglimit", "0",
        "+map", mapname,
    ]
    for i in range(nbots):
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
    con = open(logp, errors="ignore").read()
    glog = os.path.join(home, "baseoa", "games.log")
    txt = open(glog, errors="ignore").read() if os.path.exists(glog) else con
    import shutil as _sh; _sh.rmtree(home, ignore_errors=True)  # don't litter /var tmp
    lat_self = lat_rival = telefrag = combat = void = 0
    for att, vic, mod in RE_KILL.findall(txt):
        if mod == "MOD_LATTICE":
            if att == vic:
                lat_self += 1
            else:
                lat_rival += 1
        elif mod == "MOD_TELEFRAG":
            telefrag += 1
        elif mod == "MOD_FALLING":      # the rising void erases you
            void += 1
        elif mod not in ("MOD_SUICIDE", "MOD_WATER", "MOD_LAVA"):
            combat += 1
    durs = [int(m) * 60 + int(s) for m, s in RE_EXIT.findall(txt)]
    mean_dur = sum(durs) / len(durs) if durs else 0.0
    return {
        "radius": radius, "selfms": selfms, "health": health, "damage": damage,
        "voiddelay": voiddelay, "voidrise": voidrise,
        "self": lat_self, "rival": lat_rival, "telefrag": telefrag,
        "combat": combat, "void": void,
        "heats": len(durs) or len(RE_WIN.findall(txt)),
        "mean_dur": mean_dur, "durs": durs,
        "crash": "Hunk_Alloc failed" in con or "Server crashed" in con,
    }


def regress(dur=70):
    """Regression gate for the LATTICE mode — the sibling of dojo.py for a mode
    whose health lives in Kill-line OUTCOMES (telefrag-free spawns, the rival-trail
    mechanic expressing, heats resolving) rather than movement telemetry. Runs a
    heat window with VOID OFF to isolate the arena + rival mechanic (void timing is
    characterized separately), checks bands, journals to lattice_runs.jsonl."""
    import json, subprocess as sp, time as _t
    git = (sp.run(["git", "rev-parse", "--short", "HEAD"], cwd=HERE,
                  capture_output=True, text=True).stdout.strip() or "?")
    res = run(140, dur, selfms=1000, voidrise=0, mapname="lattice_arena_64")
    lat = res["self"] + res["rival"]
    rshare = 100 * res["rival"] / lat if lat else 0.0
    checks = [
        ("telefrag<=1 (24 spread spawns seat the field)", res["telefrag"] <= 1),
        ("heats>=1 (last-pilot win resolves)",            res["heats"] >= 1),
        ("lattice kills>=4 (mechanic active)",            lat >= 4),
        ("rival_share>=55% (the trail is the THIRD player)", rshare >= 55),
        ("no crash",                                       not res["crash"]),
    ]
    ok = all(p for _, p in checks)
    print(f"LATTICE regression  map=lattice_arena_64  radius140 selfMs1000 void OFF"
          f"  {dur}s window  git={git}\n")
    for name, p in checks:
        print(f"  [{'PASS' if p else 'FAIL'}] {name}")
    print(f"\n  telefrag={res['telefrag']} self={res['self']} rival={res['rival']} "
          f"combat={res['combat']} heats={res['heats']} "
          f"rival_share={rshare:.0f}% mean_dur={res['mean_dur']:.1f}s")
    print(f"\nVERDICT: {'PASS — lattice mode healthy' if ok else 'FAIL — REGRESSION'}")
    rec = {"ts": int(_t.time()), "git": git, "check": "lattice", "pass": ok,
           "telefrag": res["telefrag"], "self": res["self"], "rival": res["rival"],
           "combat": res["combat"], "heats": res["heats"],
           "rival_share": round(rshare, 1), "mean_dur": res["mean_dur"]}
    with open(os.path.join(HERE, "lattice_runs.jsonl"), "a") as f:
        f.write(json.dumps(rec) + "\n")
    return ok


def main():
    if "--regress" in sys.argv[1:]:
        d = sys.argv[sys.argv.index("--dur") + 1] if "--dur" in sys.argv else "70"
        sys.exit(0 if regress(int(d)) else 1)

    # default          : sweep g_latticeRadius at the default self-grace.
    # --selfms V...     : sweep g_latticeSelfMs at a fixed --radius.
    # --ttk H:D H:D ... : sweep g_latticeHealth:g_latticeDamage at a fixed --radius,
    #                     reporting mean HEAT DURATION (s) so TTK can be co-tuned
    #                     with the void collapse.  e.g. --ttk 60:9 120:9 120:15
    vals, dur, mode, fixed_radius, mapname, nbots = [], 30, "radius", 140, "lattice_arena_64", 16
    a = sys.argv[1:]
    i = 0
    while i < len(a):
        if a[i] == "--dur":
            dur = int(a[i + 1]); i += 2
        elif a[i] == "--selfms":
            mode = "selfms"; i += 1
        elif a[i] == "--ttk":
            mode = "ttk"; i += 1
        elif a[i] == "--void":
            mode = "void"; i += 1
        elif a[i] == "--radius":
            fixed_radius = int(a[i + 1]); i += 2
        elif a[i] == "--map":
            mapname = a[i + 1]; i += 2
        elif a[i] == "--bots":
            nbots = int(a[i + 1]); i += 2
        else:
            vals.append(a[i]); i += 1

    if mode == "ttk":
        if not vals:
            vals = ["60:9", "100:9", "100:15", "150:12", "200:15"]
        pairs = [tuple(int(x) for x in v.split(":")) for v in vals]
        print(f"LATTICE TTK sweep  map={mapname}  16 bots  radius={fixed_radius}  "
              f"{dur}s/window\n")
        print(f"{'health':>6} {'damage':>6} {'mean_dur':>9} {'heats':>6} "
              f"{'lat_self':>8} {'lat_rival':>9} {'combat':>7}  rival_share")
        print("-" * 72)
        for hp, dmg in pairs:
            res = run(fixed_radius, dur, health=hp, damage=dmg, mapname=mapname, nbots=nbots)
            tot = res["self"] + res["rival"]
            share = f"{100*res['rival']/tot:.0f}%" if tot else "—"
            flag = "  CRASH" if res["crash"] else ""
            print(f"{hp:>6} {dmg:>6} {res['mean_dur']:>8.1f}s {res['heats']:>6} "
                  f"{res['self']:>8} {res['rival']:>9} {res['combat']:>7}  "
                  f"{share:>6}{flag}")
        return

    if mode == "void":
        # sweep the LATTICE auto-void DELAY (s) at fixed radius/health, void ON.
        # Does a shorter grace make the collapse co-time with the ~8s burst?
        if not vals:
            vals = ["15", "8", "6", "4"]
        delays = [int(v) for v in vals]
        print(f"LATTICE void-timing sweep  map={mapname}  16 bots  radius={fixed_radius}"
              f"  void ON  {dur}s/window\n")
        print(f"{'voidDelay':>9} {'mean_dur':>9} {'heats':>6} {'lat_self':>8} "
              f"{'lat_rival':>9} {'void':>5} {'combat':>7}  void_share")
        print("-" * 76)
        for vd in delays:
            res = run(fixed_radius, dur, mapname=mapname, nbots=nbots,
                      voidrise=1, voiddelay=vd)
            tot = res["self"] + res["rival"] + res["void"] + res["combat"]
            vshare = f"{100*res['void']/tot:.0f}%" if tot else "—"
            flag = "  CRASH" if res["crash"] else ""
            print(f"{vd:>9} {res['mean_dur']:>8.1f}s {res['heats']:>6} "
                  f"{res['self']:>8} {res['rival']:>9} {res['void']:>5} "
                  f"{res['combat']:>7}  {vshare:>6}{flag}")
        return

    nums = [int(v) for v in vals]
    if not nums:
        nums = [40, 90, 160] if mode == "radius" else [400, 700, 1500, 2500]
    col = "selfMs" if mode == "selfms" else "radius"
    label = (f"self-grace sweep  radius={fixed_radius}" if mode == "selfms"
             else "radius sweep")
    print(f"LATTICE {label}  map={mapname}  16 bots  {dur}s/window\n")
    print(f"{col:>7} {'lat_self':>8} {'lat_rival':>9} {'telefrag':>8} "
          f"{'combat':>7} {'heats':>6} {'mean_dur':>9}  rival_share")
    print("-" * 76)
    for v in nums:
        res = (run(fixed_radius, dur, selfms=v, mapname=mapname) if mode == "selfms"
               else run(v, dur, mapname=mapname))
        tot_lat = res["self"] + res["rival"]
        share = f"{100*res['rival']/tot_lat:.0f}%" if tot_lat else "—"
        flag = "  CRASH" if res["crash"] else ""
        key = res["selfms"] if mode == "selfms" else res["radius"]
        print(f"{key:>7} {res['self']:>8} {res['rival']:>9} "
              f"{res['telefrag']:>8} {res['combat']:>7} {res['heats']:>6} "
              f"{res['mean_dur']:>8.1f}s  {share:>6}{flag}")


if __name__ == "__main__":
    main()
