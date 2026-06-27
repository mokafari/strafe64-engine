"""strafegen_volumetric — sun shafts / god-rays / corona (Layer-1 atmosphere).

The runtime sun (strafegen_gfx.q3gl2_sun) gives a DIRECTION + cast shadows, but
the *air* between the geometry stays empty — no light scattering, no shafts. This
module adds the visible volume of the light: leaning additive "god-ray" slabs
that rake down from the sky-sun bearing, plus an optional sun-corona billboard.

These are DECORATIVE, NON-COLLIDING surfaces (CONTENTS_DECOR) — the BSP writer
draws them but players/traces pass straight through. The look is the classic
idTech3 cheap-volumetric: a soft-edged additive card (`cull none`, sort additive,
depthWrite off) whose texture is bright in the core and fades at the edges, so
overlapping faces brighten the middle and fake light density. No renderer change
— it ships in the map pk3 and reads on the existing GL2 (and even GL1) path.

Public API:
    godray_shaders()  -> str          # shader text (append to the .shader)
    godray_textures() -> {arc: tga}   # procedural component textures
    add_godrays(course, seed=..., ...) # place shafts (+ corona) into a built map

Sun bearing is read from strafegen_gfx (lazy import) so the shafts agree with
where the painted sky-sun and the cast shadows say the sun is.
"""
import math
import random

from strafegen_geom import (Face, Brush, FACE_KEYS, FACE_NORMALS, FACE_CORNERS,
                            _oriented)
from strafegen_palettes import CONTENTS_DECOR
from strafegen_tga import _tga32, _clamp8

TEX_GODSHAFT = "textures/strafe64/godshaft"
TEX_CORONA   = "textures/strafe64/suncorona"

# Visible rake of a shaft = horizontal run per unit of height. The physically
# exact value is 1/tan(elevation) (~1.28 at the 38deg sun), but that is so
# oblique a tall shaft would skate clear across the arena. We lean a readable
# fraction of it so the beams clearly come "from the sun" without lying flat.
SHAFT_LEAN_FRAC = 0.45
# warm sun tint baked into the shaft texture (matches strafegen_gfx.SUN_COLOR)
SHAFT_TINT = (1.00, 0.90, 0.72)


# ======================================================================
# SHADERS
# ======================================================================
GODRAY_SHADERS = """\
// ===================================================================
// strafegen_volumetric — sun shafts + corona (additive, non-colliding)
// Appended by strafegen_volumetric.godray_shaders().
// ===================================================================

// GODSHAFT — a leaning column of light. Additive, two-sided, no depth write, so
// it never occludes geometry and stacked faces brighten the core (fake density).
// The texture is bright in the middle and fades at the edges; a slow upward
// scroll drifts faint motes through the beam, and a gentle sine breathes the
// whole shaft so it reads as living air, not a decal. surfaceparm nonsolid keeps
// it intangible even if a q3map2 bake ever recompiles it.
textures/strafe64/godshaft
{
	qer_editorimage textures/strafe64/godshaft.tga
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	cull none
	sort additive
	{
		map textures/strafe64/godshaft.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.55 0.18 0 0.05
		tcMod scroll 0 0.04
	}
}

// SUNCORONA — a camera-facing flare placed at the sky-sun bearing. autosprite
// billboards the quad to always face the view, so the bloom-lifted disc reads
// from every angle. Pure additive over black; bloom does the rest.
textures/strafe64/suncorona
{
	qer_editorimage textures/strafe64/suncorona.tga
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	cull none
	sort additive
	deformVertexes autosprite
	{
		map textures/strafe64/suncorona.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.8 0.15 0 0.07
	}
}
"""


def godray_shaders():
    return GODRAY_SHADERS


# ======================================================================
# PROCEDURAL TEXTURES
# ======================================================================
_TEX_CACHE = None


def _build_textures():
    n = 64
    tr, tg, tb = SHAFT_TINT

    # ---- GODSHAFT: u = across the beam (soft edges), v = along it (motes).
    # Core bright, gaussian falloff to black at the u-edges so the additive card
    # has no hard rim. Faint vertical streaks + sparse motes give the beam grain
    # that the slow scroll animates. Black => contributes nothing additively. ----
    rng = random.Random(0x60D5)
    streak = [0.85 + 0.30 * rng.random() for _ in range(n)]      # per-column grain
    motes = [[(1.0 if rng.random() < 0.012 else 0.0) for _ in range(n)]
             for _ in range(n)]
    shaft = []
    for y in range(n):
        for x in range(n):
            u = (x / (n - 1)) - 0.5                  # -0.5 .. 0.5 across
            core = math.exp(-(u * u) / (2 * 0.16 * 0.16))   # soft gaussian core
            v = core * streak[x]
            # a gentle along-beam fade (brighter toward the top / source)
            v *= 0.55 + 0.45 * (y / (n - 1))
            if motes[y][x]:
                v = min(1.0, v + 0.5 * core)         # a drifting dust speck
            shaft.append((_clamp8(255 * v * tr),
                          _clamp8(255 * v * tg),
                          _clamp8(255 * v * tb)))

    # ---- SUNCORONA: radial flare — hot core, smooth falloff, faint spikes. ----
    cor = []
    cx = cy = (n - 1) / 2.0
    for y in range(n):
        for x in range(n):
            dx, dy = (x - cx) / cx, (y - cy) / cy
            r = math.hypot(dx, dy)
            g = max(0.0, 1.0 - r)
            g = g * g                                # smooth disc
            ang = math.atan2(dy, dx)
            spike = 0.18 * max(0.0, math.cos(ang * 6)) * max(0.0, 1.0 - r * 0.8)
            v = min(1.0, g + spike)
            cor.append((_clamp8(255 * v * tr),
                        _clamp8(255 * v * tg),
                        _clamp8(255 * v * tb)))

    return {
        TEX_GODSHAFT + ".tga": _tga32(n, n, shaft),
        TEX_CORONA + ".tga":   _tga32(n, n, cor),
    }


def godray_textures():
    global _TEX_CACHE
    if _TEX_CACHE is None:
        _TEX_CACHE = _build_textures()
    return _TEX_CACHE


# ======================================================================
# GEOMETRY
# ======================================================================
def _shear_slab(mn, mx, shx, shy, tex, palette, draw):
    """A box whose TOP face (z=max) is translated by (shx, shy): a leaning
    parallelepiped. Every face stays planar (affine shear of a cube), so it is a
    valid convex brush. `draw` is the set of face keys to render."""
    verts = []
    for i in range(8):
        x = mx[0] if i & 1 else mn[0]
        y = mx[1] if i & 2 else mn[1]
        z = mx[2] if i & 4 else mn[2]
        if i & 4:                      # top corners lean toward the sun
            x += shx
            y += shy
        verts.append((x, y, z))
    faces = []
    for key in FACE_KEYS:
        poly = _oriented(FACE_CORNERS[key], verts, FACE_NORMALS[key])
        faces.append(Face(poly, tex, palette, draw=(key in draw)))
    return Brush(faces, CONTENTS_DECOR)


def make_shaft(cx, cy, z0, z1, width=34.0, az_deg=0.0, lean=SHAFT_LEAN_FRAC):
    """One leaning light-shaft column centred at (cx,cy), from z0 up to z1.

    Square-ish cross-section so it reads from any angle; the four vertical faces
    draw (the caps don't — they'd flash as bright lozenges). Leans toward the sun
    bearing az_deg by `lean` of its height."""
    h = z1 - z0
    a = math.radians(az_deg)
    shx = lean * h * math.cos(a)
    shy = lean * h * math.sin(a)
    half = width / 2.0
    return _shear_slab((cx - half, cy - half, z0), (cx + half, cy + half, z1),
                       shx, shy, TEX_GODSHAFT, (255, 255, 255),
                       draw={"-x", "+x", "-y", "+y"})


def make_corona(cx, cy, cz, size=160.0):
    """A single camera-facing billboard quad (one drawn face of a thin decor
    box) carrying the corona shader; autosprite in the shader aims it at the
    view, so which face draws is irrelevant — pick the cheap +z cap."""
    half = size / 2.0
    return _shear_slab((cx - half, cy - half, cz),
                       (cx + half, cy + half, cz + 2.0),
                       0.0, 0.0, TEX_CORONA, (255, 255, 255), draw={"+z"})


# ======================================================================
# PLACEMENT
# ======================================================================
def _play_bounds(course):
    """Horizontal extent + floor/ceil z of the solid play volume."""
    solids = [b for b in course.solids
              if getattr(b, "contents", 1) == 1]          # CONTENTS_SOLID
    if not solids:
        return None
    xs = [b.mins[0] for b in solids] + [b.maxs[0] for b in solids]
    ys = [b.mins[1] for b in solids] + [b.maxs[1] for b in solids]
    zs = [b.mins[2] for b in solids] + [b.maxs[2] for b in solids]
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def add_godrays(course, seed=0, count=7, width=34.0, corona=True,
                az_deg=None, el_deg=None, lean=None, inset=0.16):
    """Scatter `count` sun-shafts across the arena and (optionally) a corona at
    the sun bearing. Shafts rise from just above the floor toward the sky and
    lean toward the sun, so they read as light pouring in. Returns the number of
    decor brushes appended. Safe on any built course (no-op if it has no solids).

    Sun bearing/elevation default to strafegen_gfx's (lazy import) so the beams
    agree with the painted sky-sun and the cast shadows."""
    bb = _play_bounds(course)
    if bb is None:
        return 0
    if az_deg is None or el_deg is None:
        import strafegen_gfx as gfx               # lazy: avoids an import cycle
        az_deg = gfx.SUN_AZIMUTH_DEG if az_deg is None else az_deg
        el_deg = gfx.SUN_ELEVATION_DEG if el_deg is None else el_deg
    if lean is None:
        # scale the visible rake from the real sun elevation, capped readable
        lean = min(0.7, SHAFT_LEAN_FRAC / max(0.3, math.tan(math.radians(el_deg))))
    x0, x1, y0, y1, fz, cz = bb
    dx, dy = (x1 - x0), (y1 - y0)
    ix, iy = dx * inset, dy * inset                # keep shafts off the walls
    lo_x, hi_x = x0 + ix, x1 - ix
    lo_y, hi_y = y0 + iy, y1 - iy
    a = math.radians(az_deg)
    cosa, sina = math.cos(a), math.sin(a)
    rng = random.Random((int(seed) & 0xffffffff) ^ 0x5A17)
    top = cz - (cz - fz) * 0.06                    # stop just shy of the ceiling
    bot = fz + (cz - fz) * 0.10                    # start a touch above the floor

    def clamp(v, lo, hi):
        return max(lo, min(hi, v))

    added = []
    for _ in range(count):
        w = width * rng.uniform(0.7, 1.4)
        z1 = top
        z0 = bot + (top - bot) * rng.uniform(0.0, 0.18)
        # the leaned TOP travels (shx,shy) from the base; pick a base whose top
        # ALSO lands inside the inset bounds, so no shaft pokes through a wall.
        shx, shy = lean * (z1 - z0) * cosa, lean * (z1 - z0) * sina
        bx_lo, bx_hi = lo_x - min(0, shx), hi_x - max(0, shx)
        by_lo, by_hi = lo_y - min(0, shy), hi_y - max(0, shy)
        sx = rng.uniform(min(bx_lo, bx_hi), max(bx_lo, bx_hi))
        sy = rng.uniform(min(by_lo, by_hi), max(by_lo, by_hi))
        added.append(make_shaft(sx, sy, z0, z1, width=w,
                                az_deg=az_deg, lean=lean))
    if corona:
        # the sun disc: high, biased toward the sun bearing, but kept INSIDE the
        # hull (a corona beyond the solid sky/wall brushes would be occluded).
        cx0, cy0 = (x0 + x1) / 2, (y0 + y1) / 2
        ccx = clamp(cx0 + 0.42 * dx * cosa, lo_x, hi_x)
        ccy = clamp(cy0 + 0.42 * dy * sina, lo_y, hi_y)
        ccz = cz - (cz - fz) * 0.10
        added.append(make_corona(ccx, ccy, ccz))
    course.solids.extend(added)
    return len(added)
