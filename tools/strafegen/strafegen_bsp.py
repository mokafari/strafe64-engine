"""strafegen_bsp — IBSP v46 writer, .map writer and bsp validator.

Bakes a Course/Arena/Killbox into a vertex-lit IBSP (no q3map compile). Carries
the BSP-format constants and the dusk vertex-lighting model.
"""
import struct

import strafegen_config as cfg
from strafegen_geom import *
from strafegen_palettes import *
from strafegen_palettes import _glow_tex, theme_remap

# ---- BSP format (code/qcommon/qfiles.h, IBSP version 46) ----
BSP_IDENT   = b"IBSP"
BSP_VERSION = 46

LUMP_ENTITIES, LUMP_SHADERS, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, \
    LUMP_LEAFSURFACES, LUMP_LEAFBRUSHES, LUMP_MODELS, LUMP_BRUSHES, \
    LUMP_BRUSHSIDES, LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_FOGS, \
    LUMP_SURFACES, LUMP_LIGHTMAPS, LUMP_LIGHTGRID, LUMP_VISIBILITY = range(17)
HEADER_LUMPS = 17

MST_PLANAR         = 1
LIGHTMAP_BY_VERTEX = -3   # vertex-lit surfaces; no lightmap lump needed


# global atmospheric fog: the play volume is wrapped in one CONTENTS_FOG box so
# distance fades world geometry toward the synthwave horizon colour (depth =
# units to opaque). Sky surfaces are excluded so the skybox stays crisp.
TEX_FOG      = "textures/strafe64/fog"
FOG_COLOR    = (0.05, 0.04, 0.10)   # dark blue-purple: depth cue, not a recolour
FOG_DEPTH    = 7000                 # far fade — keeps the near track high-contrast

SZ_SHADER    = 72   # <64s2i
SZ_FOGS      = 72   # <64s2i : char shader[64]; int brushNum; int visibleSide
SZ_PLANE     = 16   # <4f
SZ_NODE      = 36   # <9i
SZ_LEAF      = 48   # <12i
SZ_MODEL     = 40   # <6f4i
SZ_BRUSH     = 12   # <3i
SZ_BRUSHSIDE = 8    # <2i
SZ_DRAWVERT  = 44   # <10f4B
SZ_SURFACE   = 104  # <12i12f2i


# ======================================================================
# BSP writer
# ======================================================================
class _Store:
    """Deduplicating index store."""

    def __init__(self):
        self.items = []
        self.index = {}

    def add(self, key, item):
        if key in self.index:
            return self.index[key]
        self.index[key] = len(self.items)
        self.items.append(item)
        return self.index[key]


# ---- dusk vertex lighting -------------------------------------------------
# The world is vertex-lit (no lightmaps), so all "lighting" is a per-channel
# RGB multiplier baked into the vertex colour at emit time — zero runtime cost,
# still N64-crisp. The old model was a flat 4-tier grey scalar (top 1.0 / sides
# 0.8,0.64 / bottom 0.45) which read mood-dead. This replaces it with a small
# physically-flavoured model aligned to the VISIBLE sky sun (_build_synthsky:
# SUN_AZ 0, SUN_EL ~0.10 — a low warm sun toward +x):
#   * a HEMISPHERIC ambient — cool dusk skylight from above, a dim warm-violet
#     bounce from below, lerped by the face's up-ness — so floors read cool and
#     undersides fall to warm shadow;
#   * a warm SUN KEY added where a face turns toward the sun, so the +x-facing
#     walls glow warm and bright while the shadow side stays cool and dark.
# Net: real directional form + a warm/cool dusk palette, with more contrast than
# the old flat tiers, while floors stay bright enough to read the track at MACH.
_SUN_DIR = (0.9950, 0.0, 0.0998)        # toward the sky sun: +x, ~6deg elevation
_KEY_RGB = (0.55, 0.40, 0.23)           # warm sun key, added per-channel * n.l
_SKY_AMB = (0.80, 0.84, 0.96)           # cool skylight (up-facing ambient)
_GND_AMB = (0.36, 0.31, 0.34)           # dim warm-violet bounce (down-facing)

# ---- per-seed light-hue families ------------------------------------------
# Q3-map study (docs/q3-map-study.md): the maps that read as *designed* commit
# to ONE light-hue family (lun3dm2 blue, lun3dm3 green, de4th_run2 purple) and
# light everything from it. We do the same: each seed picks a family that tints
# the skylight ambient + the dome-fill suns. Gameplay accent colours (pads,
# danger, glow shaders in strafegen_palettes) are deliberately NOT recoloured —
# they must stay readable. A family is (sky_amb, gnd_amb, key_rgb, fill_rgb):
#   sky/gnd  = hemispheric ambient (up / down) — carries the hue.
#   key      = the dominant warm-ish sun key (the painted +x sun direction).
#   fill     = colour of the dim dome-fill suns from the other directions.
# Hue lives in the SHADOWS and WALLS, not the floor. The real maps (lun3dm2 etc.)
# read as one-hue but the floor still reads neutral-bright — the colour is in the
# fill light and the shaded undersides. So: sky_amb (hits up-facing floors) is
# near-neutral with only a slight tint; gnd_amb (down-facing shadow) and fill_rgb
# (the dome's wall light) carry the SATURATED hue. key_rgb (the sun disc) stays
# warm-neutral across families so sunlit faces read "lit", not "painted".
_HUE_FAMILIES = {
    # name      sky_amb  (floor, ~neutral)   gnd_amb (shadow, saturated)  key_rgb (warm sun)    fill_rgb (wall, saturated)
    "dusk":   ((0.84, 0.86, 0.94), (0.34, 0.30, 0.36), (0.55, 0.40, 0.23), (0.18, 0.20, 0.26)),
    "blue":   ((0.80, 0.86, 0.96), (0.16, 0.24, 0.44), (0.52, 0.44, 0.30), (0.14, 0.22, 0.42)),
    "green":  ((0.84, 0.92, 0.84), (0.18, 0.32, 0.20), (0.52, 0.46, 0.28), (0.16, 0.30, 0.16)),
    "amber":  ((0.96, 0.90, 0.80), (0.36, 0.26, 0.16), (0.58, 0.42, 0.22), (0.30, 0.22, 0.10)),
    "violet": ((0.88, 0.84, 0.96), (0.28, 0.20, 0.40), (0.52, 0.40, 0.40), (0.26, 0.16, 0.38)),
    "teal":   ((0.82, 0.92, 0.92), (0.18, 0.32, 0.34), (0.46, 0.46, 0.42), (0.14, 0.30, 0.32)),
}
# Ordered so seed 64 (the canonical showcase/identity seed) lands on "dusk":
# 64 % 6 == 4. Arena seed 1337 % 6 == 5 -> teal.
_HUE_ORDER = ("blue", "green", "amber", "violet", "dusk", "teal")

# Dome of fill suns (trick #1, faux-GI): the painted sun is one hard KEY; lun3dm5
# reads soft because light arrives from a whole sky DOME. We approximate that by
# adding a few dim directional fills from the other hemisphere bearings, so walls
# facing away from the key still catch soft hue-tinted light instead of going
# flat. Bake-time only (a handful of dot products per face) — zero runtime cost.
# Kept at LOW elevation (z~0.34) and aimed sideways/backward: the dome's job is
# to light VERTICAL walls the single +x key misses, NOT to re-brighten floors
# (the hemispheric sky ambient already does that). A straight-overhead fill just
# blows out floor readability at MACH, so there isn't one.
_DOME_FILL_DIRS = (
    (-0.9855, 0.0,     0.1700),   # behind  (opposite the key), near-horizontal
    ( 0.0,    0.9855,  0.1700),   # +y      flank, near-horizontal
    ( 0.0,   -0.9855,  0.1700),   # -y      flank, near-horizontal
)


# Concrete (lun3dm5) theme: a single bright DAYLIGHT dome, sun aimed to match the
# photo skybox (skybox_from_photo.py reported the sun front-left, high). Elevation
# is pulled down from the detected ~89deg to ~52deg so the cubes get real
# directional shading (a near-zenith sun flattens every wall). Near-white sky
# ambient keeps the pale concrete bright and airy; cool dim fills light the
# shadow-side walls so the greeble reads in soft sky bounce, not black.
_DAY_SUN_DIR  = (0.4917, 0.3706, 0.7880)   # az +37 (front-left), el ~52
_DAY_SKY_AMB  = (0.95, 0.97, 1.02)         # bright near-white skylight (floors)
_DAY_GND_AMB  = (0.50, 0.50, 0.56)         # daylight bounce (undersides not black)
_DAY_KEY_RGB  = (0.50, 0.49, 0.45)         # warm-white sun key
_DAY_FILL_RGB = (0.16, 0.18, 0.24)         # cool sky fill on shadow walls


def build_light_model(seed):
    """Build the vertex-light dome for the active theme.

    Concrete theme -> a fixed bright daylight dome aimed at the photo sky's sun.
    Default theme -> the per-seed hue family (Q3-map-study trick #1/#2)."""
    if cfg.THEME == "concrete":
        suns = [(_DAY_SUN_DIR, _DAY_KEY_RGB)]
        for d in _DOME_FILL_DIRS:
            suns.append((d, _DAY_FILL_RGB))
        return LightModel("concrete-day", _DAY_SKY_AMB, _DAY_GND_AMB, suns)
    fam = _HUE_ORDER[seed % len(_HUE_ORDER)]
    sky, gnd, key, fill = _HUE_FAMILIES[fam]
    suns = [(_SUN_DIR, key)]
    for d in _DOME_FILL_DIRS:
        suns.append((d, fill))
    return LightModel(fam, sky, gnd, suns)


class LightModel:
    """Vertex-light bake: hemispheric ambient + a dome of directional suns."""
    def __init__(self, name, sky_amb, gnd_amb, suns):
        self.name = name
        self.sky = sky_amb
        self.gnd = gnd_amb
        self.suns = suns          # list of (dir, rgb)

    def face_light(self, n):
        """Per-channel RGB light multiplier for a face with world normal n."""
        t = 0.5 * (n[2] + 1.0)            # 0 straight down .. 1 straight up
        out = [self.gnd[i] + (self.sky[i] - self.gnd[i]) * t for i in range(3)]
        for d, rgb in self.suns:
            ndl = n[0] * d[0] + n[1] * d[1] + n[2] * d[2]
            if ndl > 0.0:
                for i in range(3):
                    out[i] += rgb[i] * ndl
        return out


# Default (seed-independent) model — preserves the original single-key dusk look
# for any caller that still uses the free function (e.g. tooling / tests).
_DEFAULT_LIGHT = LightModel("dusk-legacy", _SKY_AMB, _GND_AMB, [(_SUN_DIR, _KEY_RGB)])


def _face_light(n):
    return _DEFAULT_LIGHT.face_light(n)


def _face_st(p, n):
    ax = max(range(3), key=lambda i: abs(n[i]))
    u, v = [i for i in range(3) if i != ax]
    return (p[u] / 64.0, p[v] / 64.0)


class BspWriter:
    def __init__(self, course):
        self.c = course
        self.theme = cfg.THEME          # art theme: remaps bulk dev faces (concrete)
        self.light = build_light_model(getattr(course, "seed", 64))
        self.shaders = _Store()
        self.planes = _Store()
        self.brushes = []       # packed dbrush_t
        self.brushsides = []    # packed dbrushside_t
        self.verts = []
        self.indexes = []
        self.surfaces = []
        self.surf_bounds = []   # bounds per surface, for leaf assignment
        self.brush_surfs = []   # surface ids per world brush
        self.nodes = []
        self.leafs = []
        self.leafbrushes = []
        self.leafsurfaces = []
        self.models = []
        self.has_fog = False        # set in write() when a fog volume is added
        self.fog_brush_index = None # brush index of the global fog volume
        self.fogs = []              # packed dfog_t

    def shader_id(self, name, sflags, cflags):
        return self.shaders.add(
            (name, sflags, cflags),
            struct.pack("<64s2i", name.encode(), sflags, cflags))

    def plane_id(self, normal, dist):
        key = (round(normal[0], 5), round(normal[1], 5),
               round(normal[2], 5), round(dist, 3))
        return self.planes.add(key, struct.pack("<4f", *normal, dist))

    def emit_brush(self, brush):
        first_side = len(self.brushsides)
        is_trigger = brush.contents == CONTENTS_TRIGGER
        # Two independent axes here, NOT one. COLLISION: triggers, fog and decor
        # are all non-solid (their contents bit is in no movement/trace mask, so
        # players pass through). RENDER: triggers and fog are also non-drawing
        # (nodraw), but DECOR draws — an additive god-ray / plasma / beam decal is
        # a non-colliding brush whose faces are meant to be seen.
        is_nonsolid = brush.contents in (CONTENTS_TRIGGER, CONTENTS_FOG,
                                         CONTENTS_DECOR)
        is_nodraw = brush.contents in (CONTENTS_TRIGGER, CONTENTS_FOG)
        cflags = brush.contents if is_nonsolid else CONTENTS_SOLID
        m_tex, m_pal = theme_remap(brush.faces[0].tex, brush.faces[0].palette,
                                   self.theme)
        main_sid = self.shader_id(
            _glow_tex(m_tex, m_pal),
            SURF_NODRAW if is_nodraw else 0, cflags)
        faces = []
        for f in brush.faces:
            sflags = SURF_NODRAW if (not f.draw or is_nodraw) else 0
            if f.tex == TEX_SKY:
                sflags |= SURF_SKY | SURF_NOIMPACT
            f_tex, f_pal = theme_remap(f.tex, f.palette, self.theme)
            faces.append((f, self.shader_id(_glow_tex(f_tex, f_pal),
                                            sflags, cflags),
                          self.plane_id(f.normal, f.dist)))
        face_sid_by_pid = {pid: sid for _, sid, pid in faces}
        # CM_BoundBrush derives brush bounds from sides 0-5 and assumes
        # they are the axial planes in -x +x -y +y -z +z order (q3map
        # emits these "bevels" first on every brush). They also give
        # box traces correct clipping against angled faces.
        bevel_pids = []
        for axis in range(3):
            for sign, dist in ((-1.0, -brush.mins[axis]),
                               (1.0, brush.maxs[axis])):
                normal = tuple(sign if i == axis else 0.0 for i in range(3))
                pid = self.plane_id(normal, dist)
                bevel_pids.append(pid)
                self.brushsides.append(struct.pack(
                    "<2i", pid, face_sid_by_pid.get(pid, main_sid)))
        num_sides = 6
        surf_ids = []
        for f, sid, pid in faces:
            if pid not in bevel_pids:    # bevels already cover axial faces
                self.brushsides.append(struct.pack("<2i", pid, sid))
                num_sides += 1
            if f.draw and not is_nodraw:
                surf_ids.append(self.emit_face(f, sid))
        self.brushes.append(struct.pack(
            "<3i", first_side, num_sides, main_sid))
        return surf_ids

    def emit_face(self, f, shader_id):
        poly = list(reversed(f.poly))  # engine winding: CW from outside
        first_vert = len(self.verts)
        lt = self.light.face_light(f.normal)
        _, pal = theme_remap(f.tex, f.palette, self.theme)
        color = tuple(min(255, int(c * lt[i])) for i, c in enumerate(pal)) + (255,)
        for p in poly:
            s, t = _face_st(p, f.normal)
            self.verts.append(struct.pack(
                "<10f4B", p[0], p[1], p[2], s, t, 0.0, 0.0,
                f.normal[0], f.normal[1], f.normal[2], *color))
        first_index = len(self.indexes)
        for i in range(1, len(poly) - 1):
            self.indexes += [0, i, i + 1]
        num_idx = 3 * (len(poly) - 2)
        center = tuple(sum(p[i] for p in poly) / len(poly) for i in range(3))
        sid = len(self.surfaces)
        # fogNum: tag every non-sky surface to global fog 0 (-> fogIndex 1) when
        # the map carries a fog volume; sky AND additive decals (god-rays, beams,
        # plasma — see NOFOG_TEX) stay unfogged (-1) so distance fog doesn't smear
        # their self-illuminated glow into a grey haze.
        fog_num = (0 if (self.has_fog and f.tex != TEX_SKY
                         and f.tex not in NOFOG_TEX) else -1)
        self.surfaces.append(struct.pack(
            "<12i12f2i",
            shader_id, fog_num, MST_PLANAR,
            first_vert, len(poly), first_index, num_idx,
            LIGHTMAP_BY_VERTEX, 0, 0, 0, 0,
            center[0], center[1], center[2],
            0, 0, 0, 0, 0, 0,
            f.normal[0], f.normal[1], f.normal[2],
            0, 0))
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        zs = [p[2] for p in poly]
        self.surf_bounds.append(((min(xs), min(ys), min(zs)),
                                 (max(xs), max(ys), max(zs))))
        return sid

    # ---- KD tree over world brushes ------------------------------------
    LEAF_MAX = 8
    MAX_DEPTH = 18

    def build_tree(self, world_brushes, region):
        items = [(b.mins, b.maxs, i) for i, b in enumerate(world_brushes)]
        root = self._split(items, region, 0)
        if root < 0:  # degenerate: wrap the lone leaf in one node
            pid = self.plane_id((0.0, 0.0, 1.0), region[0][2])
            self.nodes.append(self._pack_node(pid, root, root, region))

    def _pack_node(self, pid, c0, c1, region):
        return struct.pack(
            "<9i", pid, c0, c1,
            *[int(math.floor(v)) for v in region[0]],
            *[int(math.ceil(v)) for v in region[1]])

    def _make_leaf(self, items, region):
        leaf_id = len(self.leafs)
        first_lb = len(self.leafbrushes)
        first_ls = len(self.leafsurfaces)
        for _, _, bid in items:
            self.leafbrushes.append(bid)
            self.leafsurfaces.extend(self.brush_surfs[bid])
        self.leafs.append(struct.pack(
            "<12i", leaf_id, 0,
            *[int(math.floor(v)) for v in region[0]],
            *[int(math.ceil(v)) for v in region[1]],
            first_ls, len(self.leafsurfaces) - first_ls,
            first_lb, len(self.leafbrushes) - first_lb))
        return -(leaf_id + 1)

    def _split(self, items, region, depth):
        if len(items) <= self.LEAF_MAX or depth >= self.MAX_DEPTH:
            return self._make_leaf(items, region)
        mins, maxs = region
        axis = max(range(3), key=lambda i: maxs[i] - mins[i])
        centers = sorted((it[0][axis] + it[1][axis]) / 2 for it in items)
        split = centers[len(centers) // 2]
        if split <= mins[axis] + 1 or split >= maxs[axis] - 1:
            split = (mins[axis] + maxs[axis]) / 2
        front = [it for it in items if it[1][axis] > split]
        back = [it for it in items if it[0][axis] < split]
        if len(front) == len(items) and len(back) == len(items):
            return self._make_leaf(items, region)
        normal = tuple(1.0 if i == axis else 0.0 for i in range(3))
        pid = self.plane_id(normal, split)
        node_id = len(self.nodes)
        self.nodes.append(None)  # reserve preorder slot
        fr = (tuple(split if i == axis else mins[i] for i in range(3)), maxs)
        br = (mins, tuple(split if i == axis else maxs[i] for i in range(3)))
        c0 = self._split(front, fr, depth + 1)
        c1 = self._split(back, br, depth + 1)
        self.nodes[node_id] = self._pack_node(pid, c0, c1, region)
        return node_id

    # ---- lightgrid (must match R_LoadLightGrid's size formula) ---------
    @staticmethod
    def lightgrid(world_mins, world_maxs):
        size = (64.0, 64.0, 128.0)
        bounds = []
        for i in range(3):
            o = size[i] * math.ceil(world_mins[i] / size[i])
            mx = size[i] * math.floor(world_maxs[i] / size[i])
            bounds.append(int((mx - o) / size[i]) + 1)
        n = bounds[0] * bounds[1] * bounds[2]
        if n <= 0 or n * 8 > 0x800000:
            return b""
        # one uniform cell repeated over the grid — this lights the DYNAMIC
        # entities (players, weapons, items, gibs) that the vertex-lit world
        # can't. Tuned to the same dusk mood as the world vertex lighting:
        #   bytes 0-2 ambient  -> cool blue-violet skylight fill
        #   bytes 3-5 directed -> a warm sun key
        #   byte 6 lng, byte 7 lat (tr_light.c: z=cos(lng), x=cos(lat)*sin(lng))
        #     lng=60 (~84deg from zenith, low) + lat=0 (+x) aims the key at the
        #     visible sky sun, so models get a warm sunward rim and a cool
        #     shadow side that matches the surrounding geometry.
        return bytes((92, 98, 124, 188, 150, 104, 60, 0)) * n

    # ---- assembly -------------------------------------------------------
    def write(self, path):
        c = self.c
        # wrap the whole play volume in one global fog brush sized to the world
        # bounds, so every non-sky surface (tagged fogNum 0 in emit_face) fades
        # with distance toward the synthwave horizon. Non-solid: movement and
        # collision ignore it. Added as a world brush so it lives in the world
        # model + tree like any other.
        if c.solids:
            fxs = [b.mins[0] for b in c.solids] + [b.maxs[0] for b in c.solids]
            fys = [b.mins[1] for b in c.solids] + [b.maxs[1] for b in c.solids]
            fzs = [b.mins[2] for b in c.solids] + [b.maxs[2] for b in c.solids]
            fog = make_box((min(fxs), min(fys), min(fzs)),
                           (max(fxs), max(fys), max(fzs)),
                           tex=theme_fog(self.theme), contents=CONTENTS_FOG,
                           draw=set())
            c.solids.append(fog)
            self.has_fog = True
        for b in c.solids:
            if b.contents == CONTENTS_FOG:
                self.fog_brush_index = len(self.brushes)
            self.brush_surfs.append(self.emit_brush(b))
        n_world = len(c.solids)
        n_world_surfs = len(self.surfaces)

        wm = tuple(min(b.mins[i] for b in c.solids) for i in range(3))
        wx = tuple(max(b.maxs[i] for b in c.solids) for i in range(3))
        self.build_tree(c.solids, (wm, wx))

        self.models.append(struct.pack(
            "<6f4i", *wm, *wx, 0, n_world_surfs, 0, n_world))
        for ti, (tb, ent) in enumerate(c.triggers):
            first = len(self.brushes)
            self.emit_brush(tb)
            self.models.append(struct.pack(
                "<6f4i", *tb.mins, *tb.maxs, n_world_surfs, 0, first, 1))
            ent["model"] = f"*{ti + 1}"
        # drawn movers (func_bobbing): unlike triggers these carry real
        # surfaces, so the inline model renders. Their faces land contiguously
        # past the world surfaces and are referenced by the model's surf range
        # (they're not in any leaf — inline bmodels draw per-entity, not by vis)
        movers = getattr(c, "movers", [])
        n_trig = len(c.triggers)
        for mi, (mb, ent) in enumerate(movers):
            first = len(self.brushes)
            first_surf = len(self.surfaces)
            surf_ids = self.emit_brush(mb)
            self.models.append(struct.pack(
                "<6f4i", *mb.mins, *mb.maxs, first_surf, len(surf_ids), first, 1))
            ent["model"] = f"*{n_trig + mi + 1}"

        ent_text = ""
        for e in (c.entities + [ent for _, ent in c.triggers]
                  + [ent for _, ent in movers]):
            ent_text += "{\n"
            for k, v in e.items():
                ent_text += f'"{k}" "{v}"\n'
            ent_text += "}\n"

        lumps = [b""] * HEADER_LUMPS
        lumps[LUMP_ENTITIES] = ent_text.encode() + b"\x00"
        lumps[LUMP_SHADERS] = b"".join(self.shaders.items)
        lumps[LUMP_PLANES] = b"".join(self.planes.items)
        lumps[LUMP_NODES] = b"".join(self.nodes)
        lumps[LUMP_LEAFS] = b"".join(self.leafs)
        lumps[LUMP_LEAFSURFACES] = struct.pack(
            f"<{len(self.leafsurfaces)}i", *self.leafsurfaces)
        lumps[LUMP_LEAFBRUSHES] = struct.pack(
            f"<{len(self.leafbrushes)}i", *self.leafbrushes)
        lumps[LUMP_MODELS] = b"".join(self.models)
        lumps[LUMP_BRUSHES] = b"".join(self.brushes)
        lumps[LUMP_BRUSHSIDES] = b"".join(self.brushsides)
        lumps[LUMP_DRAWVERTS] = b"".join(self.verts)
        lumps[LUMP_DRAWINDEXES] = struct.pack(
            f"<{len(self.indexes)}i", *self.indexes)
        if self.fog_brush_index is not None:
            lumps[LUMP_FOGS] = struct.pack(
                "<64s2i", theme_fog(self.theme).encode(),
                self.fog_brush_index, -1)
        else:
            lumps[LUMP_FOGS] = b""
        lumps[LUMP_SURFACES] = b"".join(self.surfaces)
        lumps[LUMP_LIGHTMAPS] = b""      # vertex-lit
        lumps[LUMP_LIGHTGRID] = self.lightgrid(wm, wx)
        lumps[LUMP_VISIBILITY] = b""     # no vis -> everything visible

        header_size = 8 + HEADER_LUMPS * 8
        offs = []
        pos = header_size
        body = b""
        for l in lumps:
            offs.append((pos, len(l)))
            pad = (4 - len(l) % 4) % 4
            body += l + b"\x00" * pad
            pos += len(l) + pad
        with open(path, "wb") as fh:
            fh.write(BSP_IDENT + struct.pack("<i", BSP_VERSION))
            for o, ln in offs:
                fh.write(struct.pack("<2i", o, ln))
            fh.write(body)
        return {
            "brushes": len(self.brushes), "surfaces": len(self.surfaces),
            "verts": len(self.verts), "nodes": len(self.nodes),
            "leafs": len(self.leafs), "planes": len(self.planes.items),
            "models": len(self.models), "bytes": header_size + len(body),
        }


# ======================================================================
# .map writer (optional, for tweaking a course in Radiant)
# ======================================================================
def write_map(course, path):
    def brush_text(b):
        out = "{\n"
        for f in b.faces:
            p0, p1, p2 = f.poly[0], f.poly[1], f.poly[2]
            # q3map: normal = (p0-p1) x (p2-p1); orient to face outward
            n = vcross(vsub(p0, p1), vsub(p2, p1))
            if vdot(n, f.normal) < 0:
                p0, p2 = p2, p0
            tex = f.tex[len("textures/"):]
            pts = " ".join(
                f"( {q[0]:g} {q[1]:g} {q[2]:g} )" for q in (p0, p1, p2))
            out += f"{pts} {tex} 0 0 0 0.5 0.5 0 0 0\n"
        return out + "}\n"

    with open(path, "w") as fh:
        fh.write("{\n")
        for k, v in course.entities[0].items():
            fh.write(f'"{k}" "{v}"\n')
        for b in course.solids:
            fh.write(brush_text(b))
        fh.write("}\n")
        for e in course.entities[1:]:
            fh.write("{\n")
            for k, v in e.items():
                fh.write(f'"{k}" "{v}"\n')
            fh.write("}\n")
        for tb, ent in course.triggers:
            fh.write("{\n")
            for k, v in ent.items():
                if k != "model":
                    fh.write(f'"{k}" "{v}"\n')
            fh.write(brush_text(tb))
            fh.write("}\n")
        for mb, ent in getattr(course, "movers", []):
            fh.write("{\n")
            for k, v in ent.items():
                if k != "model":
                    fh.write(f'"{k}" "{v}"\n')
            fh.write(brush_text(mb))
            fh.write("}\n")


# ======================================================================
# validator
# ======================================================================
def check_bsp(path):
    data = open(path, "rb").read()
    assert data[:4] == BSP_IDENT, "bad ident"
    assert struct.unpack_from("<i", data, 4)[0] == BSP_VERSION, "bad version"
    lumps = [struct.unpack_from("<2i", data, 8 + i * 8)
             for i in range(HEADER_LUMPS)]

    def lump(i):
        o, ln = lumps[i]
        assert 0 <= o and o + ln <= len(data), f"lump {i} out of file"
        return data[o:o + ln]

    def count(i, size):
        ln = lumps[i][1]
        assert ln % size == 0, f"lump {i}: funny size {ln} % {size}"
        return ln // size

    n_shaders = count(LUMP_SHADERS, SZ_SHADER)
    n_planes = count(LUMP_PLANES, SZ_PLANE)
    n_nodes = count(LUMP_NODES, SZ_NODE)
    n_leafs = count(LUMP_LEAFS, SZ_LEAF)
    n_ls = count(LUMP_LEAFSURFACES, 4)
    n_lb = count(LUMP_LEAFBRUSHES, 4)
    n_models = count(LUMP_MODELS, SZ_MODEL)
    n_brushes = count(LUMP_BRUSHES, SZ_BRUSH)
    n_sides = count(LUMP_BRUSHSIDES, SZ_BRUSHSIDE)
    n_verts = count(LUMP_DRAWVERTS, SZ_DRAWVERT)
    n_idx = count(LUMP_DRAWINDEXES, 4)
    n_surfs = count(LUMP_SURFACES, SZ_SURFACE)
    for name, n in (("shaders", n_shaders), ("planes", n_planes),
                    ("nodes", n_nodes), ("leafs", n_leafs),
                    ("models", n_models)):
        assert n >= 1, f"map with no {name}"
    assert lumps[LUMP_VISIBILITY][1] == 0
    assert lumps[LUMP_LIGHTMAPS][1] == 0

    sides = lump(LUMP_BRUSHSIDES)
    for i in range(n_sides):
        p, s = struct.unpack_from("<2i", sides, i * SZ_BRUSHSIDE)
        assert 0 <= p < n_planes and 0 <= s < n_shaders, f"side {i}"
    brushes = lump(LUMP_BRUSHES)
    for i in range(n_brushes):
        fs, ns, sh = struct.unpack_from("<3i", brushes, i * SZ_BRUSH)
        assert 0 <= fs and fs + ns <= n_sides and 0 <= sh < n_shaders, \
            f"brush {i}"
    ls = struct.unpack(f"<{n_ls}i", lump(LUMP_LEAFSURFACES))
    lb = struct.unpack(f"<{n_lb}i", lump(LUMP_LEAFBRUSHES))
    assert all(0 <= v < n_surfs for v in ls), "leafsurface out of range"
    assert all(0 <= v < n_brushes for v in lb), "leafbrush out of range"
    leafs = lump(LUMP_LEAFS)
    for i in range(n_leafs):
        v = struct.unpack_from("<12i", leafs, i * SZ_LEAF)
        assert v[0] >= 0, f"leaf {i}: solid cluster in playable space"
        assert v[8] + v[9] <= n_ls and v[10] + v[11] <= n_lb, f"leaf {i}"
    nodes = lump(LUMP_NODES)
    seen = set()

    def walk(idx, depth=0):
        assert depth < 64, "tree cycle?"
        if idx < 0:
            li = -1 - idx
            assert 0 <= li < n_leafs, f"bad leaf ref {idx}"
            seen.add(li)
            return
        assert idx < n_nodes, f"bad node ref {idx}"
        v = struct.unpack_from("<9i", nodes, idx * SZ_NODE)
        assert 0 <= v[0] < n_planes
        walk(v[1], depth + 1)
        walk(v[2], depth + 1)

    walk(0)
    assert seen == set(range(n_leafs)), "unreachable leafs"
    world_brushes = None
    models = lump(LUMP_MODELS)
    for i in range(n_models):
        v = struct.unpack_from("<6f4i", models, i * SZ_MODEL)
        assert v[6] + v[7] <= n_surfs and v[8] + v[9] <= n_brushes, \
            f"model {i}"
        if i == 0:
            world_brushes = v[9]
    assert set(lb) == set(range(world_brushes)), \
        "world brushes not fully covered by leafs"
    # fogs: 0 (none) or 1 global fog whose brush exists and has >=6 axial sides
    # (R_LoadFogs reads sides 0-5 for the volume bounds, errors otherwise)
    n_fogs = count(LUMP_FOGS, SZ_FOGS)
    assert n_fogs in (0, 1), f"unexpected fog count {n_fogs}"
    if n_fogs:
        fshader, fbrush, fside = struct.unpack_from("<64s2i", lump(LUMP_FOGS), 0)
        assert 0 <= fbrush < n_brushes, "fog brushNum out of range"
        fs, ns, sh = struct.unpack_from("<3i", brushes, fbrush * SZ_BRUSH)
        assert ns >= 6, "fog brush needs >=6 axial sides"
    surfs = lump(LUMP_SURFACES)
    for i in range(n_surfs):
        v = struct.unpack_from("<12i", surfs, i * SZ_SURFACE)
        assert v[2] == MST_PLANAR
        # fogNum is -1 (unfogged: sky) or 0 (the single global fog)
        assert v[1] == -1 or (v[1] == 0 and n_fogs == 1), f"surf {i} fogNum {v[1]}"
        assert v[7] == LIGHTMAP_BY_VERTEX
        assert 0 <= v[3] and v[3] + v[4] <= n_verts, f"surf {i} verts"
        assert 0 <= v[5] and v[5] + v[6] <= n_idx and v[6] % 3 == 0
        assert 0 <= v[0] < n_shaders
    idx = struct.unpack(f"<{n_idx}i", lump(LUMP_DRAWINDEXES))
    off = 0
    for i in range(n_surfs):
        v = struct.unpack_from("<12i", surfs, i * SZ_SURFACE)
        for j in range(v[5], v[5] + v[6]):
            assert 0 <= idx[j] < v[4], f"surf {i} index out of poly"
    # lightgrid size must satisfy R_LoadLightGrid or be empty
    v0 = struct.unpack_from("<6f4i", models, 0)
    expect = BspWriter.lightgrid(v0[0:3], v0[3:6])
    assert lumps[LUMP_LIGHTGRID][1] in (0, len(expect)), "lightgrid mismatch"
    ents = lump(LUMP_ENTITIES).rstrip(b"\x00").decode()
    assert ents.count("{") == ents.count("}")
    assert '"classname" "worldspawn"' in ents.split("}")[0]
    for tok in ents.split('"'):
        if tok.startswith("*"):
            assert int(tok[1:]) < n_models, f"entity model {tok} missing"
    assert '"classname" "info_player_deathmatch"' in ents
    return {
        "brushes": n_brushes, "surfaces": n_surfs, "leafs": n_leafs,
        "nodes": n_nodes, "models": n_models, "entities": ents.count("{"),
    }


