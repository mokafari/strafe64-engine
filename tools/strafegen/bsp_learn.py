#!/usr/bin/env python3
"""bsp_learn.py — decompile maps and LEARN their design, as structured stats.

Builds on bsp_import (geometry) + bsp_study (entities) to turn one or many .bsp /
.pk3 maps into a design digest: platform/floor footprints, vertical layering
(decks), wall heights, room volume, entity & item mix, spawn counts, and jump-pad
arcs. Aggregated across a corpus into percentile distributions you can study — or
feed back into generation (e.g. size platforms like the maps you admire).

  analyze(path) -> per-map digest
  analyze_corpus([paths]) -> aggregated distributions + suggested values
  python3 bsp_learn.py map.bsp
  python3 bsp_learn.py dir_or_pk3 [more...] --json learned.json
"""
import argparse
import glob
import json
import os
import statistics
import zipfile
from collections import Counter

import bsp_import


def _pct(xs, p):
    if not xs:
        return None
    xs = sorted(xs)
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * p / 100.0
    f = int(k)
    return xs[f] if f + 1 >= len(xs) else xs[f] + (xs[f + 1] - xs[f]) * (k - f)


def _dist(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return {"n": len(xs), "min": round(min(xs)), "p25": round(_pct(xs, 25)),
            "median": round(statistics.median(xs)), "p75": round(_pct(xs, 75)),
            "max": round(max(xs))}


def analyze_scene(scene):
    """Design features of one decompiled scene (bsp_import.import_bsp output)."""
    bx = scene["bounds"]
    W, D, H = bx[3] - bx[0], bx[4] - bx[1], bx[5] - bx[2]
    floor_w, floor_l, floor_z, wall_h = [], [], [], []
    for b in scene["brushes"]:
        if b["role"] == "sky/enclosure":
            continue
        a = b["aabb"]
        dx, dy, dz = a[3] - a[0], a[4] - a[1], a[5] - a[2]
        nz = abs(b["faces"][0]["normal"][2]) if b["faces"] else 0.0
        if nz > 0.7 and dx * dy > 4096:           # sizeable up/down face -> floor
            floor_w.append(min(dx, dy)); floor_l.append(max(dx, dy)); floor_z.append(a[5])
        elif nz < 0.3 and dz > 64:                # vertical surface -> wall
            wall_h.append(dz)
    decks = sorted({round(z / 96.0) * 96 for z in floor_z})   # vertical layering

    ents = scene["entities"]
    classes = Counter(e["classname"] for e in ents)
    items = [e for e in ents if e["classname"].startswith(("item_", "weapon_", "holdable_", "ammo_"))]
    spawns = [e for e in ents if e["classname"].startswith("info_player_deathmatch")]
    # item heights above the map floor
    item_z = [e["origin"][2] - bx[2] for e in items if e.get("origin")]
    # jump-pad arcs: trigger_push -> target_position
    targets = {e["keys"].get("targetname"): e for e in ents
               if e.get("keys", {}).get("targetname") and e.get("origin")}
    pads = []
    for e in ents:
        if e["classname"] == "trigger_push":
            t = targets.get(e["keys"].get("target"))
            if t and t.get("origin"):
                pads.append({"to": t["origin"]})
    return {
        "name": scene.get("name"), "bounds": [round(W), round(D), round(H)],
        "surfaces": scene["counts"].get("brushes", len(scene["brushes"])),
        "triangles": scene["counts"].get("triangles", 0),
        "decks": len(decks), "deck_heights": [int(z) for z in decks][:12],
        "floor_min_side": floor_w, "floor_max_side": floor_l, "wall_height": wall_h,
        "item_height": item_z,
        "classnames": dict(classes.most_common()),
        "items": len(items), "spawns": len(spawns), "jumppads": len(pads),
    }


def _scene_from(path, mapname=None):
    if path.lower().endswith((".pk3", ".zip")):
        data, name = bsp_import._load_pk3(path, mapname)
        return bsp_import.import_bsp(data, os.path.splitext(name)[0])
    return bsp_import.import_bsp(path, os.path.splitext(os.path.basename(path))[0])


def analyze(path, mapname=None):
    return analyze_scene(_scene_from(path, mapname))


def _expand(paths):
    """Expand dirs/globs into a flat list of .bsp/.pk3 files."""
    out = []
    for p in paths:
        if os.path.isdir(p):
            out += glob.glob(os.path.join(p, "**", "*.bsp"), recursive=True)
            out += glob.glob(os.path.join(p, "**", "*.pk3"), recursive=True)
        else:
            out += glob.glob(p)
    return sorted(set(out))


def analyze_corpus(paths):
    """Aggregate per-map digests into corpus-wide distributions + suggestions."""
    files = _expand(paths)
    digests, fw, fl, wh, iz, decks, dims = [], [], [], [], [], [], []
    classes = Counter()
    for f in files:
        try:
            d = analyze(f)
        except Exception as e:                    # skip unreadable/foreign maps
            digests.append({"name": os.path.basename(f), "error": str(e)[:80]})
            continue
        digests.append({k: d[k] for k in ("name", "bounds", "surfaces", "decks",
                                          "items", "spawns", "jumppads")})
        fw += d["floor_min_side"]; fl += d["floor_max_side"]; wh += d["wall_height"]
        iz += d["item_height"]; decks.append(d["decks"]); dims.append(d["bounds"])
        classes.update(d["classnames"])
    agg = {
        "maps": len([d for d in digests if "error" not in d]),
        "platform_min_side": _dist(fw), "platform_max_side": _dist(fl),
        "wall_height": _dist(wh), "item_height": _dist(iz),
        "decks": _dist(decks),
        "map_width": _dist([d[0] for d in dims]),
        "map_depth": _dist([d[1] for d in dims]),
        "map_height": _dist([d[2] for d in dims]),
        "classnames": dict(classes.most_common(25)),
    }
    # suggestions: median real-world values, for calibrating generation/editing
    agg["suggest"] = {
        "platform_w": (agg["platform_min_side"] or {}).get("median"),
        "platform_l": (agg["platform_max_side"] or {}).get("median"),
        "wall_h": (agg["wall_height"] or {}).get("median"),
        "item_h": (agg["item_height"] or {}).get("median"),
        "decks": (agg["decks"] or {}).get("median"),
    }
    return {"corpus": agg, "maps": digests}


def main():
    ap = argparse.ArgumentParser(description="Decompile maps and learn their design")
    ap.add_argument("paths", nargs="+", help=".bsp / .pk3 / dir / glob")
    ap.add_argument("--json", help="write the full learned dataset here")
    args = ap.parse_args()
    res = analyze_corpus(args.paths)
    c = res["corpus"]
    print(f"learned from {c['maps']} map(s):")
    for k in ("map_width", "map_depth", "map_height", "platform_min_side",
              "platform_max_side", "wall_height", "item_height", "decks"):
        d = c[k]
        if d:
            print(f"  {k:18s} median {d['median']:>6}  (p25 {d['p25']} – p75 {d['p75']}, n={d['n']})")
    print(f"  suggest: {c['suggest']}")
    top = list(c["classnames"].items())[:10]
    print("  top classnames: " + ", ".join(f"{k}×{v}" for k, v in top))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(res, f, indent=2)
        print(f"  wrote {args.json}")


if __name__ == "__main__":
    main()
