#!/usr/bin/env python3
"""mapview.py — schematic LAYOUT viewer for strafegen arenas.

Builds an arena (no engine / BSP needed) and renders a top-down PLAN (XY) plus a
side ELEVATION (XZ) to a single self-contained SVG, with an ASCII fallback for quick
text iteration. The point: look at a *picture* of the layout and iterate the recipe,
without launching the game.

Reads the REAL generated geometry: each brush's footprint comes from its face verts,
each brush's ROLE from its palette colour; entities (spawns/items/pads/portals) are
overlaid as markers.

Examples
--------
  python3 mapview.py killbox 8224                  # ring archetype (seed-picked)
  python3 mapview.py killbox 7001 --arch spiral    # force a centerpiece archetype
  python3 mapview.py arena 7140                     # velodrome arena
  python3 mapview.py killbox 8224 --ascii           # text grid to stdout
  python3 mapview.py killbox 8224 --out /tmp/v.svg
"""
import argparse
import strafegen as sg

# ---- role classification: palette RGB -> (label, fill hex, z-order hint) --------
# aliased palettes (e.g. PAL_KB_DECK == SRC_ORANGE) collapse to one role.
ROLE = {
    sg.SRC_ORANGE:    ("deck/floor",        "#c8783c", 0),
    sg.SRC_GREY:      ("structure",         "#8f8f96", 2),
    sg.SRC_TRIM:      ("platform/ledge",    "#e8b070", 3),
    sg.SRC_BLUE:      ("velodrome ring",    "#3f63bd", 1),
    sg.PAL_KB_COLUMN: ("wall-jump column",  "#cf57f0", 4),
    sg.PAL_KB_NEON:   ("neon/portal/crown", "#54dcf0", 5),
    sg.PAL_PAD:       ("jump pad",          "#f2f06e", 4),
    sg.PAL_GATE:      ("gate/portal frame", "#f2e85a", 4),
    sg.PAL_DANGER:    ("hazard",            "#ff5f5f", 6),
}

# entity markers: classname-prefix -> (label, glyph, fill)
ENT = [
    ("info_player_deathmatch", ("spawn",   "S", "#8fff8f")),
    ("item_quad",              ("quad",    "Q", "#cf57f0")),
    ("item_health_mega",       ("mega",    "M", "#ff7777")),
    ("item_health",            ("health",  "h", "#ff9b9b")),
    ("item_armor",             ("armor",   "A", "#7fb0ff")),
    ("misc_teleporter_dest",   ("portal",  "P", "#54dcf0")),
]


def aabb(brush):
    xs, ys, zs = [], [], []
    for f in brush.faces:
        for (x, y, z) in f.poly:
            xs.append(x); ys.append(y); zs.append(z)
    return min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)


def classify(brush):
    """First face palette wins (boxes are single-palette)."""
    pal = brush.faces[0].palette if brush.faces else None
    return ROLE.get(tuple(pal)) if pal is not None else None


def build(kind, seed, diff, arch):
    if kind == "killbox":
        m = sg.Killbox(seed, diff, archetype=arch)
    elif kind == "arena":
        m = sg.Arena(seed, diff)
    else:
        raise SystemExit(f"unsupported kind for mapview: {kind} (use killbox|arena)")
    m.build()
    return m


def collect(m):
    """Return (boxes, ents, bounds). boxes = list of dicts with role+aabb, sorted
    bottom-up so taller structures draw on top. Thin rim strips are dropped."""
    boxes = []
    for b in m.solids:
        role = classify(b)
        if role is None:
            continue
        x0, y0, z0, x1, y1, z1 = aabb(b)
        dx, dy = x1 - x0, y1 - y0
        label, fill, zo = role
        # drop the ENCLOSURE: ceiling / skybox slabs span the whole footprint at
        # height — they'd swamp the plan and make every height cell read "max".
        # Keep the deck (z1<=10) as the floor backdrop.
        if dx > 2600 and dy > 2600 and z1 > 30:
            continue
        # drop thin neon RIM strips (t~10-14u) so the plan stays readable; keep
        # genuine neon volumes (crown, portal frames) which are chunky.
        if min(dx, dy) < 20:
            continue
        boxes.append(dict(label=label, fill=fill, zo=zo,
                          x0=x0, y0=y0, z0=z0, x1=x1, y1=y1, z1=z1,
                          dx=dx, dy=dy, dz=z1 - z0))
    boxes.sort(key=lambda b: (b["z0"], b["zo"], b["dz"]))

    ents = []
    for e in m.entities:
        cn = e.get("classname", "")
        for prefix, meta in ENT:
            if cn.startswith(prefix):
                try:
                    x, y, z = (float(v) for v in e["origin"].split())
                except (KeyError, ValueError):
                    break
                ents.append(dict(meta=meta, x=x, y=y, z=z))
                break

    # FLOW arrows: trigger -> its target_position/teleporter_dest. Shows the
    # circulation a pad/portal sets up (where it throws/sends you).
    targets = {}
    for e in m.entities:
        tn = e.get("targetname")
        if tn and "origin" in e:
            try:
                targets[tn] = tuple(float(v) for v in e["origin"].split())
            except ValueError:
                pass
    flows = []
    for brush, info in getattr(m, "triggers", []):
        tgt = info.get("target")
        if not tgt or tgt not in targets:
            continue
        bx0, by0, bz0, bx1, by1, bz1 = aabb(brush)
        fx, fy = (bx0 + bx1) / 2, (by0 + by1) / 2
        tx, ty, tz = targets[tgt]
        kind = "pad" if info.get("classname") == "trigger_push" else "portal"
        flows.append(dict(fx=fx, fy=fy, tx=tx, ty=ty, tz=tz, kind=kind))

    xs = [b["x0"] for b in boxes] + [b["x1"] for b in boxes]
    ys = [b["y0"] for b in boxes] + [b["y1"] for b in boxes]
    zs = [b["z0"] for b in boxes] + [b["z1"] for b in boxes]
    bounds = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
    return boxes, ents, flows, bounds


# ----------------------------- SVG rendering ------------------------------------
def svg_view(m, kind, seed, arch_used):
    boxes, ents, flows, (wx0, wy0, wz0, wx1, wy1, wz1) = collect(m)
    W = wx1 - wx0
    Dy = wy1 - wy0
    Hz = wz1 - wz0

    PAD = 28
    PLAN = 560                      # plan panel size (px)
    s = PLAN / max(W, Dy)           # world->px scale (shared so plan is to-scale)
    plan_w, plan_h = W * s, Dy * s
    elev_h = max(120.0, Hz * s)

    def px(x):  return PAD + (x - wx0) * s
    def py(y):  return PAD + (wy1 - y) * s        # flip: +Y up on screen
    def ez(z):  return PAD + plan_h + 40 + (wz1 - z) * s   # elevation Y (z up)
    def ex(x):  return PAD + (x - wx0) * s

    parts = []
    total_w = PAD * 2 + plan_w + 220               # + legend column
    total_h = PAD + plan_h + 40 + elev_h + PAD
    parts.append(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w:.0f}" '
        f'height="{total_h:.0f}" viewBox="0 0 {total_w:.0f} {total_h:.0f}" '
        f'font-family="monospace" font-size="11">')
    parts.append(f'<rect width="{total_w:.0f}" height="{total_h:.0f}" fill="#15151c"/>')
    parts.append(
        '<defs>'
        '<marker id="ahp" markerWidth="7" markerHeight="7" refX="6" refY="3" '
        'orient="auto"><path d="M0,0 L7,3 L0,6 Z" fill="#ffb26b"/></marker>'
        '<marker id="aht" markerWidth="7" markerHeight="7" refX="6" refY="3" '
        'orient="auto"><path d="M0,0 L7,3 L0,6 Z" fill="#54dcf0"/></marker>'
        '</defs>')

    # title
    arch_lbl = next((d["kind"] for (n, d) in m.sections if n == "centerpiece"),
                    arch_used or "-")
    title = (f'{kind} seed {seed}  |  centerpiece: {arch_lbl}  |  '
             f'{int(W)}x{int(Dy)}x{int(Hz)}u  |  boxes {len(boxes)}  spawns '
             f'{sum(1 for e in ents if e["meta"][0]=="spawn")}  |  '
             f'heights labelled (z); arrows = pad/portal flow')
    parts.append(f'<text x="{PAD}" y="16" fill="#e8e8f0">{title}</text>')

    # ---- PLAN (XY) ----
    parts.append(f'<text x="{PAD}" y="{PAD-4:.0f}" fill="#9aa">PLAN (top-down)</text>')
    parts.append(f'<rect x="{PAD:.0f}" y="{PAD:.0f}" width="{plan_w:.0f}" '
                 f'height="{plan_h:.0f}" fill="#1d1d27" stroke="#33333f"/>')
    # grid every 512u
    g = 512
    gx = wx0 - (wx0 % g)
    while gx <= wx1:
        parts.append(f'<line x1="{px(gx):.1f}" y1="{PAD:.1f}" x2="{px(gx):.1f}" '
                     f'y2="{PAD+plan_h:.1f}" stroke="#262630"/>')
        gx += g
    gy = wy0 - (wy0 % g)
    while gy <= wy1:
        parts.append(f'<line x1="{PAD:.1f}" y1="{py(gy):.1f}" x2="{PAD+plan_w:.1f}" '
                     f'y2="{py(gy):.1f}" stroke="#262630"/>')
        gy += g
    HEIGHT_ROLES = {"structure", "platform/ledge", "wall-jump column",
                    "neon/portal/crown"}
    for b in boxes:
        x, y = px(b["x0"]), py(b["y1"])
        w, h = b["dx"] * s, b["dy"] * s
        op = 0.30 if b["label"].startswith("deck") else 0.85
        parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                     f'fill="{b["fill"]}" fill-opacity="{op}" stroke="#00000040"/>')
        # HEIGHT MARKER: top-of-brush z, on sizeable elevated structures
        if b["label"] in HEIGHT_ROLES and b["z1"] > 40 and min(w, h) > 14:
            parts.append(f'<text x="{x+2:.1f}" y="{y+10:.1f}" fill="#ffffffcc" '
                         f'font-size="8">{int(b["z1"])}</text>')

    # ---- FLOW arrows (pad/portal circulation) ----
    for fl in flows:
        col = "#ffb26b" if fl["kind"] == "pad" else "#54dcf0"
        mk = "ahp" if fl["kind"] == "pad" else "aht"
        dash = '' if fl["kind"] == "pad" else ' stroke-dasharray="4 3"'
        parts.append(
            f'<line x1="{px(fl["fx"]):.1f}" y1="{py(fl["fy"]):.1f}" '
            f'x2="{px(fl["tx"]):.1f}" y2="{py(fl["ty"]):.1f}" stroke="{col}" '
            f'stroke-width="1.6" opacity="0.8" marker-end="url(#{mk})"{dash}/>')

    for e in ents:
        lab, glyph, fill = e["meta"]
        cx, cy = px(e["x"]), py(e["y"])
        parts.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="6" fill="{fill}" '
                     f'stroke="#000" stroke-width="0.5"/>')
        parts.append(f'<text x="{cx:.1f}" y="{cy+3:.1f}" fill="#111" '
                     f'text-anchor="middle" font-size="8">{glyph}</text>')
        # HEIGHT MARKER: z of elevated items/portals (deck things at ~40 stay clean)
        if e["z"] > 60:
            parts.append(f'<text x="{cx+7:.1f}" y="{cy-5:.1f}" fill="#ffffffcc" '
                         f'font-size="8">z{int(e["z"])}</text>')

    # ---- ELEVATION (XZ) ----
    ey_top = PAD + plan_h + 40
    parts.append(f'<text x="{PAD}" y="{ey_top-6:.0f}" fill="#9aa">ELEVATION (side, X x Z) — Z scale at left</text>')
    parts.append(f'<rect x="{PAD:.0f}" y="{ey_top:.0f}" width="{plan_w:.0f}" '
                 f'height="{elev_h:.0f}" fill="#1d1d27" stroke="#33333f"/>')
    # Z scale: horizontal gridlines + labels every 400u
    ztick = 400
    zt = 0
    while zt <= wz1:
        if zt >= wz0:
            yy = ez(zt)
            parts.append(f'<line x1="{PAD:.1f}" y1="{yy:.1f}" x2="{PAD+plan_w:.1f}" '
                         f'y2="{yy:.1f}" stroke="#2a2a36"/>')
            parts.append(f'<text x="{PAD+2:.1f}" y="{yy-2:.1f}" fill="#667" '
                         f'font-size="8">{zt}u</text>')
        zt += ztick
    for b in sorted(boxes, key=lambda d: d["z0"]):
        x, y = ex(b["x0"]), ez(b["z1"])
        w, h = b["dx"] * s, b["dz"] * s
        op = 0.25 if b["label"].startswith("deck") else 0.7
        parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{max(h,1):.1f}" '
                     f'fill="{b["fill"]}" fill-opacity="{op}" stroke="#00000030"/>')
    for e in ents:
        lab, glyph, fill = e["meta"]
        parts.append(f'<circle cx="{ex(e["x"]):.1f}" cy="{ez(e["z"]):.1f}" r="3.5" '
                     f'fill="{fill}"/>')

    # ---- LEGEND ----
    lx = PAD + plan_w + 18
    ly = PAD + 6
    parts.append(f'<text x="{lx}" y="{ly}" fill="#e8e8f0">LEGEND</text>')
    ly += 18
    seen = []
    for (label, fill, _zo) in ROLE.values():
        if label in seen:
            continue
        seen.append(label)
        parts.append(f'<rect x="{lx}" y="{ly-9:.0f}" width="12" height="12" '
                     f'fill="{fill}"/>')
        parts.append(f'<text x="{lx+18}" y="{ly:.0f}" fill="#ccd">{label}</text>')
        ly += 17
    ly += 6
    for prefix, (label, glyph, fill) in ENT:
        parts.append(f'<circle cx="{lx+6}" cy="{ly-4:.0f}" r="6" fill="{fill}"/>')
        parts.append(f'<text x="{lx+6}" y="{ly-1:.0f}" fill="#111" '
                     f'text-anchor="middle" font-size="8">{glyph}</text>')
        parts.append(f'<text x="{lx+18}" y="{ly:.0f}" fill="#ccd">{label}</text>')
        ly += 17

    parts.append('</svg>')
    return "\n".join(parts)


# ----------------------------- ASCII view ---------------------------------------
def ascii_view(m, cells=42):
    boxes, ents, flows, (wx0, wy0, wz0, wx1, wy1, wz1) = collect(m)
    W, Dy = wx1 - wx0, wy1 - wy0
    grid = [[" "] * cells for _ in range(cells)]
    hgt = [[" "] * cells for _ in range(cells)]      # height-tier grid (parallel)
    rank = {"deck/floor": 0, "velodrome ring": 1, "platform/ledge": 2,
            "structure": 3, "jump pad": 4, "wall-jump column": 5,
            "neon/portal/crown": 4, "gate/portal frame": 4, "hazard": 6}
    ch = {"deck/floor": ".", "velodrome ring": ":", "platform/ledge": "=",
          "structure": "#", "jump pad": "o", "wall-jump column": "^",
          "neon/portal/crown": "+", "gate/portal frame": "+", "hazard": "!"}
    rankgrid = [[-1] * cells for _ in range(cells)]
    topz = [[-1.0] * cells for _ in range(cells)]

    def cx(x): return min(cells - 1, max(0, int((x - wx0) / W * (cells - 1))))
    def cy(y): return min(cells - 1, max(0, int((wy1 - y) / Dy * (cells - 1))))

    for b in boxes:
        r = rank.get(b["label"], 3)
        c = ch.get(b["label"], "#")
        for iy in range(cy(b["y1"]), cy(b["y0"]) + 1):
            for ix in range(cx(b["x0"]), cx(b["x1"]) + 1):
                if r >= rankgrid[iy][ix]:
                    rankgrid[iy][ix] = r
                    grid[iy][ix] = c
                if b["z1"] > topz[iy][ix]:           # tallest brush wins the height cell
                    topz[iy][ix] = b["z1"]
                    # tier: 0-9 in ~200u steps (deck=blank, '#'>=2000u)
                    tier = int(b["z1"] // 200)
                    hgt[iy][ix] = "#" if tier > 9 else (str(tier) if tier > 0 else ".")
    glyphs = {"spawn": "S", "quad": "Q", "mega": "M", "health": "h",
              "armor": "A", "portal": "P"}
    for e in ents:
        grid[cy(e["y"])][cx(e["x"])] = glyphs.get(e["meta"][0], "*")
    arch_lbl = next((d["kind"] for (n, d) in m.sections if n == "centerpiece"), "-")
    out = [f"# centerpiece: {arch_lbl}   ({int(W)}x{int(Dy)}x{int(wz1-wz0)}u, "
           f"{cells}x{cells} cells)   pad/portal flows: {len(flows)}",
           "",
           "PLAN (role):" + " " * (cells - 11) + "    HEIGHT (z-tier, x200u):"]
    for ry in range(cells):
        out.append("".join(grid[ry]) + "    " + "".join(hgt[ry]))
    out.append("")
    out.append("role:   . deck  : ring  = platform  # structure  ^ column  o pad  "
               "+ neon/portal  ! hazard   S spawn Q quad M mega h health A armor P portal")
    out.append("height: digit = top height in 200u tiers (1=200u .. 9=1800u, #=higher); "
               "'.' = deck level")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description="Schematic layout viewer for strafegen arenas")
    ap.add_argument("kind", choices=("killbox", "arena"))
    ap.add_argument("seed", type=int)
    ap.add_argument("--diff", type=int, default=1, choices=(0, 1, 2))
    ap.add_argument("--arch", default=None,
                    choices=("spire", "spiral", "forest", "ring", "cross", "twin", "court"),
                    help="force killbox centerpiece archetype (recipe selection)")
    ap.add_argument("--ascii", action="store_true", help="print ASCII grid to stdout")
    ap.add_argument("--out", default=None, help="SVG output path")
    args = ap.parse_args()

    m = build(args.kind, args.seed, args.diff, args.arch)

    if args.ascii:
        print(ascii_view(m))
        return
    out = args.out or f"{args.kind}_{args.seed}{('_'+args.arch) if args.arch else ''}.svg"
    with open(out, "w") as fh:
        fh.write(svg_view(m, args.kind, args.seed, args.arch))
    boxes, ents, flows, b = collect(m)
    print(f"wrote {out}  ({len(boxes)} boxes, {len(ents)} markers, "
          f"{len(flows)} flow arrows, "
          f"{int(b[3]-b[0])}x{int(b[4]-b[1])}x{int(b[5]-b[2])}u)")


if __name__ == "__main__":
    main()
