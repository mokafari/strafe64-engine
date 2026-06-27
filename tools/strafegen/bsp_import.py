#!/usr/bin/env python3
"""bsp_import.py — decompile ANY Quake3 (IBSP v46) .bsp into MapForge's scene model.

Where bsp_study.py pulls the *text* lumps (entities + shaders) for design study,
this reads the GEOMETRY lumps — drawverts, drawindexes, surfaces — and rebuilds
renderable triangles grouped by shader, plus the entities. The output is the same
scene dict MapForge renders, so the browser can load and orbit any existing map
(stock Q3, OpenArena, hand-built, or strafegen output) — the foundation for
"do any map" and for learning from shipped level design.

  import_bsp(path_or_bytes) -> scene dict {bounds, brushes[], entities[], ...}
  python3 bsp_import.py map.bsp            # print a geometry digest
  python3 bsp_import.py pack.pk3 --map q3dm6 --json scene.json

Surface types handled: planar (1) and triangle-soup/mesh (3) via the index list;
patches (2) are approximated by triangulating their control-point grid (no Bezier
subdivision — enough to read the shape); flares (4) are skipped.
"""
import struct
import sys
import os
import zipfile
import json
import argparse

# reuse the text-lump parsers from the study tool
from bsp_study import read_lumps, parse_entities, parse_shaders, \
    LUMP_ENTITIES, LUMP_SHADERS

LUMP_DRAWVERTS = 10
LUMP_DRAWINDEXES = 11
LUMP_SURFACES = 13

SZ_VERT = 44       # <10f4B
SZ_SURFACE = 104   # <12i12f2i
MST_PLANAR, MST_PATCH, MST_TRISOUP, MST_FLARE = 1, 2, 3, 4

TRI_CAP = 120000   # safety bound on emitted triangles (huge maps get truncated)


def _verts(blob):
    out = []
    for i in range(0, len(blob) - SZ_VERT + 1, SZ_VERT):
        f = struct.unpack_from("<10f4B", blob, i)
        out.append((f[0], f[1], f[2],            # pos
                    f[7], f[8], f[9],            # normal
                    f[10], f[11], f[12]))        # color rgb (drop alpha)
    return out


def _indexes(blob):
    n = len(blob) // 4
    return list(struct.unpack_from(f"<{n}i", blob, 0)) if n else []


def _surfaces(blob):
    out = []
    for i in range(0, len(blob) - SZ_SURFACE + 1, SZ_SURFACE):
        v = struct.unpack_from("<12i12f2i", blob, i)
        out.append({
            "shader": v[0], "type": v[2],
            "firstVert": v[3], "numVerts": v[4],
            "firstIndex": v[5], "numIndex": v[6],
            "normal": (v[21], v[22], v[23]),
            "patchW": v[24], "patchH": v[25],
        })
    return out


def _hex(r, g, b):
    return f"#{max(0,min(255,int(r))):02x}{max(0,min(255,int(g))):02x}{max(0,min(255,int(b))):02x}"


def _tris_for_surface(s, verts, indexes):
    """Yield (p0,p1,p2) vertex tuples for a surface."""
    fv, nv = s["firstVert"], s["numVerts"]
    if s["type"] in (MST_PLANAR, MST_TRISOUP):
        idx = indexes[s["firstIndex"]:s["firstIndex"] + s["numIndex"]]
        for k in range(0, len(idx) - 2, 3):
            a, b, c = idx[k], idx[k + 1], idx[k + 2]
            if fv + max(a, b, c) < len(verts):
                yield verts[fv + a], verts[fv + b], verts[fv + c]
    elif s["type"] == MST_PATCH and s["patchW"] >= 2 and s["patchH"] >= 2:
        w, h = s["patchW"], s["patchH"]
        if fv + w * h > len(verts):
            return
        g = [verts[fv + r * w:fv + (r + 1) * w] for r in range(h)]
        for r in range(h - 1):           # quad grid over the control net
            for c in range(w - 1):
                yield g[r][c], g[r][c + 1], g[r + 1][c]
                yield g[r][c + 1], g[r + 1][c + 1], g[r + 1][c]
    # MST_FLARE and degenerate patches: nothing renderable


def import_bsp(src, name="imported"):
    data = src if isinstance(src, (bytes, bytearray)) else open(src, "rb").read()
    version, lumps = read_lumps(data)
    eo, el = lumps[LUMP_ENTITIES]
    ent_text = data[eo:eo + el].split(b"\x00", 1)[0].decode("latin-1")
    entities = parse_entities(ent_text)
    so, sl = lumps[LUMP_SHADERS]
    shaders = parse_shaders(data[so:so + sl])
    vo, vl = lumps[LUMP_DRAWVERTS]
    io, il = lumps[LUMP_DRAWINDEXES]
    fo, fl = lumps[LUMP_SURFACES]
    verts = _verts(data[vo:vo + vl])
    indexes = _indexes(data[io:io + il])
    surfaces = _surfaces(data[fo:fo + fl])

    brushes, ntri, truncated = [], 0, False
    bb = [1e30, 1e30, 1e30, -1e30, -1e30, -1e30]
    for sid, s in enumerate(surfaces):
        shname = shaders[s["shader"]][0] if 0 <= s["shader"] < len(shaders) else "?"
        if "sky" in shname.lower() or "caulk" in shname.lower() or "nodraw" in shname.lower():
            role = "sky/enclosure"
        else:
            role = "imported"
        faces = []
        for (p0, p1, p2) in _tris_for_surface(s, verts, indexes):
            if ntri >= TRI_CAP:
                truncated = True
                break
            ntri += 1
            col = _hex((p0[6] + p1[6] + p2[6]) / 3,
                       (p0[7] + p1[7] + p2[7]) / 3,
                       (p0[8] + p1[8] + p2[8]) / 3)
            poly = [[p[0], p[1], p[2]] for p in (p0, p1, p2)]
            for p in poly:
                bb[0], bb[1], bb[2] = min(bb[0], p[0]), min(bb[1], p[1]), min(bb[2], p[2])
                bb[3], bb[4], bb[5] = max(bb[3], p[0]), max(bb[4], p[1]), max(bb[5], p[2])
            faces.append({"poly": poly, "normal": list(s["normal"]),
                          "color": col, "draw": True})
        if not faces:
            continue
        xs = [c for fc in faces for v in fc["poly"] for c in (v[0],)]
        ys = [v[1] for fc in faces for v in fc["poly"]]
        zs = [v[2] for fc in faces for v in fc["poly"]]
        brushes.append({
            "id": sid, "role": role, "shader": shname,
            "color": faces[0]["color"],
            "aabb": [min(xs), min(ys), min(zs), max(xs), max(ys), max(zs)],
            "aabb_editable": False, "contents": 1, "faces": faces})
        if truncated:
            break

    if bb[0] > bb[3]:
        bb = [0, 0, 0, 1, 1, 1]
    ents_out = []
    for i, e in enumerate(entities):
        o = e.get("origin", "").split()
        origin = [float(o[0]), float(o[1]), float(o[2])] if len(o) == 3 else None
        ents_out.append({"id": i, "classname": e.get("classname", ""),
                         "origin": origin,
                         "keys": {k: v for k, v in e.items()
                                  if k not in ("classname", "origin")}})
    return {
        "imported": True, "name": name, "version": version,
        "bounds": bb, "brushes": brushes, "entities": ents_out,
        "triggers": [], "movers": [], "flows": [], "sections": [],
        "counts": {"brushes": len(brushes), "surfaces": len(surfaces),
                   "triangles": ntri, "entities": len(ents_out),
                   "spawns": sum(1 for e in entities
                                 if e.get("classname", "").startswith("info_player")),
                   "truncated": truncated},
        "shaders": [s[0] for s in shaders],
    }


def _load_pk3(path, mapname=None):
    with zipfile.ZipFile(path) as z:
        bsps = [n for n in z.namelist() if n.lower().endswith(".bsp")]
        if mapname:
            bsps = [n for n in bsps if os.path.splitext(os.path.basename(n))[0] == mapname]
        if not bsps:
            raise SystemExit("no matching .bsp in pk3")
        return z.read(bsps[0]), os.path.basename(bsps[0])


def main():
    ap = argparse.ArgumentParser(description="Decompile a .bsp into a MapForge scene")
    ap.add_argument("path")
    ap.add_argument("--map", help="with a .pk3: pick this map basename")
    ap.add_argument("--json", help="write the scene JSON here")
    args = ap.parse_args()
    if args.path.lower().endswith((".pk3", ".zip")):
        data, name = _load_pk3(args.path, args.map)
        scene = import_bsp(data, os.path.splitext(name)[0])
    else:
        scene = import_bsp(args.path, os.path.splitext(os.path.basename(args.path))[0])
    c = scene["counts"]
    print(f"{scene['name']}: IBSP v{scene['version']}  "
          f"{c['brushes']} drawn surfaces, {c['triangles']} triangles, "
          f"{c['entities']} entities, {c['spawns']} spawns"
          + ("  [TRUNCATED]" if c["truncated"] else ""))
    b = scene["bounds"]
    print(f"  bounds {int(b[3]-b[0])}x{int(b[4]-b[1])}x{int(b[5]-b[2])}u   "
          f"shaders: {len(scene['shaders'])}")
    if args.json:
        with open(args.json, "w") as f:
            json.dump(scene, f)
        print(f"  wrote {args.json}")


if __name__ == "__main__":
    main()
