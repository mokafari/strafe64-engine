"""strafegen_geom — vector helpers + convex-brush geometry primitives.

Face / Brush and the make_box / make_prism / make_skybox builders every course
emits, plus avoid_footprints() spawn-clearance. Imports palette defaults.
"""
import math
import random

from strafegen_palettes import *
from strafegen_palettes import CONTENTS_SOLID

# ---- face lookup tables ----
FACE_KEYS = ("-x", "+x", "-y", "+y", "-z", "+z")
FACE_NORMALS = {
    "-x": (-1, 0, 0), "+x": (1, 0, 0),
    "-y": (0, -1, 0), "+y": (0, 1, 0),
    "-z": (0, 0, -1), "+z": (0, 0, 1),
}
# corner index bit layout: bit0 = +x, bit1 = +y, bit2 = +z
FACE_CORNERS = {
    "-x": (0, 2, 6, 4), "+x": (1, 3, 7, 5),
    "-y": (0, 1, 5, 4), "+y": (2, 3, 7, 6),
    "-z": (0, 1, 3, 2), "+z": (4, 5, 7, 6),
}



# ======================================================================
# small vector helpers
# ======================================================================
def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def vnorm(a):
    l = math.sqrt(vdot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l)


def newell_normal(poly):
    nx = ny = nz = 0.0
    for i, p in enumerate(poly):
        q = poly[(i + 1) % len(poly)]
        nx += (p[1] - q[1]) * (p[2] + q[2])
        ny += (p[2] - q[2]) * (p[0] + q[0])
        nz += (p[0] - q[0]) * (p[1] + q[1])
    return vnorm((nx, ny, nz))



# ======================================================================
# geometry: convex brushes built from explicit face polygons
# ======================================================================
class Face:
    """One face of a convex brush.

    poly is stored counter-clockwise as seen from outside (right-hand
    normal = outward). The BSP writer reverses it: the renderer culls
    GL_FRONT for front-sided shaders, so visible windings are clockwise
    viewed from the front (tr_backend.c GL_Cull).
    """

    def __init__(self, poly, tex, palette, draw=True):
        n = newell_normal(poly)
        self.poly = poly
        self.normal = n
        self.tex = tex
        self.palette = palette
        self.draw = draw
        self.dist = vdot(n, poly[0])


class Brush:
    def __init__(self, faces, contents=CONTENTS_SOLID):
        self.faces = faces
        self.contents = contents
        pts = [p for f in faces for p in f.poly]
        # convexity / outward-plane sanity: every brush point must lie
        # behind every face plane, or the engine traces through the gap
        for f in faces:
            assert all(vdot(f.normal, p) <= f.dist + 0.5 for p in pts), \
                "brush face plane points inward (or brush not convex)"
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        zs = [p[2] for p in pts]
        self.mins = (min(xs), min(ys), min(zs))
        self.maxs = (max(xs), max(ys), max(zs))


def _oriented(corner_ids, verts, outward):
    poly = [verts[i] for i in corner_ids]
    if vdot(newell_normal(poly), outward) < 0:
        poly.reverse()
    return poly


def make_box(mins, maxs, tex=TEX_FLOOR, palette=PAL_PLAIN,
             contents=CONTENTS_SOLID, draw=None, face_tex=None,
             top_drop=None):
    """Convex brush from an axis box.

    draw: set of face keys to render (None = all).
    face_tex: per-face texture overrides, e.g. {"+z": TEX_SKY}.
    top_drop: {corner_id: drop} lowers chosen top corners — used for ramps.
    """
    verts = []
    for i in range(8):
        x = maxs[0] if i & 1 else mins[0]
        y = maxs[1] if i & 2 else mins[1]
        z = maxs[2] if i & 4 else mins[2]
        verts.append((x, y, z))
    if top_drop:
        for cid, drop in top_drop.items():
            x, y, z = verts[cid]
            verts[cid] = (x, y, z - drop)
    faces = []
    for key in FACE_KEYS:
        poly = _oriented(FACE_CORNERS[key], verts, FACE_NORMALS[key])
        t = (face_tex or {}).get(key, tex)
        visible = (draw is None or key in draw) and t != TEX_CAULK
        faces.append(Face(poly, t, palette, draw=visible))
    return Brush(faces, contents)


def make_prism(foot, z_bottom, top_zs, tex=TEX_FLOOR, palette=PAL_PLAIN,
               contents=CONTENTS_SOLID):
    """Convex prism from a CCW (seen from above) footprint polygon.

    top_zs gives a per-vertex top height — banked tops are allowed as
    long as they stay planar (velodrome segments are, by symmetry).
    Side faces are always planar: each contains two vertical lines.
    """
    n = len(foot)
    # normalize the footprint to CCW: the side-normal formula below is
    # (dy, -dx), which faces outward only for CCW polygons — a CW
    # footprint would build inward planes and the engine would trace
    # straight through the brush
    area2 = sum(foot[i][0] * foot[(i + 1) % n][1]
                - foot[(i + 1) % n][0] * foot[i][1] for i in range(n))
    if area2 < 0:
        foot = list(reversed(foot))
        top_zs = list(reversed(top_zs))
    top = [(foot[i][0], foot[i][1], top_zs[i]) for i in range(n)]
    bot = [(foot[i][0], foot[i][1], z_bottom) for i in range(n)]

    def oriented(poly, want):
        if vdot(newell_normal(poly), want) < 0:
            return list(reversed(poly))
        return poly

    faces = [Face(oriented(top, (0, 0, 1)), tex, palette)]
    assert all(abs(vdot(faces[0].normal, p) - faces[0].dist) < 0.5
               for p in top), "non-planar prism top"
    faces.append(Face(oriented(bot, (0, 0, -1)), tex, palette))
    for i in range(n):
        j = (i + 1) % n
        out = (foot[j][1] - foot[i][1], foot[i][0] - foot[j][0], 0.0)
        faces.append(Face(
            oriented([bot[i], bot[j], top[j], top[i]], out), tex, palette))
    return Brush(faces, contents)


def make_skybox(x0, y0, z0, x1, y1, z1):
    """Six slabs enclosing the given volume; only inner faces draw, as sky."""
    t = 64.0
    slabs = (
        ((x0 - t, y0 - t, z0 - t), (x1 + t, y1 + t, z0), "+z"),
        ((x0 - t, y0 - t, z1), (x1 + t, y1 + t, z1 + t), "-z"),
        ((x0 - t, y0 - t, z0), (x0, y1 + t, z1), "+x"),
        ((x1, y0 - t, z0), (x1 + t, y1 + t, z1), "-x"),
        ((x0, y0 - t, z0), (x1, y0, z1), "+y"),
        ((x0, y1, z0), (x1, y1 + t, z1), "-y"),
    )
    return [make_box(mins, maxs, tex=TEX_SKY, palette=PAL_PLAIN, draw={inner},
                     face_tex={k: (TEX_SKY if k == inner else TEX_CAULK)
                               for k in FACE_KEYS})
            for mins, maxs, inner in slabs]




# ======================================================================
# greeble: lun3dm5-style concrete cube erosion
# ======================================================================
def _is_axis_box(b):
    """True for a 6-face axis-aligned box (make_box output). Banked ramps/prisms
    have slanted faces and are skipped — their AABB sides don't match the real
    surface, so cubes attached to the AABB would float off."""
    if len(b.faces) != 6:
        return False
    return all(max(abs(c) for c in f.normal) > 0.999 for f in b.faces)


# the small-cube vocabulary mined from lun3dm5's brushwork (decompiled): edge
# lengths cluster hard at 8 / 16 / 24 / 32 on an 8-unit grid — those are the
# trim/detail fragments that erode its big concrete masses into the cube look.
# Biased SMALL (8/16 dominate) so the articulation reads as fine detail, not a
# few coarse chunks. Larger 32/48 turn up only as the occasional beam/step.
_GREEBLE_FINE  = (8, 8, 16, 16, 16, 24, 24, 32)
_GREEBLE_BEAM  = (24, 32, 32, 48)


def greeble_course(course, seed=0, density=1.0, cap=6000):
    """Erode large concrete masses into DENSE clustered cubes, lun3dm5-style.

    Articulates sizable axis-box brushes with three gameplay-safe cube passes,
    all kept OUTSIDE the footprint / BELOW the top surface so the run line is
    untouched:

      * crenellated edge bands — a near-continuous run of small (8-24u) cubes of
        irregular height/depth/inset along each top edge (the fine "teeth" that
        make the silhouette read as eroded concrete, not a smooth slab);
      * corner pylons — a stepped stack of shrinking cubes hung off each corner,
        the strong vertical accents that anchor the brutalist look;
      * underside relief — blocky cubes scattered under wide platforms so the
        floating slabs read as chunky masses from below.

    Cubes carry the concrete material + shade-varied greys so the sun-dome
    differentiates each face. Appends Brushes to ``course.solids`` and returns
    the count added (capped). No-op at density 0; pairs with --theme concrete.
    """
    if density <= 0:
        return 0
    rng = random.Random((int(seed) & 0xffffffff) ^ 0xC5EB1E)
    pals = (PAL_CRETE, PAL_CRETE, PAL_CRETE_DARK, PAL_CRETE_TRIM)
    sides = ((0, -1, 1), (0, +1, 1), (1, -1, 0), (1, +1, 0))
    added = []

    def cube(cmn, cmx):
        if (cmx[0] - cmn[0] < 4 or cmx[1] - cmn[1] < 4 or cmx[2] - cmn[2] < 4):
            return
        added.append(make_box(tuple(cmn), tuple(cmx), tex=TEX_CONCRETE,
                              palette=rng.choice(pals)))

    def box(out_ax, run_ax, olo, ohi, rlo, rhi, zb, zt):
        cmn = [0.0, 0.0, zb]
        cmx = [0.0, 0.0, zt]
        cmn[out_ax], cmx[out_ax] = olo, ohi
        cmn[run_ax], cmx[run_ax] = rlo, rhi
        cube(cmn, cmx)

    for b in list(course.solids):
        if len(added) >= cap:
            break
        if getattr(b, "contents", CONTENTS_SOLID) != CONTENTS_SOLID:
            continue
        if not _is_axis_box(b):
            continue
        mn, mx = b.mins, b.maxs
        dims = [mx[i] - mn[i] for i in range(3)]
        if min(dims[0], dims[1]) < 96 or max(dims) > 4096:
            continue
        top = mx[2]

        # ---- 1. crenellated edge bands -----------------------------------
        for out_ax, sgn, run_ax in sides:
            run_lo, run_hi = mn[run_ax], mx[run_ax]
            face = mn[out_ax] if sgn < 0 else mx[out_ax]
            pos = run_lo + rng.uniform(0, 12)
            guard = 0
            while pos < run_hi - 4 and len(added) < cap and guard < 4000:
                guard += 1
                c = float(rng.choice(_GREEBLE_FINE))
                w = c                                   # tooth footprint along edge
                rlo, rhi = pos, min(run_hi, pos + w)
                # ~70% of teeth are present at density 1 (gaps = the erosion)
                if rng.random() < 0.70 * min(1.0, density):
                    inset = c * 0.4                     # bite back into the mass
                    d = min(28.0, c * rng.uniform(0.5, 1.1))   # outward protrusion
                    o0 = face
                    olo, ohi = ((o0 - d, o0 + inset) if sgn < 0
                                else (o0 - inset, o0 + d))
                    zt = top - rng.choice((16, 24, 32))        # ALWAYS below the rim
                    h = c * rng.uniform(0.6, 2.2)
                    box(out_ax, run_ax, olo, ohi, rlo, rhi, zt - h, zt)
                    # occasional second tooth stepping further out + down
                    if rng.random() < 0.4 * density:
                        d2 = d + c * rng.uniform(0.4, 0.9)
                        olo2, ohi2 = ((o0 - d2, o0 - d + 2) if sgn < 0
                                      else (o0 + d - 2, o0 + d2))
                        box(out_ax, run_ax, olo2, ohi2, rlo, rhi,
                            zt - h - rng.uniform(8, 40), zt - rng.uniform(0, 12))
                pos += w * rng.uniform(0.9, 1.6)        # dense but irregular spacing

        # ---- 2. corner pylons (stepped hanging stacks) -------------------
        corners = ((mn[0], mn[1]), (mx[0], mn[1]), (mn[0], mx[1]), (mx[0], mx[1]))
        for cx, cy in corners:
            if len(added) >= cap or rng.random() > 0.85 * min(1.0, density):
                continue
            sx = -1 if cx == mn[0] else 1
            sy = -1 if cy == mn[1] else 1
            c = float(rng.choice(_GREEBLE_BEAM))
            zt = top - rng.choice((16, 24))             # below the rim, never flush
            ox, oy = cx, cy
            for _step in range(rng.randint(2, 4)):
                if len(added) >= cap:
                    break
                d = min(40.0, c)
                xlo, xhi = (ox - d, ox + c * 0.5) if sx < 0 else (ox - c * 0.5, ox + d)
                ylo, yhi = (oy - d, oy + c * 0.5) if sy < 0 else (oy - c * 0.5, oy + d)
                h = c * rng.uniform(1.0, 2.0)
                cube([xlo, ylo, zt - h], [xhi, yhi, zt])
                zt -= h * rng.uniform(0.5, 0.85)        # next step lower
                ox += sx * d * 0.5                       # and further out
                oy += sy * d * 0.5
                c = max(8.0, c * rng.uniform(0.55, 0.8))  # and smaller

        # ---- 3. underside relief (wide platforms only) -------------------
        if dims[2] <= 160 and dims[0] >= 192 and dims[1] >= 192:
            cell = 64.0
            nx = max(1, int(dims[0] // cell))
            ny = max(1, int(dims[1] // cell))
            bottom = mn[2]
            for ix in range(nx):
                for iy in range(ny):
                    if len(added) >= cap or rng.random() > 0.30 * min(1.0, density):
                        continue
                    px = mn[0] + (ix + 0.5) * dims[0] / nx
                    py = mn[1] + (iy + 0.5) * dims[1] / ny
                    c = float(rng.choice(_GREEBLE_FINE))
                    h = c * rng.uniform(0.5, 1.6)
                    cube([px - c / 2, py - c / 2, bottom - h],
                         [px + c / 2, py + c / 2, bottom + 2])

    course.solids.extend(added)
    return len(added)


def scale_course(course, sx, sy, sz):
    """Uniformly rescale a built course in place by (sx, sy, sz).

    Multiplies every brush vertex, trigger volume and entity origin by the per-
    axis factors and rebuilds each Face/Brush so normals, plane distances and
    bounding boxes stay consistent (the BSP writer auto-sizes the fog + tree to
    the new bounds). Angles are LEFT ALONE — including the >999999 momentum-portal
    marker — and so are non-spatial keys. A no-op (and byte-identical) at 1,1,1.

    NOTE: scaling is internally consistent (spawns, portals and geometry move
    together), so it is ideal for resizing arenas/killboxes. It does NOT retune
    jump-gated course gaps to the fixed player jump, so big vertical scales make
    linear courses harder/easier rather than merely bigger — that is expected."""
    if sx == 1.0 and sy == 1.0 and sz == 1.0:
        return course

    def spt(p):
        return (p[0] * sx, p[1] * sy, p[2] * sz)

    def sbrush(b):
        nf = [Face([spt(p) for p in f.poly], f.tex, f.palette, draw=f.draw)
              for f in b.faces]
        return Brush(nf, b.contents)

    def sorigin(ent):
        if "origin" in ent:
            x, y, z = (float(v) for v in ent["origin"].split())
            ent["origin"] = f"{x * sx:g} {y * sy:g} {z * sz:g}"

    course.solids = [sbrush(b) for b in course.solids]
    course.triggers = [(sbrush(tb), ent) for tb, ent in course.triggers]
    for ent in course.entities:
        sorigin(ent)
    for _tb, ent in course.triggers:
        sorigin(ent)
    if course.entities and "voidbase" in course.entities[0]:
        course.entities[0]["voidbase"] = f"{float(course.entities[0]['voidbase']) * sz:g}"
    return course


def avoid_footprints(x, y, blockers, clearance=40.0, ring_r=None):
    """Nudge a spawn point off any blocker footprint so nobody spawns embedded
    in a pillar/column (which ejects them at huge speed — "stuck in pillar") or
    on a jump pad (which launches them the instant they appear).

    blockers: list of (cx, cy, half) axis-aligned square footprints. The point
    is rotated around the origin (staying on its ring) in small steps until the
    player box clears every blocker; falls back to the original if nothing fits.
    """
    def hits(px, py):
        for (bx, by, bhalf) in blockers:
            if abs(px - bx) < bhalf + clearance and abs(py - by) < bhalf + clearance:
                return True
        return False
    if not hits(x, y):
        return x, y
    r = ring_r if ring_r is not None else math.hypot(x, y)
    a0 = math.atan2(y, x)
    for step in range(1, 73):                       # sweep +/- up to 180 deg
        for sgn in (1, -1):
            a = a0 + sgn * step * math.radians(2.5)
            nx, ny = r * math.cos(a), r * math.sin(a)
            if not hits(nx, ny):
                return nx, ny
    return x, y                                     # no clear angle (shouldn't happen)


# Q3 player bounding box (origin-relative). A spawn is "inside geometry" when
# this box, placed at the spawn origin, overlaps a CONTENTS_SOLID brush.
PLAYER_MINS = (-15.0, -15.0, -24.0)
PLAYER_MAXS = (15.0, 15.0, 32.0)


def validate_spawns(course, margin=1.0):
    """Universal guard: refuse to ship a map with a player spawn embedded in
    solid geometry (the "stuck in pillar" 7000-ups ejection). Scans every
    info_player_deathmatch / info_player_start against all solid brushes and
    raises ValueError if the player box at any spawn overlaps one. Runs on the
    final, scaled geometry for EVERY map kind — a single chokepoint so no
    generator can emit a spawn-in-wall map. Returns the spawn count on success.
    """
    solids = [b for b in course.solids
              if getattr(b, "contents", CONTENTS_SOLID) == CONTENTS_SOLID]
    spawns = [e for e in course.entities
              if e.get("classname") in ("info_player_deathmatch",
                                        "info_player_start", "info_player_team1",
                                        "info_player_team2")]
    bad = []
    for e in spawns:
        ox, oy, oz = (float(v) for v in e["origin"].split())
        pmin = (ox + PLAYER_MINS[0], oy + PLAYER_MINS[1], oz + PLAYER_MINS[2])
        pmax = (ox + PLAYER_MAXS[0], oy + PLAYER_MAXS[1], oz + PLAYER_MAXS[2])
        for b in solids:
            if all(pmin[i] < b.maxs[i] - margin and pmax[i] > b.mins[i] + margin
                   for i in range(3)):
                bad.append((ox, oy, oz, b.mins, b.maxs))
                break
    if bad:
        lines = "\n".join(f"  spawn ({x:g} {y:g} {z:g}) inside brush "
                          f"{mn}..{mx}" for x, y, z, mn, mx in bad)
        raise ValueError(
            f"{len(bad)} player spawn(s) embedded in solid geometry — would "
            f"eject players at huge speed. Fix spawn placement:\n{lines}")
    return len(spawns)


