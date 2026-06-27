#!/usr/bin/env python3
"""scene.py — shared serializer / build-dispatch / edit-apply for strafegen maps.

Single source of truth for turning a generated map into a renderable scene and
back. Both ``mapview.py`` (SVG) and ``mapforge/`` (the browser tool) import it, so
the role/colour vocabulary and the geometry extraction stay in one place.

  build_kind(kind, params) -> course   build any kind in-memory (no engine/bspc),
                                        greebled + scaled, ready to serialize/write
  apply_edits(course, edits) -> course  apply the JSON edit ops (entities+boxes)
  serialize(course) -> dict             scene JSON: bounds, brushes, entities, ...
  meta() -> dict                        kinds, params, archetypes, role legend

The course object the writers consume needs only .solids / .triggers / .entities
(/.movers /.seed) — see strafegen_bsp.BspWriter. Edits operate on .solids and
.entities; ids are positions in those lists at build time (deterministic per seed,
so stable across a rebuild — the server is stateless and rebuilds per request).
"""
import strafegen as sg
import strafegen_config as cfg
import strafegen_geom as geom    # _is_axis_box is private — not re-exported by `import *`


# ---- role classification: palette RGB -> (label, fill hex, z-order hint) --------
# Mirrors and extends mapview's table; covers course palettes (start/finish/check)
# and the concrete theme so every kind classifies. faces[0].palette wins (boxes are
# single-palette). Unknown palettes fall back to a neutral "other" role so the 3D
# view still renders them — nothing is silently dropped.
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
    sg.PAL_START:     ("start pad",         "#96ff96", 4),
    sg.PAL_FINISH:    ("finish line",       "#96ffdc", 4),
    sg.PAL_CHECK:     ("checkpoint",        "#78f0ff", 4),
    sg.PAL_CRETE:      ("concrete",         "#e4e4e8", 2),
    sg.PAL_CRETE_WARM: ("concrete floor",   "#e8e4dc", 0),
    sg.PAL_CRETE_TRIM: ("concrete trim",    "#f5f3f0", 3),
    sg.PAL_CRETE_DARK: ("concrete shadow",  "#c4c4cc", 2),
}
_OTHER = ("other", "#6c6c78", 2)

# role -> (palette, texture) for brush.add (inverse of ROLE, plus a sane tex)
ROLE_BUILD = {
    "deck/floor":     (sg.SRC_ORANGE, sg.TEX_FLOOR),
    "structure":      (sg.SRC_GREY,   sg.TEX_WALL),
    "platform/ledge": (sg.SRC_TRIM,   sg.TEX_FLOOR),
    "hazard":         (sg.PAL_DANGER, sg.TEX_FLOOR),
    "concrete":       (sg.PAL_CRETE,  sg.TEX_WALL),
}

# entity markers: classname-prefix -> (label, glyph, fill)
ENT = [
    ("info_player_deathmatch", ("spawn",   "S", "#8fff8f")),
    ("info_player_start",      ("spawn",   "S", "#8fff8f")),
    ("item_quad",              ("quad",    "Q", "#cf57f0")),
    ("item_health_mega",       ("mega",    "M", "#ff7777")),
    ("item_health",            ("health",  "h", "#ff9b9b")),
    ("item_armor",             ("armor",   "A", "#7fb0ff")),
    ("weapon_",                ("weapon",  "W", "#ffd27f")),
    ("misc_teleporter_dest",   ("portal",  "P", "#54dcf0")),
    ("target_position",        ("target",  "t", "#c0c0c0")),
]

# entities the editor can ADD from a palette
ENTITY_PALETTE = [
    {"classname": "info_player_deathmatch", "label": "Spawn"},
    {"classname": "item_armor",             "label": "Armor"},
    {"classname": "item_health",            "label": "Health"},
    {"classname": "item_health_mega",       "label": "Mega health"},
    {"classname": "item_quad",              "label": "Quad"},
    {"classname": "weapon_rocketlauncher",  "label": "Rocket launcher"},
    {"classname": "weapon_railgun",         "label": "Railgun"},
    {"classname": "weapon_lightning",       "label": "Lightning gun"},
]

ARCHETYPES = ["spire", "spiral", "forest", "ring", "cross", "twin", "court"]
SECTION_KEYS = ["gaps", "bhop", "slide", "walls", "slalom", "hurdles",
                "movers", "hazard", "fork", "tower"]


# ----------------------------- small helpers ------------------------------------
def _hex(rgb):
    r, g, b = (max(0, min(255, int(round(c)))) for c in rgb)
    return f"#{r:02x}{g:02x}{b:02x}"


def _vec(v):
    """[x,y,z] -> the engine's "x y z" string (entity origins are strings)."""
    x, y, z = (float(c) for c in v)
    return f"{x:g} {y:g} {z:g}"


def _parse_vec(s):
    try:
        return [float(c) for c in str(s).split()][:3]
    except (ValueError, AttributeError):
        return None


def aabb(brush):
    return [*brush.mins, *brush.maxs]


def classify(brush):
    """First face palette wins (boxes are single-palette). Returns a ROLE tuple."""
    pal = tuple(brush.faces[0].palette) if brush.faces else None
    return ROLE.get(pal, _OTHER)


def _normal_key(normal):
    """Map a face outward normal back to one of FACE_KEYS (axis boxes only)."""
    best, bestdot = None, 0.0
    for k in sg.FACE_KEYS:
        d = sum(a * b for a, b in zip(sg.FACE_NORMALS[k], normal))
        if d > bestdot:
            best, bestdot = k, d
    return best


def _box_state(b):
    """Recover make_box inputs from an axis-box brush so a rebuilt box keeps its
    look: the base tex, the set of drawn face keys, and any per-face tex override."""
    base = b.faces[0].tex
    draw, face_tex = set(), {}
    for f in b.faces:
        k = _normal_key(f.normal)
        if f.draw:
            draw.add(k)
        if f.tex != base:
            face_tex[k] = f.tex
    pal = tuple(b.faces[0].palette)
    return base, draw, face_tex, pal, b.contents


def _is_sky(b):
    return any(f.tex == sg.TEX_SKY for f in b.faces)


def _is_enclosure(ab, bounds):
    """True for a world-boundary shell brush (the sealing wall/ceiling slabs that
    span the whole footprint) so the 3D view can hide them and see inside — like
    mapview's "drop the enclosure" filter. The bottom floor slab is deliberately
    kept (a backdrop), so only full-height walls and the ceiling are flagged."""
    x0, y0, z0, x1, y1, z1 = ab
    W = (bounds[3] - bounds[0]) or 1
    D = (bounds[4] - bounds[1]) or 1
    H = (bounds[5] - bounds[2]) or 1
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    spanX, spanY, spanZ = dx >= 0.9 * W, dy >= 0.9 * D, dz >= 0.9 * H
    # vertical wall: full height + full along one horizontal axis + thin on the other
    if spanZ and (spanX or spanY) and min(dx, dy) < 0.2 * max(W, D):
        return True
    # ceiling: thin slab spanning both horizontals, sitting in the top half
    if spanX and spanY and dz < 0.1 * H and z0 > bounds[2] + 0.5 * H:
        return True
    return False


# ----------------------------- build dispatch -----------------------------------
def _mods(params):
    secs = params.get("sections")
    return cfg.GenMods(vscale=float(params.get("vscale", 1.0)),
                       hscale=float(params.get("hscale", 1.0)),
                       density=float(params.get("density", 1.0)),
                       sections=(set(secs) if secs else None))


def build_kind(kind, params):
    """Build any kind in-memory and return the unwritten course, greebled + scaled
    exactly as strafegen.generate() would (so preview coords == export coords).
    Never call BspWriter on the returned object twice — write() mutates it."""
    seed = int(params.get("seed", 64))
    diff = int(params.get("difficulty", 1))
    length = int(params.get("length", 1))
    arch = params.get("archetype") or None
    theme = params.get("theme", "default")
    void = bool(params.get("void", True))
    voidrise = params.get("voidrise")
    voiddelay = params.get("voiddelay")
    mods = _mods(params)
    cfg.THEME = theme

    if kind == "killbox":
        course = sg.Killbox(seed, diff, archetype=arch, mods=mods).build()
    elif kind == "arena":
        course = sg.Arena(seed, diff).build()
    elif kind == "surf":
        course = sg.SurfLine(seed).build()
    elif kind == "surfturn":
        course = sg.SurfTurn(seed).build()
    elif kind in ("lattice", "lattice_lite"):
        course = sg.LatticeArena(seed, weapons=(kind == "lattice")).build()
    elif kind == "pit":
        course = sg.Pit(seed).build()
    elif kind.startswith("dojo:"):
        recipe = sg.DOJO_RECIPES[kind.split(":", 1)[1]]
        course = sg.Course(seed, diff, 1, void=void, voidrise=voidrise,
                           voiddelay=voiddelay, recipe=recipe, mods=mods).build()
    elif kind == "combat":
        course = sg.Course(seed, diff, length, void=void, voidrise=voidrise,
                           voiddelay=voiddelay, combat=True, mods=mods).build()
    elif kind in ("course", "", None):
        course = sg.Course(seed, diff, length, void=void, voidrise=voidrise,
                           voiddelay=voiddelay, mods=mods).build()
    else:
        raise ValueError(f"unknown kind: {kind}")

    if theme == "concrete":
        gd = float(params.get("greeble_density", 1.0))
        if gd > 0:
            sg.greeble_course(course, seed=getattr(course, "seed", 0), density=gd)
    # universal post-build resize (identity at 1,1,1 -> no-op, byte-unchanged)
    sg.scale_course(course, mods.hscale, mods.hscale, mods.vscale)
    return course


def default_name(kind, params):
    seed = int(params.get("seed", 64))
    diff = int(params.get("difficulty", 1))
    length = int(params.get("length", 1))
    dtag = "" if diff == 1 else f"_d{diff}"
    if kind == "killbox":
        arch = params.get("archetype")
        base = f"strafe64kb_{arch}" if arch else "strafe64kb"
        return f"{base}_{seed}{dtag}"
    if kind == "arena":
        return f"strafe64dm_{seed}{dtag}"
    if kind == "surf":
        return f"surf_{seed}"
    if kind == "surfturn":
        return f"surfturn_{seed}"
    if kind in ("lattice", "lattice_lite"):
        return f"lattice_arena{'_lite' if kind == 'lattice_lite' else ''}_{seed}"
    if kind == "pit":
        return f"dojo_arena_{seed}"
    if kind.startswith("dojo:"):
        return f"dojo_{kind.split(':', 1)[1]}_{seed}"
    tag = "cb" if kind == "combat" else ""
    ltag = "" if length == 1 else f"_x{length}"
    return f"strafe64{tag}_{seed}{dtag}{ltag}"


# ----------------------------- edit application ---------------------------------
def _rebuild_box(b, mins, maxs):
    for i in range(3):
        if maxs[i] - mins[i] < 1.0:
            raise ValueError(f"brush extent < 1u on axis {i} ({mins} -> {maxs})")
    base, draw, face_tex, pal, contents = _box_state(b)
    return sg.make_box(tuple(mins), tuple(maxs), tex=base, palette=pal,
                       contents=contents, draw=draw, face_tex=face_tex or None)


def _new_box(mins, maxs, role):
    for i in range(3):
        if maxs[i] - mins[i] < 1.0:
            raise ValueError(f"new brush extent < 1u on axis {i}")
    pal, tex = ROLE_BUILD.get(role, (sg.SRC_GREY, sg.TEX_WALL))
    return sg.make_box(tuple(mins), tuple(maxs), tex=tex, palette=pal,
                       contents=sg.CONTENTS_SOLID)


def apply_edits(course, edits):
    """Apply ordered JSON edit ops. ids reference positions in the freshly-built
    course's .entities / .solids (the same snapshot serialize() reports). Deletes
    are deferred so earlier ids stay valid; adds are appended last."""
    if not edits:
        return course
    ents = list(course.entities)
    solids = list(course.solids)
    ent_del, brush_del, ent_add, brush_add = set(), set(), [], []

    for e in edits:
        op = e.get("op")
        if op == "ent.move":
            i = e["id"]
            ents[i] = dict(ents[i], origin=_vec(e["origin"]))
        elif op == "ent.setkey":
            i = e["id"]
            key = e["key"]
            if (ents[i].get("classname") == "misc_teleporter_dest"
                    and key == "angles"):
                raise ValueError("refusing to edit 'angles' on a teleporter dest "
                                 "(killbox momentum portals depend on it)")
            ents[i] = dict(ents[i], **{key: str(e["value"])})
        elif op == "ent.delete":
            ent_del.add(e["id"])
        elif op == "ent.add":
            d = {"classname": e["classname"], "origin": _vec(e["origin"])}
            d.update({k: str(v) for k, v in (e.get("keys") or {}).items()})
            ent_add.append(d)
        elif op == "brush.move":
            i = e["id"]
            b = solids[i]
            if len(b.faces) != 6 or not geom._is_axis_box(b):
                raise ValueError(f"brush {i} is not an axis box (move unsupported)")
            dx, dy, dz = e["delta"]
            mn = [b.mins[0] + dx, b.mins[1] + dy, b.mins[2] + dz]
            mx = [b.maxs[0] + dx, b.maxs[1] + dy, b.maxs[2] + dz]
            solids[i] = _rebuild_box(b, mn, mx)
        elif op == "brush.resize":
            i = e["id"]
            b = solids[i]
            if len(b.faces) != 6 or not geom._is_axis_box(b):
                raise ValueError(f"brush {i} is not an axis box (resize unsupported)")
            solids[i] = _rebuild_box(b, e["mins"], e["maxs"])
        elif op == "brush.delete":
            brush_del.add(e["id"])
        elif op == "brush.add":
            brush_add.append(_new_box(e["mins"], e["maxs"], e.get("role", "structure")))
        else:
            raise ValueError(f"unknown edit op: {op!r}")

    if 0 in ent_del:
        raise ValueError("cannot delete worldspawn (entity 0)")
    final_ents = [ents[i] for i in range(len(ents)) if i not in ent_del] + ent_add
    if not any(en.get("classname", "").startswith("info_player_deathmatch")
               for en in final_ents):
        raise ValueError("at least one info_player_deathmatch must remain")
    final_solids = [solids[i] for i in range(len(solids))
                    if i not in brush_del] + brush_add
    course.entities = final_ents
    course.solids = final_solids
    return course


# ----------------------------- serialization ------------------------------------
def _serialize_brush(i, b, bounds, detect_enclosure=True):
    role, fill, _zo = classify(b)
    if detect_enclosure and (_is_sky(b) or _is_enclosure(aabb(b), bounds)):
        role, fill = "sky/enclosure", "#2a2a3a"
    faces = [{"poly": [[float(x), float(y), float(z)] for (x, y, z) in f.poly],
              "normal": [float(c) for c in f.normal],
              "color": _hex(f.palette),
              "draw": bool(f.draw)}
             for f in b.faces]
    return {"id": i, "role": role, "color": _hex(b.faces[0].palette) if b.faces else fill,
            "aabb": [float(c) for c in aabb(b)],
            "aabb_editable": bool(len(b.faces) == 6 and geom._is_axis_box(b)),
            "contents": int(b.contents), "faces": faces}


def _serialize_entity(i, e):
    o = _parse_vec(e.get("origin")) if e.get("origin") else None
    keys = {k: v for k, v in e.items() if k not in ("classname", "origin")}
    return {"id": i, "classname": e.get("classname", ""), "origin": o, "keys": keys}


def _flows(course):
    targets = {}
    for e in course.entities:
        tn = e.get("targetname")
        o = _parse_vec(e.get("origin")) if e.get("origin") else None
        if tn and o:
            targets[tn] = o
    out = []
    for brush, info in getattr(course, "triggers", []):
        tgt = info.get("target")
        if not tgt or tgt not in targets:
            continue
        bx0, by0, _, bx1, by1, _ = aabb(brush)
        kind = "pad" if info.get("classname") == "trigger_push" else "portal"
        out.append({"kind": kind,
                    "from": [(bx0 + bx1) / 2, (by0 + by1) / 2],
                    "to": targets[tgt]})
    return out


def serialize(course):
    # world bounds first (raw brush AABBs) so the enclosure heuristic has a frame
    axs = [aabb(b) for b in course.solids] or [[0, 0, 0, 1, 1, 1]]
    bounds = [min(a[0] for a in axs), min(a[1] for a in axs), min(a[2] for a in axs),
              max(a[3] for a in axs), max(a[4] for a in axs), max(a[5] for a in axs)]
    brushes = [_serialize_brush(i, b, bounds) for i, b in enumerate(course.solids)]
    entities = [_serialize_entity(i, e) for i, e in enumerate(course.entities)]
    triggers = []
    for brush, info in getattr(course, "triggers", []):
        triggers.append({"classname": info.get("classname", "trigger"),
                         "aabb": [float(c) for c in aabb(brush)],
                         "target": info.get("target")})
    movers = []
    for brush, info in getattr(course, "movers", []):
        movers.append({"classname": info.get("classname", "func_bobbing"),
                       "aabb": [float(c) for c in aabb(brush)],
                       "color": _hex(brush.faces[0].palette) if brush.faces else "#cccccc"})
    sections = [{"name": n, "info": {k: str(v) for k, v in (info or {}).items()}}
                for n, info in getattr(course, "sections", [])]
    counts = {"brushes": len(brushes), "entities": len(entities),
              "triggers": len(triggers), "movers": len(movers),
              "spawns": sum(1 for e in entities
                            if e["classname"].startswith("info_player"))}
    return {"bounds": bounds, "brushes": brushes, "entities": entities,
            "triggers": triggers, "movers": movers, "flows": _flows(course),
            "sections": sections, "counts": counts,
            "seed": getattr(course, "seed", None)}


def role_legend():
    seen, out = set(), []
    for (label, fill, _zo) in ROLE.values():
        if label in seen:
            continue
        seen.add(label)
        out.append({"role": label, "color": fill})
    out.append({"role": _OTHER[0], "color": _OTHER[1]})
    out.append({"role": "sky/enclosure", "color": "#2a2a3a"})
    return out


def meta():
    kinds = [
        {"id": "course", "label": "Course — linear run",
         "params": ["seed", "difficulty", "length", "theme", "mods", "sections", "void"]},
        {"id": "combat", "label": "Combat course",
         "params": ["seed", "difficulty", "length", "theme", "mods", "void"]},
        {"id": "arena", "label": "Arena — velodrome DM",
         "params": ["seed", "difficulty"]},
        {"id": "killbox", "label": "Killbox — vertical melee",
         "params": ["seed", "difficulty", "archetype", "theme", "mods"]},
        {"id": "surf", "label": "Surf line", "params": ["seed"]},
        {"id": "surfturn", "label": "Surf turn", "params": ["seed"]},
        {"id": "lattice", "label": "Lattice arena", "params": ["seed"]},
        {"id": "lattice_lite", "label": "Lattice arena — no weapons",
         "params": ["seed"]},
        {"id": "pit", "label": "Combat pit", "params": ["seed"]},
    ]
    kinds += [{"id": f"dojo:{d}", "label": f"Dojo — {d}",
               "params": ["seed", "difficulty"]} for d in sg.DOJO_RECIPES]
    return {"kinds": kinds, "archetypes": ARCHETYPES, "sections": SECTION_KEYS,
            "difficulties": [0, 1, 2], "themes": ["default", "concrete"],
            "legend": role_legend(), "entityPalette": ENTITY_PALETTE,
            "addRoles": list(ROLE_BUILD.keys())}


# ============================================================================
# COMPOSE — section kit-bash: each section is a standalone "part" with entry /
# exit connectors; the browser places + snaps parts, the server bakes them into
# one sealed map. A part is a Course section built at origin facing +Y; its
# connectors are the cursor before (entry) and after (exit) the section runs.
# ============================================================================
SECTION_PARTS = [
    ("start",   "sec_start",   "Start pad",        "start"),
    ("gaps",    "sec_gaps",    "Strafe gaps",      "opener"),
    ("bhop",    "sec_bhop",    "Bhop lane",        "opener"),
    ("fork",    "sec_fork",    "Fork (branch)",    "flow"),
    ("slalom",  "sec_slalom",  "Slalom",           "flow"),
    ("slide",   "sec_slide",   "Slide ramp",       "flow"),
    ("walls",   "sec_walls",   "Walljump hall",    "flow"),
    ("hurdles", "sec_hurdles", "Hurdles",          "spice"),
    ("movers",  "sec_movers",  "Moving platforms", "spice"),
    ("hazard",  "sec_hazard",  "Hazard pits",      "spice"),
    ("tower",   "sec_tower",   "Double-jump tower", "climb"),
    ("finish",  "sec_finish",  "Finish gate",      "finish"),
]
# Geometric PRIMITIVES — parametric building blocks (not procedural sections) so
# you can route a map ANY direction and shape it freely. Same part contract
# (entry at origin facing +Y, an exit connector), so they slot into compose/snap/
# export unchanged. Turns are the key unlock: sections only run +Y, turns reorient.
PRIMITIVE_PARTS = [
    ("floor",      "Floor (straight)", "primitive"),
    ("ramp_up",    "Ramp up",          "primitive"),
    ("ramp_down",  "Ramp down",        "primitive"),
    ("stairs",     "Stairs up",        "primitive"),
    ("turn_left",  "Turn left 90°",    "primitive"),
    ("turn_right", "Turn right 90°",   "primitive"),
    ("arena",      "Open arena pad",   "primitive"),
]
_SECTION_METHOD = {k: m for (k, m, _l, _g) in SECTION_PARTS}
_PART_META = {k: (lbl, grp) for (k, _m, lbl, grp) in SECTION_PARTS}
_PART_META.update({k: (lbl, grp) for (k, lbl, grp) in PRIMITIVE_PARTS})
_PRIM_KEYS = {k for (k, _l, _g) in PRIMITIVE_PARTS}
_PART_CACHE = {}


def _build_primitive(key):
    """Construct a primitive's geometry directly (make_box / make_prism) with
    entry (0,0,0 facing +Y) and an exit connector. Returns a part dict."""
    S = []
    box = lambda mn, mx, pal=sg.SRC_ORANGE, tex=sg.TEX_FLOOR: \
        S.append(sg.make_box(mn, mx, tex=tex, palette=pal))
    exit_ = ((0.0, 512.0, 0.0), (0, 1))
    if key == "floor":
        box((-128, 0, -24), (128, 512, 0))
    elif key == "ramp_up":
        S.append(sg.make_prism([(-128, 0), (128, 0), (128, 512), (-128, 512)],
                               -24, [0, 0, 256, 256], tex=sg.TEX_WALL, palette=sg.SRC_GREY))
        exit_ = ((0.0, 512.0, 256.0), (0, 1))
    elif key == "ramp_down":
        S.append(sg.make_prism([(-128, 0), (128, 0), (128, 512), (-128, 512)],
                               -280, [0, 0, -256, -256], tex=sg.TEX_WALL, palette=sg.SRC_GREY))
        exit_ = ((0.0, 512.0, -256.0), (0, 1))
    elif key == "stairs":
        for i in range(8):
            box((-128, i * 64, -24), (128, (i + 1) * 64, (i + 1) * 32), pal=sg.SRC_TRIM)
        exit_ = ((0.0, 512.0, 256.0), (0, 1))
    elif key == "turn_left":
        box((-256, 0, -24), (256, 512, 0))
        exit_ = ((-256.0, 256.0, 0.0), (-1, 0))
    elif key == "turn_right":
        box((-256, 0, -24), (256, 512, 0))
        exit_ = ((256.0, 256.0, 0.0), (1, 0))
    elif key == "arena":
        box((-512, 0, -24), (512, 1024, 0))
        exit_ = ((0.0, 1024.0, 0.0), (0, 1))
    else:
        raise ValueError(f"unknown primitive: {key}")
    return {"key": key, "solids": S, "entities": [], "triggers": [], "movers": [],
            "entry": ((0.0, 0.0, 0.0), (0, 1)), "exit": exit_}


def _build_part(key):
    """Build one section/primitive standalone at the origin (entry at 0,0,0 facing
    +Y), capturing its geometry + exit connector. Cached (deterministic)."""
    if key in _PART_CACHE:
        return _PART_CACHE[key]
    if key in _PRIM_KEYS:
        part = _build_primitive(key)
    else:
        c = sg.Course(1337, 1, 1)
        c.pos = [0.0, 0.0, 0.0]
        c.dir = (0, 1)
        s0, e0, t0, m0 = (len(c.solids), len(c.entities), len(c.triggers), len(c.movers))
        getattr(c, _SECTION_METHOD[key])()
        part = {"key": key,
                "solids": c.solids[s0:], "entities": c.entities[e0:],
                "triggers": c.triggers[t0:], "movers": c.movers[m0:],
                "entry": ((0.0, 0.0, 0.0), (0, 1)),
                "exit": (tuple(c.pos), tuple(c.dir))}
    _PART_CACHE[key] = part
    return part


def serialize_part(key):
    P = _build_part(key)
    label, group = _PART_META[key]
    axs = [aabb(b) for b in P["solids"]] or [[0, 0, 0, 1, 1, 1]]
    bounds = [min(a[0] for a in axs), min(a[1] for a in axs), min(a[2] for a in axs),
              max(a[3] for a in axs), max(a[4] for a in axs), max(a[5] for a in axs)]
    brushes = [_serialize_brush(i, b, bounds, detect_enclosure=False)
               for i, b in enumerate(P["solids"])]
    entities = [_serialize_entity(i, e) for i, e in enumerate(P["entities"])]
    conns = [{"id": "in",  "pos": [0.0, 0.0, 0.0], "dir": list(P["entry"][1])},
             {"id": "out", "pos": [float(c) for c in P["exit"][0]],
              "dir": list(P["exit"][1])}]
    return {"key": key, "label": label, "group": group, "bounds": bounds,
            "brushes": brushes, "entities": entities, "connectors": conns}


def parts_catalog():
    keys = [k for (k, *_r) in SECTION_PARTS] + [k for (k, *_r) in PRIMITIVE_PARTS]
    return {"parts": [serialize_part(k) for k in keys]}


def _rotxy(x, y, yaw):
    yaw %= 360
    if yaw == 0:
        return x, y
    if yaw == 90:
        return -y, x
    if yaw == 180:
        return -x, -y
    if yaw == 270:
        return y, -x
    import math
    a = math.radians(yaw)
    ca, sa = math.cos(a), math.sin(a)
    return x * ca - y * sa, x * sa + y * ca


def _xform_brush(b, yaw, tx):
    faces = []
    for f in b.faces:
        poly = []
        for (x, y, z) in f.poly:
            rx, ry = _rotxy(x, y, yaw)
            poly.append((rx + tx[0], ry + tx[1], z + tx[2]))
        faces.append(geom.Face(poly, f.tex, tuple(f.palette), draw=f.draw))
    return geom.Brush(faces, b.contents)


def _xform_entity(e, yaw, tx):
    d = dict(e)
    o = _parse_vec(e.get("origin")) if e.get("origin") else None
    if o:
        rx, ry = _rotxy(o[0], o[1], yaw)
        d["origin"] = _vec([rx + tx[0], ry + tx[1], o[2] + tx[2]])
    if "angle" in d:
        try:
            d["angle"] = str(int((float(d["angle"]) + yaw) % 360))
        except ValueError:
            pass
    return d


def compose(placed, opts=None, brushes=None):
    """Bake placed+transformed section parts (and any free-form box brushes) into
    one sealed, playable course.

    placed:  [{key, yaw, translate:[x,y,z]}]   — section/primitive parts
    brushes: [{aabb:[6], role}]                — free-form boxes in world space
    Returns a course-like object ready for BspWriter / write_map."""
    import types
    opts = opts or {}
    solids, entities, triggers, movers = [], [], [], []
    for pl in placed:
        P = _build_part(pl["key"])
        yaw = float(pl.get("yaw", 0))
        tx = pl.get("translate", [0, 0, 0])
        solids += [_xform_brush(b, yaw, tx) for b in P["solids"]]
        entities += [_xform_entity(e, yaw, tx) for e in P["entities"]]
        triggers += [(_xform_brush(b, yaw, tx), dict(info)) for b, info in P["triggers"]]
        movers += [(_xform_brush(b, yaw, tx), dict(info)) for b, info in P["movers"]]
    for fb in (brushes or []):
        a = fb["aabb"]
        solids.append(_new_box(a[:3], a[3:], fb.get("role", "structure")))
    if not solids:
        raise ValueError("nothing placed — add a section or a brush")
    # guarantee a spawn (+ rescue dest) so the map loads even without a start part
    if not any(e.get("classname", "").startswith("info_player_deathmatch")
               for e in entities):
        b0 = solids[0]
        cx, cy = (b0.mins[0] + b0.maxs[0]) / 2, (b0.mins[1] + b0.maxs[1]) / 2
        cz = b0.maxs[2] + 40
        entities.append({"classname": "info_player_deathmatch",
                         "origin": _vec([cx, cy, cz]), "angle": "90"})
        if not any(e.get("targetname") == "start_dest" for e in entities):
            entities.append({"classname": "misc_teleporter_dest",
                             "targetname": "start_dest",
                             "origin": _vec([cx, cy, cz]), "angle": "90"})
    course = types.SimpleNamespace(solids=solids, entities=entities,
                                   triggers=triggers, movers=movers,
                                   seed=int(opts.get("seed", 64)))
    _seal(course, void=bool(opts.get("void", True)))
    return course


def _seal(course, void=True):
    """Wrap a sky enclosure + (optional) void-rescue around the composed geometry
    and prepend a worldspawn — mirrors Course.add_void_and_sky."""
    xs = [c for b in course.solids for c in (b.mins[0], b.maxs[0])]
    ys = [c for b in course.solids for c in (b.mins[1], b.maxs[1])]
    zs = [c for b in course.solids for c in (b.mins[2], b.maxs[2])]
    m = 768.0
    x0, x1 = min(xs) - m, max(xs) + m
    y0, y1 = min(ys) - m, max(ys) + m
    z0, z1 = min(zs) - 896.0, max(zs) + 1024.0
    has_dest = any(e.get("targetname") == "start_dest" for e in course.entities)
    if void and has_dest:
        vb = geom.make_box((x0 + 8, y0 + 8, z0 + 256), (x1 - 8, y1 - 8, z0 + 320),
                           tex=sg.TEX_TRIGGER, contents=sg.CONTENTS_TRIGGER, draw=set())
        course.triggers.append((vb, {"classname": "trigger_teleport",
                                     "spawnflags": "2", "target": "start_dest"}))
    course.solids += sg.make_skybox(x0, y0, z0, x1, y1, z1)
    ws = {"classname": "worldspawn", "message": "STRAFE 64 — forged in MapForge"}
    if void:
        ws["voidbase"] = f"{z0 + 64:g}"
        ws["voidrise"] = "40"
        ws["voiddelay"] = "30"
    course.entities.insert(0, ws)
