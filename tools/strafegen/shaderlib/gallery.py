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
textures/strafe64/fog
{
\tqer_nocarve
\tsurfaceparm fog
\tsurfaceparm nolightmap
\tfogparms ( 0.015 0.0 0.04 ) 3200
}

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
]


# ---------------------------------------------------------------------------
# gallery map: a sealed dark room with one glowing panel per library shader
# ---------------------------------------------------------------------------
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
        PW, PH, GAP = 160.0, 220.0, 80.0
        row_w = n * PW + (n + 1) * GAP
        W = max(420.0, row_w / 2.0 + 48.0)
        D, H = 380.0, 320.0
        T = 16.0  # shell thickness
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
        cz0 = H * 0.5 - PH * 0.5
        cz1 = cz0 + PH
        x = -row_w / 2.0 + GAP
        for s in self.shaders:
            shader = "shaderlib/" + s["key"]
            self.solids.append(make_box(
                (x, D - 4.0, cz0), (x + PW, D + T, cz1),
                tex=ROOM, draw={"-y"}, face_tex={"-y": shader}))
            x += PW + GAP

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
    for s in SHADERS:
        textures.update(s["textures"]())
        shader_text += "\n" + s["shader"]().strip() + "\n"

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
