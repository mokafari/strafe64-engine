#!/usr/bin/env python3
"""STRAFE 64 — bot movement visualizer: tracers + heatmap.

Runs one map/dojo scenario headless with position-trail telemetry on, then
renders a top-down SVG so you can SEE how the bots move: each bot's path as a
speed-coloured tracer (blue slow -> red fast), plus a position heatmap of
where they spend time. Stalls show as hot blobs; flow shows as long red
streaks.

    ./viz.py dojo_flow                       # -> dojo_flow_trace.svg
    ./viz.py strafe64dm_1337 --bots 4 --dur 25 --void 0
"""

import argparse, json, math, os, shutil, subprocess, sys, tempfile, time
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = "/Users/gustav/ioquake3/build/Release"
OA = "/Users/gustav/openarena-0.8.8"
DED = os.path.join(ENGINE, "ioq3ded")
BUILD = os.path.join(ENGINE, "baseq3")


def deploy():
    for dll in ("qagame.dylib", "cgame.dylib", "ui.dylib"):
        s = os.path.join(BUILD, dll)
        if os.path.exists(s):
            d = os.path.join(OA, "baseoa", dll)
            shutil.copy2(s, d)
            subprocess.run(["codesign", "-f", "-s", "-", d], capture_output=True)


def run(mapname, bots, dur, void):
    home = tempfile.mkdtemp(prefix="strafe64_viz_")
    os.makedirs(os.path.join(home, "baseoa"), exist_ok=True)
    tag = "viz_" + mapname
    args = [
        DED, "+set", "com_basegame", "baseoa", "+set", "fs_basepath", OA,
        "+set", "fs_homepath", home, "+set", "sv_pure", "0",
        "+set", "vm_game", "0", "+set", "dedicated", "1",
        "+set", "net_port", "28100", "+set", "sv_maxclients", "16",
        "+set", "bot_enable", "1",
        "+set", "g_spSkill", "4", "+set", "g_playtest", "1",
        "+set", "g_playtestTrail", "1", "+set", "g_playtestTag", tag,
        "+set", "g_voidRise", str(void), "+set", "timelimit", "0",
        "+set", "fraglimit", "0", "+map", mapname,
    ]
    # explicit +addbot fills reliably (bot_minplayers caps ~2 on dedicated)
    BOTS = ["angelyss", "arachna", "ayumi", "beret", "dark", "gargoyle",
            "grism", "kyonshi", "merman", "penguin"]
    for i in range(bots):
        args += ["+addbot", BOTS[i % len(BOTS)], "4"]
    p = subprocess.Popen(args, stdin=subprocess.PIPE,
                         stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
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
    out = os.path.join(home, "baseoa", "playtest", tag + ".jsonl")
    recs = []
    if os.path.exists(out):
        with open(out) as f:
            for line in f:
                line = line.strip()
                if line:
                    try: recs.append(json.loads(line))
                    except json.JSONDecodeError: pass
    shutil.rmtree(home, ignore_errors=True)
    return recs


def spd_color(s):
    """blue (slow) -> cyan -> green -> yellow -> red (fast, 800+ ups)."""
    t = max(0.0, min(s / 800.0, 1.0))
    stops = [(0.0, (40, 90, 255)), (0.35, (40, 230, 200)),
             (0.6, (120, 255, 80)), (0.8, (255, 210, 40)),
             (1.0, (255, 60, 40))]
    for i in range(len(stops) - 1):
        t0, c0 = stops[i]; t1, c1 = stops[i + 1]
        if t <= t1:
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0
            r = int(c0[0] + (c1[0] - c0[0]) * f)
            g = int(c0[1] + (c1[1] - c0[1]) * f)
            b = int(c0[2] + (c1[2] - c0[2]) * f)
            return f"#{r:02x}{g:02x}{b:02x}"
    return "#ff3c28"


def render(mapname, recs, path, size=900):
    trail = [r for r in recs if r.get("ev") == "trail"]
    if not trail:
        sys.exit("no trail points (is g_playtestTrail wired + bots spawning?)")
    xs = [r["x"] for r in trail]; ys = [r["y"] for r in trail]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    spanx, spany = max(maxx - minx, 1), max(maxy - miny, 1)
    margin = 40
    scale = (size - 2 * margin) / max(spanx, spany)
    W = int(spanx * scale + 2 * margin)
    H = int(spany * scale + 2 * margin)

    def sx(x): return margin + (x - minx) * scale
    def sy(y): return margin + (maxy - y) * scale   # flip: north up

    # --- heatmap: bin positions, opacity ~ sqrt(density) ---
    cell = 64.0
    grid = defaultdict(int)
    for r in trail:
        grid[(int((r["x"] - minx) // cell), int((r["y"] - miny) // cell))] += 1
    gmax = max(grid.values()) if grid else 1
    heat = []
    cs = cell * scale
    for (gx, gy), c in grid.items():
        a = (c / gmax) ** 0.5 * 0.7
        px = margin + gx * cell * scale
        py = margin + (maxy - miny - (gy + 1) * cell) * scale
        heat.append(f'<rect x="{px:.1f}" y="{py:.1f}" width="{cs:.1f}" '
                    f'height="{cs:.1f}" fill="#ff8c1a" opacity="{a:.2f}"/>')

    # --- tracers: per bot, speed-coloured segments; break on respawn jumps ---
    bybot = defaultdict(list)
    for r in trail:
        bybot[r["cn"]].append(r)
    segs = []
    for cn, pts in bybot.items():
        pts.sort(key=lambda r: r["t"])
        for a, b in zip(pts, pts[1:]):
            dx, dy = b["x"] - a["x"], b["y"] - a["y"]
            if b["t"] < a["t"] or (dx * dx + dy * dy) ** 0.5 > 500:
                continue   # respawn / teleport — don't draw the jump
            col = spd_color((a["spd"] + b["spd"]) / 2)
            segs.append(f'<line x1="{sx(a["x"]):.1f}" y1="{sy(a["y"]):.1f}" '
                        f'x2="{sx(b["x"]):.1f}" y2="{sy(b["y"]):.1f}" '
                        f'stroke="{col}" stroke-width="2" '
                        f'stroke-linecap="round" opacity="0.85"/>')
    # start markers (first point per bot)
    marks = []
    for cn, pts in bybot.items():
        if pts:
            p0 = pts[0]
            marks.append(f'<circle cx="{sx(p0["x"]):.1f}" cy="{sy(p0["y"]):.1f}" '
                         f'r="4" fill="none" stroke="#40ff80" stroke-width="2"/>')

    # --- death markers: where runs end. X per death, red = void/fall, magenta
    # = combat/other. Clusters reveal the killer geometry (a gap, a pit edge). ---
    deaths = [r for r in recs if r.get("ev") == "death" and "deathx" in r]
    nvoid = 0
    for r in deaths:
        dx, dy = sx(r["deathx"]), sy(r["deathy"])
        void = r.get("mod") == "MOD_FALLING"
        nvoid += void
        col = "#ff2030" if void else "#ff30c0"
        marks.append(f'<g stroke="{col}" stroke-width="2.5" opacity="0.9">'
                     f'<line x1="{dx-5:.1f}" y1="{dy-5:.1f}" x2="{dx+5:.1f}" y2="{dy+5:.1f}"/>'
                     f'<line x1="{dx-5:.1f}" y1="{dy+5:.1f}" x2="{dx+5:.1f}" y2="{dy-5:.1f}"/></g>')

    maxspd = max((r["spd"] for r in trail), default=0)
    svg = [
        f'<svg viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg" '
        f'font-family="monospace">',
        f'<rect x="0" y="0" width="{W}" height="{H}" fill="#05070a"/>',
        '<g>' + "".join(heat) + '</g>',
        '<g>' + "".join(segs) + '</g>',
        '<g>' + "".join(marks) + '</g>',
        f'<text x="{margin}" y="24" fill="#ff9c0a" font-size="16">'
        f'STRAFE 64 // {mapname} // {len(bybot)} bots, {len(trail)} samples, '
        f'top speed {maxspd} ups, {len(deaths)} deaths ({nvoid} void)</text>',
        # legend
        f'<text x="{margin}" y="{H-14}" fill="#6688aa" font-size="12">'
        f'tracer colour = speed (blue slow .. red {800}+ ups) · '
        f'amber = time-spent heatmap · green ring = spawn · '
        f'red X = void/fall death · magenta X = combat death</text>',
        '</svg>',
    ]
    with open(path, "w") as f:
        f.write("\n".join(svg))
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map", help="map name, e.g. dojo_flow")
    ap.add_argument("--bots", type=int, default=3)
    ap.add_argument("--dur", type=int, default=22)
    ap.add_argument("--void", type=int, default=0)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    if not os.path.exists(DED):
        sys.exit(f"ioq3ded not found at {DED}")
    deploy()
    print(f"running {args.map} ({args.bots} bots, {args.dur}s, trails on)...")
    recs = run(args.map, args.bots, args.dur, args.void)
    out = args.out or os.path.join(HERE, f"{args.map}_trace.svg")
    render(args.map, recs, out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
