#!/usr/bin/env python3
"""STRAFE 64 SHADER LIBRARY — a viewable gallery of ported "stunning" shaders.

Each iteration of the shader loop adds ONE entry to SHADERS below: a famous /
beautiful shader (Shadertoy, GitHub, forums) recreated with idTech3 shader-script
techniques (multi-stage blends, tcMod scroll/turb/rotate/stretch, deformVertexes,
rgbGen/alphaGen waves, the audio genfuncs bass/mid/high/level) and bent to STRAFE
64's direction: synthwave / neon / void / dissolving-world / audio-reactive. We
can't run raw GLSL per-surface in idTech3, so we port the LOOK, not the code —
baking what the fragment shader computes per-pixel into a procedural texture and
animating it with the shader stages.

Run this to (re)build the gallery: it generates procedural textures, a loose
`strafe64_shaderlib.shader`, and a sealed dark gallery map with one glowing panel
per shader, then deploys them to baseoa so you can walk the gallery in-engine:

    python3 gallery.py                  # build + deploy
    # then in-engine:  /map shaderlib_gallery

Convention for a library shader `shaderlib/<key>`: an opaque dark BEZEL base stage
so the panel reads as a screen, then the effect additively on top so it glows
through the gl2 bloom. Keep at least one stage audio-reactive where it fits — the
world rides the music in this game.
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STRAFEGEN = os.path.dirname(HERE)
sys.path.insert(0, STRAFEGEN)
from strafegen import make_box, BspWriter, _tga32  # noqa: E402

OA = os.environ.get(
    "STRAFE64_OA", "/Users/gustav/strafe64-engine/assets/openarena")
BASEOA = os.path.join(OA, "baseoa")
MAP_NAME = "shaderlib_gallery"


# ---------------------------------------------------------------------------
# procedural-texture helpers
# ---------------------------------------------------------------------------
def _lerp(a, b, t):
    return a + (b - a) * t


def _smooth(t):
    return t * t * (3.0 - 2.0 * t)


def tileable_fbm(n, octaves, seed):
    """A seamless [0,1] fractal-noise field, n*n. Lattice freqs are powers of
    two that divide n, with lattice wraparound, so the texture tiles."""
    rng = random.Random(seed)
    field = [0.0] * (n * n)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        freq = 2 ** (o + 2)            # 4,8,16,...  (divide n=128)
        if freq > n:
            break
        grid = [rng.random() for _ in range(freq * freq)]

        def at(gx, gy):
            return grid[(gy % freq) * freq + (gx % freq)]

        cell = n / freq
        for y in range(n):
            fy = y / cell
            gy = int(fy)
            ty = _smooth(fy - gy)
            for x in range(n):
                fx = x / cell
                gx = int(fx)
                tx = _smooth(fx - gx)
                v = _lerp(_lerp(at(gx, gy), at(gx + 1, gy), tx),
                          _lerp(at(gx, gy + 1), at(gx + 1, gy + 1), tx), ty)
                field[y * n + x] += v * amp
        norm += amp
        amp *= 0.55
    return [v / norm for v in field]


def ramp(stops, t):
    """Piecewise-linear colour ramp. stops = [(pos,(r,g,b)),...] sorted."""
    if t <= stops[0][0]:
        return stops[0][1]
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        if t <= p1:
            k = (t - p0) / (p1 - p0) if p1 > p0 else 0.0
            return tuple(_lerp(c0[i], c1[i], k) for i in range(3))
    return stops[-1][1]


def flat_tex(n, rgb):
    return _tga32(n, n, [tuple(rgb)] * (n * n))


def grid_tex(n, base, line, major):
    """A dark neutral measure grid for the gallery room surfaces."""
    px = []
    for y in range(n):
        for x in range(n):
            on_minor = (x % 16 == 0) or (y % 16 == 0)
            on_major = (x % (n // 2) == 0) or (y % (n // 2) == 0)
            c = major if on_major else (line if on_minor else base)
            px.append(tuple(c))
    return _tga32(n, n, px)


# ---------------------------------------------------------------------------
# the room + fog shaders (gallery scaffolding, not library entries)
# ---------------------------------------------------------------------------
ROOM_SHADERS = """
// ---- STRAFE 64 shader-library gallery scaffolding (generated) ----
// NOTE: no textures/strafe64/fog defined here. That name is the canonical
// identity fog (strafegen SHADER_SCRIPT); re-defining it loose in this gallery
// shader collided GLOBALLY — idTech3 keeps the first-scanned definition and
// silently drops the rest, so every map's fog became load-order roulette. The
// gallery never applied a fog surface anyway, so the dead def is simply gone.
shaderlib/room
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/grid.tga
\t\trgbGen identity
\t}
}
"""


def room_textures():
    return {
        "textures/shaderlib/grid.tga":
            grid_tex(64, (20, 20, 26), (34, 34, 46), (52, 50, 74)),
        "textures/shaderlib/bezel.tga": flat_tex(16, (7, 7, 11)),
    }


# ===========================================================================
# THE LIBRARY.  Append one dict per loop iteration.
#   key        : shader id -> shaderlib/<key>
#   title      : display name
#   ref        : (source, url) the stunning shader it ports
#   blurb      : one-line description (for GALLERY.md)
#   technique  : how the idTech3 port fakes it
#   textures() : {arcname: tga_bytes}
#   shader()   : the shaderlib/<key> script text
# ===========================================================================

# --- #1 : Neon Plasma Flow -------------------------------------------------
def _plasma_textures():
    # classic smooth "plasma": a sum of directional sines. Every frequency is an
    # INTEGER number of cycles across the tile, so it wraps seamlessly. Smooth
    # large bands (the flow comes from the shader's scroll+turb at runtime), and
    # a palette that's mostly dark so the neon veins read against the void.
    n = 128
    tau = 2.0 * math.pi
    waves = [(2, 0, 0.0), (0, 3, 1.3), (2, 2, 0.5),
             (1, -1, 2.1), (4, 2, 0.9), (-3, 1, 3.4)]
    stops = [
        (0.00, (6, 0, 20)),       # near-black indigo void (most of the field)
        (0.45, (40, 4, 60)),      # dim violet
        (0.66, (150, 16, 130)),   # magenta vein
        (0.80, (255, 48, 120)),   # hot pink
        (0.90, (255, 130, 80)),   # sunset coral
        (0.97, (80, 220, 255)),   # cyan crest
        (1.00, (220, 255, 255)),  # white-hot core
    ]
    px = []
    for y in range(n):
        v = y / n
        for x in range(n):
            u = x / n
            s = sum(math.sin(tau * (fx * u + fy * v) + ph)
                    for fx, fy, ph in waves)
            t = s / len(waves) * 0.5 + 0.5            # -> 0..1
            t = t ** 1.6                              # bias dark, sharpen veins
            px.append(tuple(int(c) for c in ramp(stops, t)))
    return {"textures/shaderlib/plasma.tga": _tga32(n, n, px)}


def _plasma_shader():
    return """
shaderlib/plasma
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tmap textures/shaderlib/plasma.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave sin 0.55 0.35 0 0.04
\t\ttcMod scroll 0.018 0.011
\t\ttcMod turb 0 0.16 0 0.06
\t}
\t{
\t\tmap textures/shaderlib/plasma.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave sin 0.45 0.45 0.5 0.027
\t\ttcMod scale 1.7 1.7
\t\ttcMod scroll -0.015 0.008
\t\ttcMod turb 0.3 0.11 0 0.045
\t}
\t{
\t\tmap textures/shaderlib/plasma.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave bass 0 0.9 0 0
\t\ttcMod scale 0.7 0.7
\t\ttcMod scroll 0.005 -0.004
\t\ttcMod turb 0.6 0.2 0 0.03
\t}
}
"""


# --- #2 : Synthwave Sun ----------------------------------------------------
def _sun_textures():
    n = 128
    cx, cy, R = n * 0.5, n * 0.5, n * 0.42
    disc = [
        (0.00, (255, 250, 205)),  # hot white-yellow crown
        (0.32, (255, 196, 70)),   # amber
        (0.60, (255, 92, 120)),   # hot pink
        (1.00, (175, 28, 140)),   # magenta base
    ]
    sun, glow = [], []
    for y in range(n):
        for x in range(n):
            dx, dy = x - cx, y - cy
            r = math.hypot(dx, dy)
            # --- glow halo: soft radial magenta-pink falloff on black ---
            gf = 1.0 - r / (n * 0.52)
            if gf < 0:
                gf = 0.0
            gf = gf * gf
            glow.append((int(255 * gf), int(110 * gf), int(165 * gf)))
            # --- sun disc: vertical gradient, scanline bars on the lower half ---
            if r > R:
                sun.append((0, 0, 0))
                continue
            vy = (y - (cy - R)) / (2.0 * R)
            vy = 0.0 if vy < 0 else (1.0 if vy > 1 else vy)
            c = ramp(disc, vy)
            if y > cy:                       # lower half: retro scanline gaps
                below = (y - cy) / R         # 0..1 downward
                period = 4 + int(below * 11)
                if (y - int(cy)) % period < 1 + below * 5:
                    c = (0, 0, 0)            # gap (black -> additive shows nothing)
            sun.append(tuple(int(v) for v in c))
    # the panel's t maps inverted, so flip vertically -> crown up, stripes at the base
    def flipv(px):
        return [px[(n - 1 - y) * n + x] for y in range(n) for x in range(n)]
    return {
        "textures/shaderlib/sun.tga": _tga32(n, n, flipv(sun)),
        "textures/shaderlib/sunglow.tga": _tga32(n, n, flipv(glow)),
    }


def _sun_shader():
    return """
shaderlib/sun
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tmap textures/shaderlib/sunglow.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t%FIT%
\t\trgbGen wave bass 0.20 1.0 0 0
\t}
\t{
\t\tmap textures/shaderlib/sun.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t%FIT%
\t\trgbGen wave bass 0.74 0.5 0 0
\t}
}
"""


# --- #3 : Digital Rain -----------------------------------------------------
def _rain_textures():
    n = 128
    rng = random.Random(0x4A1B7)
    r = [0.0] * (n * n)
    g = [0.0] * (n * n)
    b = [0.0] * (n * n)

    def put(x, y, cr, cg, cb):           # additive-max into the wrapped buffer
        i = (y % n) * n + (x % n)
        if cr > r[i]:
            r[i] = cr
        if cg > g[i]:
            g[i] = cg
        if cb > b[i]:
            b[i] = cb

    for x in range(n):
        for _ in range(rng.randint(1, 4)):   # falling streaks per column
            hy = rng.randrange(n)
            tail = rng.randint(14, 50)
            for k in range(tail):
                f = (1.0 - k / tail) ** 1.6  # bright head -> dim tail (upward)
                v = f * (0.6 + 0.4 * rng.random())     # per-cell glyph flicker
                if k < 3:                    # white-cyan head (hot)
                    put(x, hy - k, 0.9 * v + 0.1, v, 0.97 * v)
                else:                        # cyan-blue tail
                    put(x, hy - k, 0.2 * v, 0.9 * v, v)
    px = [(int(min(1.0, r[i]) * 255), int(min(1.0, g[i]) * 255),
           int(min(1.0, b[i]) * 255)) for i in range(n * n)]
    return {"textures/shaderlib/rain.tga": _tga32(n, n, px)}


def _rain_shader():
    return """
shaderlib/rain
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tmap textures/shaderlib/rain.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave sin 1.0 0.22 0 0.4
\t\ttcMod scroll 0 0.32
\t}
\t{
\t\tmap textures/shaderlib/rain.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave high 0.35 0.9 0 0
\t\ttcMod scale 0.5 0.7
\t\ttcMod scroll 0.0 0.55
\t}
}
"""


# --- #4 : Neon Vortex ------------------------------------------------------
def _vortex_textures():
    n = 128
    cx = cy = n * 0.5
    tau = 2.0 * math.pi
    RINGS, SWIRL, SPOKES = 5.0, 2.0, 8.0
    stops = [
        (0.00, (225, 255, 255)),  # white-hot throat
        (0.22, (90, 215, 255)),   # cyan
        (0.50, (150, 45, 210)),   # violet
        (0.78, (225, 45, 135)),   # magenta
        (1.00, (12, 0, 26)),      # void edge
    ]
    px = []
    for y in range(n):
        for x in range(n):
            dx = (x - cx) / (n * 0.5)
            dy = (y - cy) / (n * 0.5)
            r = math.hypot(dx, dy)
            ang = math.atan2(dy, dx)
            spiral = 0.5 + 0.5 * math.sin(r * RINGS * tau - ang * SWIRL)
            spokes = 0.5 + 0.5 * math.sin(ang * SPOKES)
            fade = 1.0 - r
            if fade < 0:
                fade = 0.0
            v = spiral * fade * (0.55 + 0.45 * spokes)
            v = (v if v < 1 else 1.0) ** 1.4
            c = ramp(stops, r if r < 1 else 1.0)
            px.append(tuple(int(ch * v) for ch in c))
    return {"textures/shaderlib/vortex.tga": _tga32(n, n, px)}


def _vortex_shader():
    # clampMap (not map) so the rotation never wraps neighbouring tiles into the
    # corners — outside 0..1 clamps to the black edge.
    return """
shaderlib/vortex
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tclampMap textures/shaderlib/vortex.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t%FIT%
\t\ttcMod rotate 16
\t\trgbGen wave bass 0.6 0.5 0 0
\t}
\t{
\t\tclampMap textures/shaderlib/vortex.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t%FIT%
\t\ttcMod rotate -9
\t\trgbGen wave sin 0.28 0.16 0 0.11
\t}
}
"""


# --- #5 : Aurora Curtains --------------------------------------------------
def _aurora_textures():
    n = 128
    tau = 2.0 * math.pi
    cols = [(2, 0.0, 0.55), (3, 1.7, 0.30), (5, 3.3, 0.22), (7, 5.1, 0.16)]
    norm = sum(a for _, _, a in cols)
    stops = [
        (0.00, (150, 35, 210)),   # top: violet
        (0.42, (60, 110, 255)),   # blue
        (0.70, (40, 230, 210)),   # cyan
        (1.00, (110, 255, 120)),  # base: green
    ]
    px = []
    for y in range(n):
        vy = y / n
        env = math.sin(math.pi * (vy if vy < 1 else 1.0))   # fade top+bottom, bright mid
        c = ramp(stops, vy)
        for x in range(n):
            u = x / n
            ci = sum(a * (0.5 + 0.5 * math.sin(tau * f * u + ph))
                     for f, ph, a in cols) / norm
            b = (ci ** 1.7) * env                # sharpen into curtains
            px.append(tuple(int(ch * b) for ch in c))
    return {"textures/shaderlib/aurora.tga": _tga32(n, n, px)}


def _aurora_shader():
    return """
shaderlib/aurora
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tmap textures/shaderlib/aurora.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave sin 0.7 0.18 0 0.04
\t\ttcMod turb 0 0.05 0 0.05
\t\ttcMod scroll 0.011 0
\t}
\t{
\t\tmap textures/shaderlib/aurora.tga
\t\tblendFunc GL_ONE GL_ONE
\t\trgbGen wave level 0.18 0.7 0 0
\t\ttcMod scale 1.7 1.0
\t\ttcMod turb 0.4 0.045 0 0.035
\t\ttcMod scroll -0.015 0
\t}
}
"""


# --- #6 : Neon Fire --------------------------------------------------------
def _fire_textures():
    n = 128
    f1 = tileable_fbm(n, 5, 0xF18E)
    f2 = tileable_fbm(n, 4, 0x3C0A)
    flame = [
        (0.00, (4, 0, 14)),       # ember dark
        (0.30, (95, 12, 90)),     # purple
        (0.55, (215, 35, 95)),    # magenta
        (0.78, (255, 120, 45)),   # orange
        (0.93, (255, 215, 120)),  # yellow
        (1.00, (255, 255, 220)),  # white core
    ]
    fire = []
    grad = []
    for y in range(n):
        vy = y / n
        # vertical fade mask, bright at the flame BASE -> dark at the top. The panel's
        # t maps inverted (see the sun), so white-at-image-top renders at panel-bottom.
        gm = ((1.0 - vy) ** 0.95)
        gv = int(255 * gm)
        for x in range(n):
            i = y * n + x
            v = 0.6 * f1[i] + 0.4 * f2[i]
            v = (v if v < 1 else 1.0) ** 1.2
            fire.append(tuple(int(c) for c in ramp(flame, v)))
            grad.append((gv, gv, gv))
    return {
        "textures/shaderlib/fire.tga": _tga32(n, n, fire),
        "textures/shaderlib/firegrad.tga": _tga32(n, n, grad),
    }


def _fire_shader():
    # tiling flame noise scrolling UP + turb to lick, then a single fit+clamped
    # vertical gradient MULTIPLIED over it so the flames brighten at the base and
    # die toward the top (the fade maps once; the noise tiles).
    return """
shaderlib/fire
{
\tnopicmip
\t{
\t\tmap textures/shaderlib/bezel.tga
\t\trgbGen identity
\t}
\t{
\t\tmap textures/shaderlib/fire.tga
\t\tblendFunc GL_ONE GL_ONE
\t\ttcMod scroll 0.012 -0.34
\t\ttcMod turb 0 0.06 0 0.4
\t\trgbGen wave bass 0.95 0.5 0 0
\t}
\t{
\t\tmap textures/shaderlib/fire.tga
\t\tblendFunc GL_ONE GL_ONE
\t\ttcMod scale 1.7 1.4
\t\ttcMod scroll -0.018 -0.55
\t\ttcMod turb 0.5 0.05 0 0.5
\t\trgbGen wave sin 0.72 0.2 0 0.2
\t}
\t{
\t\tclampMap textures/shaderlib/firegrad.tga
\t\tblendFunc GL_DST_COLOR GL_ZERO
\t\t%FIT%
\t}
}
"""


SHADERS = [
    {
        "key": "plasma",
        "title": "Neon Plasma Flow",
        "ref": ("Shadertoy — 'Neon Plasma Storm' (4scGDH) / 'Plasma Waves' "
                "(ltXczj)", "https://www.shadertoy.com/view/4scGDH"),
        "blurb": "Flowing synthwave plasma — three turbulent neon layers that "
                 "churn and flare on the kick drum.",
        "technique": "Baked seamless 2x-fBm field through a synthwave palette, "
                     "then three additive stages scroll + tcMod-turb it at "
                     "different scales; the third stage's brightness is "
                     "rgbGen wave bass so it pumps with the music.",
        "textures": _plasma_textures,
        "shader": _plasma_shader,
    },
    {
        "key": "sun",
        "title": "Synthwave Sun",
        "ref": ("Shadertoy — 'Synthwave Shader [VIP2017]' (MslfRn) / 'another "
                "synthwave sunset thing' (tsScRK)",
                "https://www.shadertoy.com/view/MslfRn"),
        "blurb": "The signature retro sunset disc — white-hot crown fading to "
                 "magenta, cut by horizontal scanline bars, glowing and pulsing "
                 "on the kick.",
        "technique": "Baked sun disc (vertical white->amber->magenta gradient "
                     "with widening scanline gaps on the lower half) plus a soft "
                     "radial halo, both on black for additive blend. rgbGen wave "
                     "bass swells the halo and pumps the disc on the kick.",
        "textures": _sun_textures,
        "shader": _sun_shader,
        "fit": True,
    },
    {
        "key": "rain",
        "title": "Digital Rain",
        "ref": ("Shadertoy — 'Matrix rain' family (e.g. ldjBW1) / the classic "
                "digital-rain effect", "https://www.shadertoy.com/view/ldjBW1"),
        "blurb": "Falling cyan data-streaks — the dissolving digital world; bright "
                 "white-cyan heads trailing into blue, raining harder on the hats.",
        "technique": "Baked seamless column streaks (white-cyan heads, fading cyan "
                     "tails, per-cell glyph flicker, vertically wrapped to tile) "
                     "then two additive layers scroll downward at different scales "
                     "for parallax; the near layer's brightness is rgbGen wave high "
                     "so hats/snares burst the rain. Tiles (no fit).",
        "textures": _rain_textures,
        "shader": _rain_shader,
    },
    {
        "key": "vortex",
        "title": "Neon Vortex",
        "ref": ("Shadertoy — neon 'tunnel/vortex' family (e.g. Xsl3zX swirl "
                "tunnels)", "https://www.shadertoy.com/view/Xsl3zX"),
        "blurb": "A spinning spiral tunnel pulling toward a white-hot throat — the "
                 "momentum-portal / bullet-time warp, pumping on the kick.",
        "technique": "Baked spiral rings + radial spokes through a center-out "
                     "white->cyan->magenta->void palette. Two counter-rotating "
                     "additive layers (tcMod rotate) spin it; clampMap stops the "
                     "rotation wrapping the corners, %FIT% maps it once to the "
                     "panel, and rgbGen wave bass flares the throat on the kick.",
        "textures": _vortex_textures,
        "shader": _vortex_shader,
        "fit": True,
    },
    {
        "key": "aurora",
        "title": "Aurora Curtains",
        "ref": ("Shadertoy — 'Auroras' by nimitz (NdfyRM)",
                "https://www.shadertoy.com/view/XtGGRt"),
        "blurb": "Waving neon light curtains — green base rising through cyan to "
                 "violet, rippling and lifting with the music.",
        "technique": "Baked tileable vertical curtains (sum-of-sines density, "
                     "sharpened, with a green->cyan->violet vertical gradient and a "
                     "soft top/bottom fade) on black for additive blend. Two layers "
                     "ripple via tcMod turb and drift via tcMod scroll at different "
                     "scales; the second layer's brightness is rgbGen wave level so "
                     "the whole sky lifts with the mix. Tiles (no fit).",
        "textures": _aurora_textures,
        "shader": _aurora_shader,
    },
    {
        "key": "fire",
        "title": "Neon Fire",
        "ref": ("Shadertoy — classic procedural 'fire'/'flame' family (e.g. "
                "MdKfDh)", "https://www.shadertoy.com/view/MdKfDh"),
        "blurb": "Rising synthwave flames — embers through purple, magenta and "
                 "orange to a white core, licking and flaring on the kick.",
        "technique": "Seamless 2x-fBm noise baked through a fire palette (ember→"
                     "purple→magenta→orange→white) tiles and scrolls UP with tcMod "
                     "turb to lick; a single fit+clampMap vertical gradient is "
                     "MULTIPLIED over it (GL_DST_COLOR GL_ZERO) so the flames "
                     "brighten at the base and die at the top. rgbGen wave bass "
                     "flares them on the kick. Mixes a tiling effect with one fit "
                     "fade layer.",
        "textures": _fire_textures,
        "shader": _fire_shader,
    },
]


# ---------------------------------------------------------------------------
# gallery layout: a sealed dark room with one glowing panel per library shader.
# Shared by the map builder and the shader writer so the per-panel "fit" texture
# transform matches the panel it lands on.
# ---------------------------------------------------------------------------
PW, PH, GAP = 230.0, 230.0, 90.0    # panel width/height + gap (square -> round discs)
D, H, T = 380.0, 360.0, 16.0        # room half-depth, height, shell thickness
PANEL_Y = D - 4.0                   # front face plane of every panel


def _panel_layout(n):
    """Return (W, rects). rects[i] = (x0, z0, x1, z1) of panel i's front face."""
    row_w = n * PW + (n + 1) * GAP
    W = max(440.0, row_w / 2.0 + 48.0)
    z0 = H * 0.5 - PH * 0.5
    rects = []
    x = -row_w / 2.0 + GAP
    for _ in range(n):
        rects.append((x, z0, x + PW, z0 + PH))
        x += PW + GAP
    return W, rects


def _fit_tcmod(rect):
    """A `tcMod transform` that maps the texture's 0..1 exactly onto this panel's
    front face (no tiling, no wrap-split), for single-image shaders. The face is
    -y, so _face_st gives s=x/64, t=z/64; we invert that to fit the panel rect."""
    x0, z0, x1, z1 = rect
    msx, msz = 64.0 / (x1 - x0), 64.0 / (z1 - z0)
    return ("tcMod transform %g 0 0 %g %g %g"
            % (msx, msz, -x0 / (x1 - x0), -z0 / (z1 - z0)))


class GalleryRoom:
    def __init__(self, shaders):
        self.shaders = shaders
        self.solids = []
        self.triggers = []
        self.movers = []
        self.entities = []
        self.sections = []

    def build(self):
        n = len(self.shaders)
        W, rects = _panel_layout(n)
        ROOM = "shaderlib/room"

        def shell(mins, maxs, face):
            self.solids.append(make_box(mins, maxs, tex=ROOM, draw={face},
                                        face_tex={face: ROOM}))

        shell((-W, -D, -T), (W, D, 0.0), "+z")            # floor
        shell((-W, -D, H), (W, D, H + T), "-z")           # ceiling
        shell((-W - T, -D, -T), (-W, D, H + T), "+x")     # left wall
        shell((W, -D, -T), (W + T, D, H + T), "-x")       # right wall
        shell((-W, D, -T), (W, D + T, H + T), "-y")       # far wall (behind panels)
        shell((-W, -D - T, -T), (W, -D, H + T), "+y")     # near wall (behind spawn)

        # display panels mounted on the far (+y) wall, facing the spawn (-y)
        for s, (x0, z0, x1, z1) in zip(self.shaders, rects):
            shader = "shaderlib/" + s["key"]
            self.solids.append(make_box(
                (x0, PANEL_Y, z0), (x1, D + T, z1),
                tex=ROOM, draw={"-y"}, face_tex={"-y": shader}))

        self.entities.append({"classname": "worldspawn",
                               "message": "STRAFE 64 shader library"})
        self.entities.append({
            "classname": "info_player_deathmatch",
            "origin": f"0 {-D + 110:g} 40", "angle": "90"})
        self.entities.append({
            "classname": "info_player_intermission",
            "origin": f"0 0 {H - 64:g}", "angle": "90"})
        return self


# ---------------------------------------------------------------------------
# build + deploy
# ---------------------------------------------------------------------------
def build():
    textures = dict(room_textures())
    shader_text = ROOM_SHADERS
    _, rects = _panel_layout(len(SHADERS))
    for s, rect in zip(SHADERS, rects):
        textures.update(s["textures"]())
        # any stage carrying a %FIT% token gets a per-panel tcMod transform that maps
        # the texture once onto the panel (single-image discs, or a fade mask layered
        # over a tiling effect); stages without the token tile by world UV as usual.
        shader_text += ("\n" + s["shader"]().strip()
                        .replace("%FIT%", _fit_tcmod(rect)) + "\n")

    scripts = os.path.join(BASEOA, "scripts")
    tex_dir = os.path.join(BASEOA, "textures", "shaderlib")
    maps_dir = os.path.join(BASEOA, "maps")
    for d in (scripts, tex_dir, maps_dir):
        os.makedirs(d, exist_ok=True)

    with open(os.path.join(scripts, "strafe64_shaderlib.shader"), "w") as fh:
        fh.write(shader_text)
    for arc, data in textures.items():
        # arc is "textures/shaderlib/<file>"
        with open(os.path.join(BASEOA, arc), "wb") as fh:
            fh.write(data)

    bsp = os.path.join(maps_dir, MAP_NAME + ".bsp")
    stats = BspWriter(GalleryRoom(SHADERS).build()).write(bsp)

    print(f"shader library: {len(SHADERS)} shader(s)")
    for s in SHADERS:
        print(f"  - shaderlib/{s['key']:<12} {s['title']}")
    print(f"deployed shader  -> {scripts}/strafe64_shaderlib.shader")
    print(f"deployed {len(textures)} textures -> {tex_dir}")
    print(f"deployed map     -> {bsp}  ({stats})")
    print(f"\nin-engine:  /map {MAP_NAME}")


if __name__ == "__main__":
    build()
