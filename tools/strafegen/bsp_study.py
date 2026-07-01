#!/usr/bin/env python3
"""bsp_study.py — pull the *instructive* lumps out of a Quake3 (IBSP v46) .bsp.

Full BSP->.map decompilation (q3map2 -convert) gives messy triangulated
brushwork.  The parts you actually learn level-design tricks from are
plaintext or trivially structured:

  * lump 0  Entities  -> raw entity text (spawns, lights, jumppads, teleporters,
                         movers, worldspawn fog/sky/music/gravity keys)
  * lump 1  Shaders   -> every texture/shader the map references

This reads a loose .bsp or a .pk3 (extracts maps/*.bsp), dumps both lumps, and
prints a design digest: classname histogram, light-colour palette, jumppad arcs,
sky/fog shaders, texture vocabulary.

Usage:
  bsp_study.py path/to/map.bsp
  bsp_study.py path/to/mappack.pk3          # studies every map inside
  bsp_study.py map.bsp --entities           # dump raw entity lump only
  bsp_study.py map.bsp --json digest.json   # machine-readable digest
"""
import sys, os, struct, zipfile, io, json, argparse, re
from collections import Counter, defaultdict

LUMP_ENTITIES = 0
LUMP_SHADERS  = 1


def read_lumps(data):
    magic = data[:4]
    if magic not in (b"IBSP", b"RBSP"):
        raise ValueError(f"not an IBSP/RBSP file (magic={magic!r})")
    version = struct.unpack_from("<i", data, 4)[0]
    # 17 lumps (Q3); RBSP has more but the first two are the same
    lumps = []
    off = 8
    for i in range(17):
        o, l = struct.unpack_from("<ii", data, off)
        lumps.append((o, l))
        off += 8
    return version, lumps


def parse_entities(text):
    """Parse the entity lump into a list of dict (preserves duplicate keys as last-wins)."""
    ents = []
    cur = None
    for raw in text.splitlines():
        line = raw.strip()
        if line == "{":
            cur = {}
        elif line == "}":
            if cur is not None:
                ents.append(cur)
            cur = None
        elif cur is not None and line.startswith('"'):
            m = re.match(r'"([^"]*)"\s+"([^"]*)"', line)
            if m:
                cur[m.group(1)] = m.group(2)
    return ents


def parse_shaders(blob):
    names = []
    for i in range(0, len(blob), 72):
        chunk = blob[i:i + 72]
        if len(chunk) < 72:
            break
        name = chunk[:64].split(b"\x00", 1)[0].decode("latin-1")
        flags, contents = struct.unpack_from("<ii", chunk, 64)
        names.append((name, flags, contents))
    return names


def digest(ents, shaders, name):
    classes = Counter(e.get("classname", "?") for e in ents)
    world = next((e for e in ents if e.get("classname") == "worldspawn"), {})

    # light palette
    lights = [e for e in ents if e.get("classname") == "light"]
    colors = Counter()
    for e in lights:
        c = e.get("_color", e.get("color", "")).strip()
        if c:
            colors[c] += 1

    # jumppad arcs: trigger_push -> target_position/info_notnull
    targets = {e.get("targetname"): e for e in ents if e.get("targetname")}
    pads = []
    for e in ents:
        if e.get("classname") == "trigger_push":
            tgt = targets.get(e.get("target"))
            pads.append({"target": e.get("target"),
                         "dest": tgt.get("origin") if tgt else None})

    sky = [s[0] for s in shaders if "sky" in s[0].lower()]
    tex_roots = Counter(s[0].split("/")[0] if "/" in s[0] else s[0]
                        for s in shaders if not s[0].startswith("noshader"))

    return {
        "map": name,
        "entities_total": len(ents),
        "classnames": dict(classes.most_common()),
        "worldspawn": {k: world[k] for k in world
                       if k in ("message", "music", "gravity", "ambient",
                                "_color", "fog", "sky", "_fog", "distancecull",
                                "author", "artpack")},
        "lights": len(lights),
        "light_palette": dict(colors.most_common(12)),
        "jumppads": len(pads),
        "jumppad_arcs": pads[:20],
        "shaders_total": len(shaders),
        "sky_shaders": sky,
        "texture_roots": dict(tex_roots.most_common(20)),
    }


def study_bsp(data, name, args):
    version, lumps = read_lumps(data)
    eo, el = lumps[LUMP_ENTITIES]
    so, sl = lumps[LUMP_SHADERS]
    ent_text = data[eo:eo + el].split(b"\x00", 1)[0].decode("latin-1")
    shaders = parse_shaders(data[so:so + sl])
    ents = parse_entities(ent_text)

    if args.entities:
        print(ent_text)
        return None

    d = digest(ents, shaders, name)
    if not args.json:
        print(f"\n=== {name}  (IBSP v{version}) ===")
        print(f"  entities: {d['entities_total']}   shaders: {d['shaders_total']}"
              f"   lights: {d['lights']}   jumppads: {d['jumppads']}")
        if d["worldspawn"]:
            print(f"  worldspawn: {d['worldspawn']}")
        if d["sky_shaders"]:
            print(f"  sky: {d['sky_shaders']}")
        print(f"  top classnames: " +
              ", ".join(f"{k}×{v}" for k, v in list(d["classnames"].items())[:12]))
        if d["light_palette"]:
            print(f"  light palette: " +
                  ", ".join(f"[{k}]×{v}" for k, v in d["light_palette"].items()))
        print(f"  texture roots: " +
              ", ".join(f"{k}×{v}" for k, v in d["texture_roots"].items()))
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--entities", action="store_true",
                    help="dump raw entity lump and exit")
    ap.add_argument("--json", help="write digest(s) as JSON to this file")
    args = ap.parse_args()

    digests = []
    if args.path.lower().endswith(".pk3") or args.path.lower().endswith(".zip"):
        with zipfile.ZipFile(args.path) as z:
            bsps = [n for n in z.namelist() if n.lower().endswith(".bsp")]
            if not bsps:
                print("no .bsp inside pk3", file=sys.stderr)
                sys.exit(1)
            for n in bsps:
                d = study_bsp(z.read(n), os.path.basename(n), args)
                if d:
                    digests.append(d)
    else:
        with open(args.path, "rb") as f:
            d = study_bsp(f.read(), os.path.basename(args.path), args)
            if d:
                digests.append(d)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(digests if len(digests) > 1 else digests[0], f, indent=2)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
