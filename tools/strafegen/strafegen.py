#!/usr/bin/env python3
"""
strafegen — procedural movement-run map generator for Quake III Arena.

Writes playable IBSP v46 .bsp files directly (no q3map compile pass), tuned
to the movement mod in code/game/bg_pmove.c: bunny-hop chains, double jump,
crouch slide, CPM air control and walljumps.

    python3 strafegen.py 1337                 # generate generated/strafe64_1337.bsp
    python3 strafegen.py 1337 --difficulty 2  # harder gaps, fewer rest pads
    python3 strafegen.py 1337 --pk3           # also pack a .pk3 with an .arena file
    python3 strafegen.py 1337 --map           # also emit a Radiant-editable .map
    python3 strafegen.py --daily --pk3        # today's tower: same course worldwide
    python3 strafegen.py --check FILE.bsp     # validate any generated bsp
    python3 strafegen.py --selftest           # generate+validate a seed matrix

Courses carry race triggers (trigger_race_start / trigger_race_finish)
for the mod's run timer + ghosts, and worldspawn void keys (voidbase /
voidrise / voiddelay) for the rising kill plane. length > 1 builds a
tower: decks of sections chained by lift-gate teleporters, climbing
away from the void.

Drop the .bsp into baseq3/maps/ (or the .pk3 into baseq3/) and run:
    \\map strafe64_1337
"""

import argparse
import math
import os
import random
import struct
import sys
import zipfile

# ======================================================================
# Movement-mod tuning. These mirror code/game/bg_pmove.c + bg_local.h —
# if the mod's numbers change, change them here so courses stay solvable.
# ======================================================================
GRAVITY       = 1000.0   # g_gravity default (snappier, less floaty)
RUN_SPEED     = 320.0    # g_speed default
JUMP_VELOCITY = 300.0    # bg_local.h JUMP_VELOCITY (raised with gravity, apex ~same)
STEPSIZE      = 18.0     # bg_local.h STEPSIZE
DJ_BOOST      = 75.0     # pm_doubleJumpBoost
SLIDE_MIN     = 250.0    # pm_slideMinSpeed
SLIDE_JUMP    = 1.08     # pm_slideJumpBoost
BHOP_MAX      = 1.10     # pm_bhopBoostMax
WALLJUMP_KICK = 200.0    # pm_wallJumpKick
WALLJUMP_VZ   = 250.0    # pm_wallJumpVelocity
WALLJUMP_MAX  = 2        # pm_wallJumpMax

AIR_TIME  = 2.0 * JUMP_VELOCITY / GRAVITY                    # 0.675 s
JUMP_APEX = JUMP_VELOCITY ** 2 / (2.0 * GRAVITY)             # 45.6 u
DJ_APEX   = (JUMP_VELOCITY + DJ_BOOST) ** 2 / (2 * GRAVITY)  # 74.4 u
LEDGE_SJ  = JUMP_APEX + STEPSIZE                             # ~63.6 u
LEDGE_DJ  = DJ_APEX + STEPSIZE                               # ~92.4 u


def jump_range(speed):
    """Flat-ground gap crossable at a given horizontal speed."""
    return speed * AIR_TIME


# safety margin applied to every reachability bound
SAFETY = 0.80

# ======================================================================
# BSP format (code/qcommon/qfiles.h, IBSP version 46)
# ======================================================================
BSP_IDENT   = b"IBSP"
BSP_VERSION = 46

LUMP_ENTITIES, LUMP_SHADERS, LUMP_PLANES, LUMP_NODES, LUMP_LEAFS, \
    LUMP_LEAFSURFACES, LUMP_LEAFBRUSHES, LUMP_MODELS, LUMP_BRUSHES, \
    LUMP_BRUSHSIDES, LUMP_DRAWVERTS, LUMP_DRAWINDEXES, LUMP_FOGS, \
    LUMP_SURFACES, LUMP_LIGHTMAPS, LUMP_LIGHTGRID, LUMP_VISIBILITY = range(17)
HEADER_LUMPS = 17

MST_PLANAR         = 1
LIGHTMAP_BY_VERTEX = -3   # vertex-lit surfaces; no lightmap lump needed

# game/surfaceflags.h
CONTENTS_SOLID   = 0x00000001
CONTENTS_TRIGGER = 0x40000000
SURF_SKY         = 0x00000004
SURF_NOIMPACT    = 0x00000010
SURF_NODRAW      = 0x00000080

SZ_SHADER    = 72   # <64s2i
SZ_PLANE     = 16   # <4f
SZ_NODE      = 36   # <9i
SZ_LEAF      = 48   # <12i
SZ_MODEL     = 40   # <6f4i
SZ_BRUSH     = 12   # <3i
SZ_BRUSHSIDE = 8    # <2i
SZ_DRAWVERT  = 44   # <10f4B
SZ_SURFACE   = 104  # <12i12f2i

# STRAFE 64 identity shaders, defined in scripts/strafe64.shader which is
# bundled into every --pk3. Pure vertex color on flat geometry — N64
# clarity. Without the pk3 they degrade to the grey default shader, which
# is still fully playable since section identity lives in vertex color.
TEX_FLOOR   = "textures/strafe64/surf"
TEX_WALL    = "textures/strafe64/wall"
TEX_SKY     = "textures/strafe64/sky"
TEX_CAULK   = "textures/common/caulk"
TEX_TRIGGER = "textures/common/trigger"

SHADER_SCRIPT = """\
// STRAFE 64 identity — the geometry is vertex-colored; the section's
// COLOUR is its identity (N64 clarity) and the detail map only carves
// tech panels/grid into it. rgbGen exactVertex multiplies the near-white
// detail texel by the vertex colour, so colour reads through everywhere
// and the lines just darken it. Detail TGAs are generated procedurally
// (see build_detail_textures) — a few KB, no hand-painted art. Bundled
// in every strafegen pk3.
textures/strafe64/surf
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_floor.tga
		rgbGen exactVertex
	}
}
textures/strafe64/wall
{
	surfaceparm nolightmap
	{
		map textures/strafe64/d_wall.tga
		rgbGen exactVertex
	}
	// section-tinted energy conduits drifting up the walls — additive,
	// so only the faint lines glow; rgbGen exactVertex keeps the colour.
	// tcMod stretch on the bass envelope (au_bass) makes the conduits
	// pulse-breathe on the kick — the walls bounce in time with the music
	// without touching geometry (texcoord only, so section colour and brush
	// seams are untouched). See snd_codec_mod.c / the audio genfuncs.
	{
		map textures/strafe64/accent.tga
		blendFunc GL_ONE GL_ONE
		rgbGen exactVertex
		tcMod scroll 0 0.04
		tcMod stretch bass 1.0 0.35 0 0
	}
}
textures/strafe64/sky
{
	qer_editorimage textures/strafe64/env/synth_ft.tga
	surfaceparm noimpact
	surfaceparm nolightmap
	surfaceparm nomarks
	surfaceparm sky
	// 90s vector / synthwave geometric-landscape skybox: a real 6-face box
	// (env/synth_{rt,lf,ft,bk,up,dn}) built by _build_synthsky from one
	// direction->colour function, so the cube is seamless. Outrun sunset +
	// scanline sun + jagged neon ridgeline above the horizon, a receding
	// neon grid below it. Replaces the old scrolling star dome.
	skyparms textures/strafe64/env/synth 512 -
}
// the rising void plane, drawn client-side by the cgame race layer
strafe64/void
{
	surfaceparm trans
	surfaceparm nonsolid
	surfaceparm nomarks
	surfaceparm nolightmap
	cull none
	// a churning digital lattice, not a flat sheet: the texture luminance
	// modulates the red, two layers scroll + warp against each other so
	// the kill-plane looks like dissolving data rising to eat the world.
	// deformVertexes on au_bass makes the whole plane heave on the kick —
	// the void physically breathes with the music. Safe here: the plane is
	// translucent + cull none + nonsolid, so the render-only vertex push has
	// no seams to crack and never affects collision.
	deformVertexes wave 64 bass 0 10 0 0
	{
		map textures/strafe64/void_hex.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen const ( 1.00 0.08 0.28 )
		alphaGen const 0.55
		tcMod scroll 0.05 0.07
		tcMod turb 0 0.15 0 0.25
	}
	{
		map textures/strafe64/void_hex.tga
		blendFunc GL_ONE GL_ONE
		// brightness rides the bass envelope: base glow + a kick-driven
		// flare (was a fixed sine throb)
		rgbGen wave bass 0.06 0.22 0 0
		tcMod scale 2 2
		tcMod scroll -0.03 -0.04
	}
}
// the racing ghost — a flat translucent silhouette of the player model,
// drawn client-side by the cgame race layer over the best run. rgbGen /
// alphaGen entity hand tint + opacity to cgame, so the ghost colour and
// cg_ghostAlpha ride the entity's shaderRGBA, tunable live. A cool hologram
// tint that never reads as a live (opaque) player, legible at speed under
// the PSX point-sampling preset. No texture needed.
strafe64/ghost
{
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen entity
		alphaGen entity
	}
}
"""

# tower decks: vertical spacing between chained section decks, and how
# long the void should take to swallow one deck (the pace pressure)
DECK_RISE = 1024.0
VOID_SECONDS_PER_DECK = (90.0, 70.0, 55.0)   # by difficulty
VOID_FLAT_RATE        = (8.0, 12.0, 16.0)    # ups/s for length-1 courses
VOID_DELAY            = (20.0, 15.0, 12.0)   # grace seconds


# ======================================================================
# procedural detail textures
#
# Small tiling grayscale-ish maps, generated by code (no hand-painted
# art, a few KB zipped). They sit near white so the vertex colour shows
# through (rgbGen exactVertex multiplies), and only darker grid/panel
# lines + faint noise carve tech detail into each section's colour. At
# 64px over a 64-unit tile that is ~1 texel/unit — deliberately lo-fi,
# crisp and PSX-friendly under point sampling.
# ======================================================================
TEX_SIZE = 64


def _tga32(w, h, px):
    """Uncompressed 32-bit TGA. px is a flat list of (r,g,b) rows top→bottom."""
    hdr = struct.pack("<BBBHHBHHHHBB",
                      0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 8)  # bottom-up, 8 alpha
    out = bytearray(hdr)
    for y in range(h - 1, -1, -1):
        row = px[y * w:(y + 1) * w]
        for r, g, b in row:
            out += bytes((b, g, r, 255))
    return bytes(out)


def _clamp8(v):
    return 0 if v < 0 else (255 if v > 255 else int(v))


def build_detail_textures():
    """Return {arcname: tga_bytes} for the tech-panel detail maps."""
    rng = random.Random(0xC0FFEE)
    n = TEX_SIZE
    noise = [rng.randint(-6, 6) for _ in range(n * n)]

    # --- floor: 32-unit cells, 2px major grid, 16px minor, corner rivets ---
    floor = []
    for y in range(n):
        for x in range(n):
            v = 238 + noise[y * n + x]
            # alternating panels read as tech flooring (clarity-safe: a
            # gentle shade, not a pattern that fights the section colour)
            if ((x // 32) + (y // 32)) % 2 == 0:
                v -= 12
            if x % 32 < 2 or y % 32 < 2:           # major panel seams
                v -= 104
            elif x % 16 == 0 or y % 16 == 0:       # minor grid
                v -= 34
            # rivet at each 32-cell corner
            if (x % 32) in (3, 4) and (y % 32) in (3, 4):
                v -= 66
            floor.append((_clamp8(v * 1.00), _clamp8(v * 1.02), _clamp8(v * 0.98)))

    # --- wall: vertical panel seams every 16px, mid horizontal band, scanlines ---
    wall = []
    for y in range(n):
        for x in range(n):
            v = 228 + noise[y * n + x]
            if x % 16 < 2:                         # vertical seams
                v -= 82
            if x % 32 < 2:                         # major bay seams
                v -= 28
            if 30 <= y <= 33:                      # waist band
                v -= 52
            if y % 4 == 0:                         # faint scanlines
                v -= 14
            wall.append((_clamp8(v * 0.99), _clamp8(v), _clamp8(v * 1.03)))

    # --- accent: near-black with faint vertical conduit lines + nodes,
    # added back as a scrolling, section-tinted glow on walls ---
    accent = []
    cols = (10, 30, 52)
    for y in range(n):
        for x in range(n):
            v = 4
            for cx in cols:
                if abs(x - cx) <= 1:
                    v = 58                         # conduit line
                if x == cx and (y % 16) in (0, 1):
                    v = 120                        # bright node
            if y % 32 == 16:                       # faint cross-tie
                v = max(v, 26)
            accent.append((v, _clamp8(v * 1.05), _clamp8(v * 1.15)))

    # --- void: a digital lattice whose luminance modulates the red void
    # plane — bright grid edges + scattered "data" cells + dark gaps, so
    # scroll+turb in the shader make it churn like a dissolving kill-field ---
    vrng = random.Random(0x07A1D)
    filled = {(vrng.randrange(4), vrng.randrange(4)) for _ in range(5)}
    dark = {(vrng.randrange(4), vrng.randrange(4)) for _ in range(4)}
    voidtex = []
    for y in range(n):
        for x in range(n):
            cell = (x // 16, y // 16)
            v = 92
            if cell in dark:
                v = 40
            elif cell in filled:
                v = 150
            if x % 16 < 2 or y % 16 < 2:           # bright lattice edges
                v = 220
            if (x % 16) in (7, 8) and (y % 16) in (7, 8):
                v = max(v, 180)                    # cell core pip
            v += noise[y * n + x]
            voidtex.append((_clamp8(v), _clamp8(v), _clamp8(v)))

    tex = {
        "textures/strafe64/d_floor.tga": _tga32(n, n, floor),
        "textures/strafe64/d_wall.tga": _tga32(n, n, wall),
        "textures/strafe64/accent.tga": _tga32(n, n, accent),
        "textures/strafe64/void_hex.tga": _tga32(n, n, voidtex),
        "textures/strafe64/sky_stars.tga": _build_starfield(),
    }
    tex.update(_build_synthsky())   # 90s geometric-landscape skybox (6 faces)
    return tex


def _build_starfield():
    """Seamless 128x128 star/nebula tile for the sky dome.

    A deep-space blue floor (never pure black) + a multi-octave periodic
    (so it wraps) blue/magenta nebula + a dense star layer, some amber/cyan,
    the brightest with a soft glow. Two of these scrolling at different
    scales make a parallax space backdrop, matched to the NERV palette.
    """
    rng = random.Random(0x5A17)
    n = 128
    px = []
    for y in range(n):
        for x in range(n):
            u = 2.0 * math.pi * x / n
            v = 2.0 * math.pi * y / n
            # multi-octave periodic nebula (each term wraps over the tile)
            neb = (0.60 * math.sin(u) * math.sin(v * 0.5)
                   + 0.40 * math.sin(u * 2.0 + 1.3) * math.sin(v + 0.5)
                   + 0.28 * math.sin(u * 3.0 - 0.7) * math.sin(v * 2.0 + 2.1))
            neb = max(0.0, neb)
            # a second field marks where the nebula glows magenta
            mag = max(0.0, math.sin(u + 2.0) * math.sin(v * 1.5 - 1.0))
            # deep-space floor: a faint blue, so the void never reads black
            r = 9 + int(20 * neb + 48 * neb * mag)
            g = 11 + int(16 * neb)
            b = 24 + int(74 * neb)
            px.append([_clamp8(r), _clamp8(g), _clamp8(b)])

    def splat(i, col, spread):
        x0, y0 = i % n, i // n
        for dy in range(-spread, spread + 1):
            for dx in range(-spread, spread + 1):
                d = dx * dx + dy * dy
                if d > spread * spread:
                    continue
                f = 1.0 - math.sqrt(d) / (spread + 1)
                j = ((y0 + dy) % n) * n + ((x0 + dx) % n)
                px[j] = [min(255, px[j][k] + int(col[k] * f)) for k in range(3)]

    # dense star layer
    for _ in range(320):
        i = rng.randrange(n * n)
        m = rng.randint(110, 255)
        tint = rng.random()
        if tint < 0.16:        # amber star
            col = (m, int(m * 0.78), int(m * 0.42))
        elif tint < 0.32:      # cyan star
            col = (int(m * 0.55), m, m)
        else:                  # white/blue star
            col = (m, m, min(255, m + 14))
        px[i] = [max(px[i][k], col[k]) for k in range(3)]
        if m > 200:            # soft glow on the brightest
            splat(i, [c // 3 for c in col], 2 if m > 235 else 1)
    return _tga32(n, n, [tuple(p) for p in px])


# the skybox is map-independent — build the six faces once per process
_SYNTHSKY_CACHE = None


def _build_synthsky(n=256):
    """Six-face procedural skybox — a 90s vector / synthwave geometric landscape.

    Every face samples one direction->colour function, so the cube is seamless
    by construction (a shared edge resolves to the same world direction on both
    faces). Above the horizon: an Outrun sunset gradient, a big horizontal-
    scanline sun, and a jagged neon ridgeline. Below it: the iconic perspective
    neon grid receding into the horizon glow. Pure vector-era look, friendly to
    the PSX point-sampling preset. Emits textures/strafe64/env/synth_<side>.tga
    for side in rt/lf/ft/bk/up/dn (the Q3 skyparms box).
    """
    global _SYNTHSKY_CACHE
    if _SYNTHSKY_CACHE is not None:
        return _SYNTHSKY_CACHE

    SUN_EL, SUN_R = 0.27, 0.40
    sun = (math.cos(SUN_EL), 0.0, math.sin(SUN_EL))          # low, toward +x
    cos_sun_r = math.cos(SUN_R)
    SKY = [(0.00, (255, 146, 46)), (0.10, (255, 80, 120)), (0.30, (150, 44, 132)),
           (0.60, (52, 26, 96)), (1.00, (12, 8, 34))]        # horizon -> zenith
    SUNC = [(0.0, (255, 250, 235)), (0.5, (255, 176, 70)), (1.0, (255, 70, 150))]

    def grad(stops, t):
        if t <= stops[0][0]:
            return list(stops[0][1])
        for i in range(1, len(stops)):
            if t <= stops[i][0]:
                a, ca = stops[i - 1]
                b, cb = stops[i]
                f = (t - a) / (b - a)
                return [ca[k] + (cb[k] - ca[k]) * f for k in range(3)]
        return list(stops[-1][1])

    def tri(p):                                              # triangle, 0..1
        x = (p / (2.0 * math.pi)) % 1.0
        return 1.0 - abs(2.0 * x - 1.0)

    def ridge(az):                                           # jagged peaks (rad)
        return (0.085 * tri(3 * az + 0.4) + 0.045 * tri(7 * az + 1.7)
                + 0.028 * tri(17 * az + 0.9) + 0.016 * tri(29 * az + 2.2))

    def fline(v):                                            # dist to integer
        return abs(v - round(v))

    def color(dx, dy, dz):
        el = dz
        if el < 0.0:                                         # neon grid floor
            t = -1.0 / el
            gx, gy = dx * t, dy * t
            fade = 1.0 / (1.0 + 0.06 * t)
            lw = 0.02 + 0.12 * (1.0 - fade)                 # widen near horizon
            d = min(fline(gx * 0.5), fline(gy * 0.5))
            g = max(0.0, 1.0 - d / lw) * fade
            ln = (255, 60, 170) if (round(gx) + round(gy)) % 2 == 0 else (70, 230, 235)
            r, gg, b = 16 + ln[0] * g, 8 + ln[1] * g, 32 + ln[2] * g
            if el > -0.05:                                   # horizon glow
                hg = 1.0 + el / 0.05
                r += 110 * hg
                gg += 200 * hg
                b += 205 * hg
            return (_clamp8(r), _clamp8(gg), _clamp8(b))

        az = math.atan2(dy, dx)
        c = grad(SKY, math.sqrt(el))

        mh = ridge(az)                                       # mountain silhouette
        if el <= mh:
            if mh - el < 0.006:
                return (90, 255, 235)                        # glowing crest wire
            sh = 0.35 + 0.65 * (el / mh)
            c = [18 * sh + 10, 10 * sh + 6, 30 * sh + 16]
            if fline((mh - el) * 60.0) < 0.10:              # contour wireframe
                c = [c[0] + 26, c[1] + 80, c[2] + 80]

        cosang = dx * sun[0] + dy * sun[1] + dz * sun[2]
        if cosang > cos_sun_r and el >= mh:                 # scanline sun
            ang = math.acos(max(-1.0, min(1.0, cosang)))
            band = (el - (SUN_EL - SUN_R)) / (2.0 * SUN_R)
            stripe = (SUN_EL - el) * 26.0
            gap = band < 0.60 and (stripe - math.floor(stripe)) < 0.35 + 0.5 * (0.60 - band)
            if not gap:
                c = grad(SUNC, ang / SUN_R)

        if el > 0.35:                                        # sparse high stars
            hx, hy = int((az + math.pi) * 240.0), int(el * 240.0)
            if ((hx * 73856093) ^ (hy * 19349663)) & 1023 == 0:
                c = [c[0] + 170, c[1] + 170, c[2] + 190]

        return (_clamp8(c[0]), _clamp8(c[1]), _clamp8(c[2]))

    # right-handed orthonormal basis per face: dir = F + sx*R + sy*U, with sx
    # left->right [-1,1] and sy bottom->top. All sides share U=+z so the horizon
    # ring is level and seamless across rt/lf/ft/bk.
    faces = {
        "rt": ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
        "lf": ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
        "ft": ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
        "bk": ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
        "up": ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        "dn": ((0, 0, -1), (1, 0, 0), (0, -1, 0)),
    }
    out = {}
    for side, (F, R, U) in faces.items():
        px = []
        for y in range(n):
            sy = 1.0 - 2.0 * y / (n - 1)
            for x in range(n):
                sx = 2.0 * x / (n - 1) - 1.0
                vx = F[0] + sx * R[0] + sy * U[0]
                vy = F[1] + sx * R[1] + sy * U[1]
                vz = F[2] + sx * R[2] + sy * U[2]
                inv = 1.0 / math.sqrt(vx * vx + vy * vy + vz * vz)
                px.append(color(vx * inv, vy * inv, vz * inv))
        out["textures/strafe64/env/synth_%s.tga" % side] = _tga32(n, n, px)
    _SYNTHSKY_CACHE = out
    return out


# section palettes (N64 clarity: read the mechanic from the color at speed)
PAL_START  = (150, 255, 150)
PAL_GAPS   = (255, 215, 140)
PAL_BHOP   = (140, 225, 255)
PAL_SLIDE  = (255, 150, 150)
PAL_WALLS  = (225, 150, 255)
PAL_TOWER  = (255, 255, 150)
PAL_FINISH = (150, 255, 220)
PAL_PLAIN  = (205, 205, 205)
PAL_CHECK  = (120, 240, 255)   # checkpoint pads: bright cyan, reads at speed

# arena palettes
PAL_FLOORA = (185, 200, 185)
PAL_BANK   = (150, 175, 255)
PAL_WALLA  = (125, 125, 145)
PAL_CENTER = (255, 215, 140)
PAL_PILLAR = (225, 150, 255)
PAL_PAD    = (255, 255, 150)
PAL_LEDGE  = (150, 255, 220)
PAL_GATE   = (255, 245, 120)
PAL_DANGER = (255, 95, 95)

# music lanes (see ART_DIRECTION.md) — tracker modules in baseoa/music/.
# A map's worldspawn "music" key is picked deterministically from its seed so
# the same seed always gets the same tune. Courses ride jungle; combat arenas
# ride breakcore. (Liquid/intelligent DnB tunes fold into the jungle pool.)
MUSIC_JUNGLE = [
    "music/jungle_living-in-jungle.it",
    "music/jungle_da-jungle-is-wicked.mod",
    "music/jungle_industry.xm",
    "music/jungle_jbird.xm",
    "music/jungle_versatile.mod",
    "music/dnb_do-ya-like-jungle.xm",
    "music/dnb_my-way.xm",
    "music/dnb_unity.xm",
    "music/dnb_punnik.mod",
    "music/dnb_base-and-drums.mod",
]
MUSIC_BREAKCORE = [
    "music/breakcore_breakadawn.xm",
    "music/breakcore_nuodata.xm",
    "music/breakcore_densha.xm",
    "music/breakcore_breaking-the-sky.xm",
    "music/breakcore_cvrtsh.xm",
    "music/breakcore_rhythmic-resource.xm",
    "music/dnb_dj-krle-drum-n-bass.xm",
]
MUSIC_AMBIENT = [
    "music/ambient_atmosphere.xm",
    "music/ambient_energy.xm",
    "music/ambient_ideas.xm",
    "music/ambient_universal-resonance.xm",
]


def pick_music(lane, seed):
    """Deterministic per-seed track choice (stable across runs, unlike hash())."""
    idx = sum(ord(c) for c in str(seed)) % len(lane)
    return lane[idx]


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
# course generation
# ======================================================================
# dojo recipes: fixed section sequences that each isolate one archetype of
# the gameplay we want, for the bot-playtest dojo. Straight (no turns) so the
# metric reflects the mechanic, not navigation noise.
DOJO_RECIPES = {
    "speed":  ["sec_gaps", "sec_gaps", "sec_gaps"],          # strafe-accel lane
    "flow":   ["sec_bhop", "sec_slide", "sec_walls",
               "sec_bhop", "sec_slide", "sec_walls"],         # chain everything
    "ztrick": ["sec_gaps", "sec_bhop", "sec_gaps", "sec_bhop"],  # gaps + bhop
    # (was gaps+double-jump-tower; bots stall climbing the tower — iter12)
    # "arena" is built from the Arena class, not a Course recipe
    # isolation dojos for the new clustertruck/trackmania sections — each
    # repeats one archetype so bot telemetry attributes cleanly to it
    "slalom":  ["sec_slalom", "sec_slalom", "sec_slalom"],
    "hurdles": ["sec_hurdles", "sec_hurdles", "sec_hurdles"],
    "hazard":  ["sec_hazard", "sec_hazard", "sec_hazard"],
    "movers":  ["sec_movers", "sec_movers", "sec_movers"],
    "fork":    ["sec_fork", "sec_fork"],
    # a full representative mix, bait-driven so a lone bot actually runs it
    # end-to-end — the integration test for the "fun course" recipe (tower
    # omitted: bots are known to stall climbing it, see ztrick note)
    # a full representative mix in the fun arc (speed -> flow -> spice),
    # bait-driven so a lone bot runs it end-to-end — the integration test for
    # the recipe build() lays down (tower omitted: bots stall climbing it)
    "showcase": ["sec_gaps", "sec_bhop", "sec_fork", "sec_slalom", "sec_slide",
                 "sec_hurdles", "sec_walls", "sec_movers", "sec_hazard"],
}


class Course:
    def __init__(self, seed, difficulty, length, void=True,
                 voidrise=None, voiddelay=None, recipe=None):
        self.rng = random.Random(seed)
        self.seed = seed
        self.diff = difficulty
        self.recipe = recipe        # dojo section sequence, or None for normal
        # gap scale: easy courses shrink jumps, hard ones stretch them
        self.scale = (0.85, 1.0, 1.12)[difficulty]
        self.length = length
        self.void = void
        self.voidrise = voidrise        # ups/s override, None = derive
        self.voiddelay = voiddelay      # seconds override, None = derive
        self.solids = []        # world brushes
        self.triggers = []      # (brush, entity-dict) -> nodraw submodels
        self.movers = []        # (brush, entity-dict) -> *drawn* submodels (func_bobbing)
        self.entities = []
        self.sections = []      # manifest for the report
        self.checkpoints = []   # respawn points, one per section (see below)
        # cursor: position is the center of the *front edge* of the last
        # platform top; dir is the axis-aligned travel direction
        self.pos = [0.0, 0.0, 0.0]
        self.dir = (0, 1)
        self.lowest = 0.0

    # ---- cursor helpers ------------------------------------------------
    def right(self):
        return (self.dir[1], -self.dir[0])

    def advance(self, dist, side=0.0, dz=0.0):
        d, r = self.dir, self.right()
        self.pos[0] += d[0] * dist + r[0] * side
        self.pos[1] += d[1] * dist + r[1] * side
        self.pos[2] += dz
        self.lowest = min(self.lowest, self.pos[2])

    def yaw(self):
        return {(0, 1): 90, (1, 0): 0, (-1, 0): 180, (0, -1): 270}[self.dir]

    def turn(self, leftright):
        d = self.dir
        self.dir = (-d[1] * leftright, d[0] * leftright)

    def world_rect(self, fwd_len, half_w):
        """Axis box footprint for a platform starting at the cursor."""
        d, r = self.dir, self.right()
        x0, y0 = self.pos[0], self.pos[1]
        xs = [x0 + r[0] * half_w, x0 - r[0] * half_w,
              x0 + d[0] * fwd_len + r[0] * half_w,
              x0 + d[0] * fwd_len - r[0] * half_w]
        ys = [y0 + r[1] * half_w, y0 - r[1] * half_w,
              y0 + d[1] * fwd_len + r[1] * half_w,
              y0 + d[1] * fwd_len - r[1] * half_w]
        return (min(xs), min(ys)), (max(xs), max(ys))

    def platform(self, fwd_len, half_w, palette, thick=24.0, tex=TEX_FLOOR):
        (x0, y0), (x1, y1) = self.world_rect(fwd_len, half_w)
        top = self.pos[2]
        b = make_box((x0, y0, top - thick), (x1, y1, top),
                     tex=tex, palette=palette)
        self.solids.append(b)
        self.advance(fwd_len)
        return b

    def gap(self, dist, assumed_speed, dz=0.0):
        """Move the cursor across a jump gap, asserting solvability."""
        limit = SAFETY * jump_range(assumed_speed)
        if dz < 0:
            # falling extends airtime: t = (vz + sqrt(vz^2 + 2g·h)) / g
            t = (JUMP_VELOCITY + math.sqrt(JUMP_VELOCITY ** 2
                                           + 2 * GRAVITY * -dz)) / GRAVITY
            limit = SAFETY * assumed_speed * t
        assert dist <= limit, (
            f"unreachable gap {dist:.0f} > {limit:.0f} "
            f"(speed {assumed_speed:.0f}, dz {dz:.0f})")
        self.advance(dist, dz=dz)

    # ---- sections ------------------------------------------------------
    def sec_start(self):
        self.sections.append(("start", {}))
        d = self.dir
        cx, cy = self.pos[0] + d[0] * 192, self.pos[1] + d[1] * 192
        top = self.pos[2]
        (sx0, sy0), (sx1, sy1) = self.world_rect(448, 224)
        self.platform(448, 224, PAL_START, thick=32)
        # the race clock restamps while you stand here and starts ticking
        # the moment you leave the pad
        tb = make_box((sx0, sy0, top), (sx1, sy1, top + 128),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_race_start"}))
        for i, off in enumerate((-120, -40, 40, 120)):
            r = self.right()
            self.entities.append({
                "classname": "info_player_deathmatch",
                "origin": f"{cx + r[0] * off - d[0] * 96:g} "
                          f"{cy + r[1] * off - d[1] * 96:g} {top + 40:g}",
                "angle": str(self.yaw()),
            })
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": "start_dest",
            "origin": f"{cx:g} {cy:g} {top + 40:g}",
            "angle": str(self.yaw()),
        })
        self.entities.append({
            "classname": "info_player_intermission",
            "origin": f"{cx:g} {cy:g} {top + 360:g}",
            "angle": str(self.yaw()),
        })

    def sec_gaps(self, n=None):
        n = n or self.rng.randint(5, 7)
        self.sections.append(("strafe gaps", {"count": n}))
        speed = RUN_SPEED + 20.0  # cold entry, slight bhop carry
        for i in range(n):
            g = min((150 + 14 * i) * self.scale,
                    SAFETY * jump_range(speed) - 8)
            self.gap(g, speed, dz=-8)
            side = self.rng.uniform(-90, 90) * self.scale
            self.advance(0, side=side)
            size = max(64.0, (96 - 4 * i) * (1.1 - 0.1 * self.diff))
            self.platform(size * 2, size, PAL_GAPS)
            speed = min(speed + 18, 430)  # chained hops build speed

    def sec_bhop(self, n=None):
        n = n or self.rng.randint(8, 11)
        self.sections.append(("bhop lane", {"count": n}))
        for i in range(n):
            g = min((100 + 4 * i) * self.scale, SAFETY * jump_range(RUN_SPEED))
            self.gap(g, RUN_SPEED)  # solvable even with a dead chain
            self.advance(0, side=self.rng.uniform(-40, 40))
            self.platform(112, 60, PAL_BHOP, thick=16)

    def sec_slide(self):
        self.sections.append(("slide ramp", {}))
        self.platform(256, 128, PAL_SLIDE)
        # ramp: top surface drops `h` over `run` — crouch at the crest,
        # slide friction (pm_slideFrictionScale) keeps nearly all of it
        run, h = 512.0, 160.0
        (x0, y0), (x1, y1) = self.world_rect(run, 128)
        top = self.pos[2]
        d = self.dir
        far = {  # top corner ids at the far end of the travel axis
            (0, 1): (6, 7), (0, -1): (4, 5),
            (1, 0): (5, 7), (-1, 0): (4, 6),
        }[d]
        self.solids.append(make_box(
            (x0, y0, top - h - 32), (x1, y1, top),
            tex=TEX_FLOOR, palette=PAL_SLIDE,
            top_drop={far[0]: h, far[1]: h}))
        self.advance(run, dz=-h)
        # slide-jump across the big gap; ~70% of the potential energy of
        # the drop survives friction, then pm_slideJumpBoost kicks
        v_exit = math.sqrt(RUN_SPEED ** 2 + 1.4 * GRAVITY * h) * SLIDE_JUMP
        g = min(260 * self.scale, SAFETY * jump_range(v_exit))
        self.gap(g, v_exit, dz=-48)
        self.platform(288, 128, PAL_SLIDE)

    def sec_walls(self):
        self.sections.append(("walljump hall", {}))
        self.platform(192, 96, PAL_WALLS)
        # hall width derives from wall-jump reach so it is always crossable
        # by wall-jumps alone (wall-running, added to the moveset, only makes
        # it easier). Harder difficulties sit closer to the limit.
        kick_t = 2.0 * WALLJUMP_VZ / GRAVITY
        reach = SAFETY * (jump_range(330) + WALLJUMP_MAX * 330 * kick_t)
        hall = int(reach * (0.88, 0.94, 0.99)[self.diff]) // 8 * 8
        assert hall <= reach, f"hall {hall} > walljump reach {reach:.0f}"
        (x0, y0), (x1, y1) = self.world_rect(hall, 144)
        base = self.pos[2]
        d, r = self.dir, self.right()
        # two flanking walls, no floor between them; inner faces sit at
        # the hall edge (offset 160 - half thickness 16 = 144)
        half_len = (y1 - y0) / 2 if d[0] == 0 else (x1 - x0) / 2
        for side in (-1, 1):
            cxa = (x0 + x1) / 2 + r[0] * side * 160
            cya = (y0 + y1) / 2 + r[1] * side * 160
            wall_mins = (cxa - (16 if d[0] == 0 else half_len),
                         cya - (half_len if d[0] == 0 else 16),
                         base - 64)
            wall_maxs = (cxa + (16 if d[0] == 0 else half_len),
                         cya + (half_len if d[0] == 0 else 16),
                         base + 288)
            self.solids.append(make_box(
                wall_mins, wall_maxs, tex=TEX_WALL, palette=PAL_WALLS))
        if self.diff < 2:
            mid_save = (self.pos[0], self.pos[1], self.pos[2])
            self.advance(hall / 2 - 56)
            self.platform(112, 64, PAL_WALLS, thick=16)
            self.pos = [mid_save[0], mid_save[1], mid_save[2]]
        self.advance(hall, dz=32)
        self.platform(224, 96, PAL_WALLS)

    def sec_tower(self, n=4):
        self.sections.append(("double-jump tower", {"steps": n}))
        rise = 68.0  # > LEDGE_SJ (63.6) so it forces the double jump,
        assert LEDGE_SJ < rise <= LEDGE_DJ - 12
        for _ in range(n):
            self.advance(0, dz=rise)
            self.platform(176, 112, PAL_TOWER, thick=rise + 24)

    def sec_slalom(self, n=None):
        """Wide deck studded with staggered pillars — weave through at speed.
        Pure run (no jumps), so it always solves; the line you pick through
        the columns is the skill, trackmania-style."""
        n = n or self.rng.randint(5, 8)
        self.sections.append(("slalom", {"pillars": n}))
        half_w = 240.0 * (1.0 + 0.05 * self.diff)
        seg = 256.0
        length = seg * (n + 1)
        (x0, y0), (x1, y1) = self.world_rect(length, half_w)
        top = self.pos[2]
        self.solids.append(make_box((x0, y0, top - 24), (x1, y1, top),
                                    tex=TEX_FLOOR, palette=PAL_GAPS))
        d, r = self.dir, self.right()
        ph = 34.0          # slimmer pillars: a clear racing line always stays
        for i in range(n):
            f = seg * (i + 1)
            # alternate sides; harder difficulties push pillars toward the
            # centre line so the gaps to thread are tighter
            side = (1 if i % 2 == 0 else -1) * self.rng.uniform(
                95, 175 - 30 * self.diff)
            cx = self.pos[0] + d[0] * f + r[0] * side
            cy = self.pos[1] + d[1] * f + r[1] * side
            self.solids.append(make_box(
                (cx - ph, cy - ph, top), (cx + ph, cy + ph, top + 200),
                tex=TEX_WALL, palette=PAL_PILLAR))
        self.advance(length)

    def sec_hurdles(self, n=None):
        """A wide lane crossed by low walls, each with an alternating-side gap.
        Two ways through every hurdle: vault the wall straight (hold the line)
        or cut to the open gap (weave). The gap also gives bot nav a walkable
        route, so the section flows for AI and humans alike."""
        n = n or self.rng.randint(4, 6)
        self.sections.append(("hurdles", {"count": n}))
        half_w = 200.0
        seg = 220.0
        gap_w = 150.0
        length = seg * (n + 1)
        (x0, y0), (x1, y1) = self.world_rect(length, half_w)
        top = self.pos[2]
        self.solids.append(make_box((x0, y0, top - 24), (x1, y1, top),
                                    tex=TEX_FLOOR, palette=PAL_BHOP))
        d, r = self.dir, self.right()
        hh = 32.0 + 6.0 * self.diff          # < JUMP_APEX (45.6u), clearable
        assert hh < JUMP_APEX
        for i in range(n):
            f = seg * (i + 1)
            cx = self.pos[0] + d[0] * f
            cy = self.pos[1] + d[1] * f
            gc = (1 if i % 2 == 0 else -1) * (half_w * 0.5)   # gap side alternates
            g0, g1 = gc - gap_w / 2, gc + gap_w / 2
            for a, b in ((-half_w, g0), (g1, half_w)):        # wall on each side of gap
                if b - a < 24:
                    continue
                p1x, p1y = cx + r[0] * a + d[0] * -14, cy + r[1] * a + d[1] * -14
                p2x, p2y = cx + r[0] * b + d[0] * 14, cy + r[1] * b + d[1] * 14
                self.solids.append(make_box(
                    (min(p1x, p2x), min(p1y, p2y), top),
                    (max(p1x, p2x), max(p1y, p2y), top + hh),
                    tex=TEX_WALL, palette=PAL_WALLS))
        self.advance(length)
        self.platform(160, half_w, PAL_BHOP)

    def sec_hazard(self, n=None):
        """"The floor is lava" — wide pads separated by red lethal-looking gaps.
        A missed leap is NOT an instakill (a trigger_hurt just respawns a runner
        at the start and, for a bot, loops it there forever); instead the fall
        drops into the course's own void/rescue, the same hostile floor the
        whole game is built on — you lose the line, eat the reset, and the
        rising void gains on you. Gaps are comfortable and slightly downhill so
        a clean run flows; the danger reads through the red pit and lip stripes."""
        n = n or self.rng.randint(3, 5)
        self.sections.append(("hazard pits", {"pits": n}))
        half_w = 168.0
        speed = RUN_SPEED + 60.0
        self.platform(224, half_w, PAL_BHOP)       # long take-off pad: room to line up
        for i in range(n):
            # comfortable gaps, slightly downhill (like sec_gaps, which bots
            # clear): the drop buys airtime so a clean jump lands easily
            dz = -28.0
            pit = min((130 + 6 * i) * self.scale,
                      SAFETY * jump_range(speed) - 48)
            (px0, py0), (px1, py1) = self.world_rect(pit, half_w)
            d = self.dir
            zt = self.pos[2]
            # red rails lining the pit's lateral edges: reads as a lava trench
            # from a run. They sit just below the lip (no jump obstruction) and
            # leave the floor open to the void (a fall is caught by the course
            # rescue — no instakill that would loop a bot at the start).
            for s in (0, 1):
                if d[0] == 0:                # travel ±y: rails at x = px0 / px1
                    wx = px0 if s == 0 else px1 - 24
                    rmins, rmaxs = (wx, py0, zt - 64), (wx + 24, py1, zt - 8)
                else:                        # travel ±x: rails at y = py0 / py1
                    wy = py0 if s == 0 else py1 - 24
                    rmins, rmaxs = (px0, wy, zt - 64), (px1, wy + 24, zt - 8)
                self.solids.append(make_box(rmins, rmaxs, tex=TEX_WALL, palette=PAL_DANGER))
            # red warning stripe across the take-off lip — danger reads at speed
            lx = self.pos[0] - d[0] * 20
            ly = self.pos[1] - d[1] * 20
            z = self.pos[2]
            if d[0] == 0:                    # travelling ±y: stripe spans x
                smins, smaxs = (lx - half_w, ly - 12, z - 2), (lx + half_w, ly + 12, z + 3)
            else:                            # travelling ±x: stripe spans y
                smins, smaxs = (lx - 12, ly - half_w, z - 2), (lx + 12, ly + half_w, z + 3)
            self.solids.append(make_box(smins, smaxs, tex=TEX_FLOOR, palette=PAL_DANGER))
            self.gap(pit, speed, dz=dz)
            self.platform(224, half_w, PAL_BHOP)
            speed = min(speed + 8, 430)

    def sec_movers(self, n=None):
        """Clustertruck moving platforms. The through-line is static stepping
        stones (a guaranteed jump path — bots can't path onto a func_bobbing,
        and a trap would break every mixed course), and beside each stone a
        bobbing platform sits as the stylish human line. No item bait on the
        bobbers: an item there only drags bot nav toward a platform it can't
        stand on. Vertical bob and lateral slide alternate; amplitudes stay
        small so the hop on is always there."""
        n = n or self.rng.randint(3, 4)
        self.sections.append(("moving platforms", {"count": n}))
        half_w = 144.0
        stone_half = 96.0
        amp = 40.0
        speed = RUN_SPEED + 20.0
        self.platform(176, half_w, PAL_PILLAR)
        d, r = self.dir, self.right()
        for i in range(n):
            g = min(196.0 * self.scale, SAFETY * jump_range(speed) - 8)
            self.gap(g, speed)
            # static stepping stone on the main line (bot + human safe)
            self.platform(stone_half * 2, stone_half, PAL_PILLAR)
            # bobbing platform set far off the alternating side — vertical bob
            # only, so it never swings into the static line and crushes a runner
            # standing there (a lateral slider at 230u did exactly that). Far
            # enough out that bot nav never strays toward it.
            side = 1 if i % 2 == 0 else -1
            bcx = self.pos[0] - d[0] * stone_half + r[0] * side * 360
            bcy = self.pos[1] - d[1] * stone_half + r[1] * side * 360
            top = self.pos[2]
            b = make_box((bcx - 80, bcy - 80, top - 24), (bcx + 80, bcy + 80, top),
                         tex=TEX_FLOOR, palette=PAL_PAD)
            cycle = self.rng.uniform(2.4, 3.4)
            phase = round(self.rng.random(), 3)
            self.movers.append((b, {
                "classname": "func_bobbing",
                "height": f"{amp:g}", "speed": f"{cycle:g}",
                "spawnflags": "0", "phase": f"{phase:g}"}))
        self.platform(192, half_w, PAL_PILLAR)

    def sec_fork(self):
        """Trackmania-style split: the path forks into a narrow risky/fast
        lane (speed shards as a reward) and a wide safe lane, then both
        rejoin on a shared merge pad. Both lanes are asserted crossable."""
        self.sections.append(("fork", {}))
        self.platform(192, 220, PAL_GATE)        # fork pad: lanes diverge here
        base = list(self.pos)
        base_dir = self.dir
        d, r = self.dir, self.right()
        off = 280.0                              # lane centre-to-centre
        K, L, G = 4, 176.0, 170.0
        span = K * (G + L)
        speed = RUN_SPEED + 50.0

        def lane(sign, pal, lane_half, reward):
            self.pos = [base[0] + r[0] * sign * off,
                        base[1] + r[1] * sign * off, base[2]]
            self.dir = base_dir
            for _ in range(K):
                self.gap(G, speed)
                self.advance(0, side=self.rng.uniform(-24, 24))
                self.platform(L, lane_half, pal)
                if reward:
                    self.entities.append({
                        "classname": "item_armor_shard",
                        "origin": f"{self.pos[0] - d[0] * L / 2:g} "
                                  f"{self.pos[1] - d[1] * L / 2:g} "
                                  f"{self.pos[2] + 40:g}"})

        lane(+1, PAL_SLIDE, 72.0, True)          # right: narrow, fast, rewarded
        lane(-1, PAL_BHOP, 120.0, False)         # left: wide, safe
        # merge pad: spans both lanes (overlaps their ends) and rejoins the path
        self.pos = [base[0] + d[0] * (span - 40), base[1] + d[1] * (span - 40), base[2]]
        self.dir = base_dir
        merge_half = off + 120.0 + 64.0
        self.platform(220, merge_half, PAL_GATE)

    def sec_finish(self):
        self.sections.append(("finish gate", {}))
        self.platform(160, 160, PAL_FINISH, thick=32)
        d, r = self.dir, self.right()
        top = self.pos[2]
        # gate columns + crossbar
        for side in (-1, 1):
            cx = self.pos[0] + r[0] * side * 128
            cy = self.pos[1] + r[1] * side * 128
            self.solids.append(make_box(
                (cx - 24, cy - 24, top - 32), (cx + 24, cy + 24, top + 192),
                tex=TEX_WALL, palette=PAL_FINISH))
        (gx0, gy0), (gx1, gy1) = self.world_rect(48, 128)
        self.solids.append(make_box(
            (gx0, gy0, top + 192), (gx1, gy1, top + 224),
            tex=TEX_WALL, palette=PAL_FINISH))
        # the finish line and the lap teleporter share the arch
        fb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {
            "classname": "trigger_teleport",
            "target": "start_dest",
        }))
        self.platform(224, 160, PAL_FINISH, thick=32)

    def sec_lift(self, deck):
        """Deck-to-deck lift: an arch that teleports a full DECK_RISE up.

        Towers climb away from the rising void — every deck you clear is
        a deck the void gets to keep.
        """
        self.sections.append(("lift gate", {"to_deck": deck + 2}))
        self.platform(160, 160, PAL_TOWER, thick=32)
        d, r = self.dir, self.right()
        top = self.pos[2]
        for side in (-1, 1):
            cx = self.pos[0] + r[0] * side * 128
            cy = self.pos[1] + r[1] * side * 128
            self.solids.append(make_box(
                (cx - 24, cy - 24, top - 32), (cx + 24, cy + 24, top + 192),
                tex=TEX_WALL, palette=PAL_TOWER))
        (gx0, gy0), (gx1, gy1) = self.world_rect(48, 128)
        self.solids.append(make_box(
            (gx0, gy0, top + 192), (gx1, gy1, top + 224),
            tex=TEX_WALL, palette=PAL_TOWER))
        tb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {
            "classname": "trigger_teleport",
            "target": f"deck{deck}_dest",
        }))
        # the next deck starts directly past and above the gate
        self.advance(96, dz=DECK_RISE)
        dcx = self.pos[0] + d[0] * 192
        dcy = self.pos[1] + d[1] * 192
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": f"deck{deck}_dest",
            "origin": f"{dcx:g} {dcy:g} {self.pos[2] + 40:g}",
            "angle": str(self.yaw()),
        })
        self.platform(448, 224, PAL_START, thick=32)

    def maybe_turn(self):
        if self.rng.random() < 0.45:
            lr = self.rng.choice((-1, 1))
            self.gap(140 * self.scale, RUN_SPEED + 30, dz=-8)
            d = self.dir
            # square corner pad starting at the far side of the gap
            cx = self.pos[0] + d[0] * 112
            cy = self.pos[1] + d[1] * 112
            self.solids.append(make_box(
                (cx - 112, cy - 112, self.pos[2] - 24),
                (cx + 112, cy + 112, self.pos[2]),
                tex=TEX_FLOOR, palette=PAL_PLAIN))
            self.pos[0], self.pos[1] = cx, cy
            self.turn(lr)
            self.advance(112)
            self.sections.append(("turn", {"dir": "left" if lr > 0 else "right"}))

    def build(self):
        self.sec_start()
        self.set_checkpoint()
        # dojo: a fixed straight recipe isolating one archetype
        if self.recipe:
            self.waypoints = []
            for secname in self.recipe:
                getattr(self, secname)()
                self.waypoints.append(tuple(self.pos))   # path point per section
                self.set_checkpoint()
            self.sec_finish()
            self.waypoints.append(tuple(self.pos))   # bait the finish line so
                                                     # bots chase across it -> completion
            self.build_checkpoint_catches()
            self.add_void_and_sky()
            self._dojo_items()      # bait bots down the course so they run it
            self.entities.insert(0, self._dojo_worldspawn())
            return self
        # THE RECIPE — the fun arc, validated by the bot dojo (every tier flows
        # at 50-58% with ~1200-2100ms stuck, the shipped-archetype band):
        #   1. OPENERS build speed (gaps, bhop) — both, every deck
        #   2. a FORK gives the trackmania route choice — the centrepiece
        #   3. FLOW sections keep the chain alive (slide, walls, slalom) — 2 of 3
        #   4. SPICE is the technical/lethal climax once you're fast
        #      (hurdles, movers, hazard) — 2 of 3
        #   5. the dj TOWER climbs out (toward the lift / finish)
        # tiers stay in arc order; the picks within shuffle for variety. ~8
        # sections/deck (was 5) — bigger, branchier, paced.
        openers = [self.sec_gaps, self.sec_bhop]
        flow = [self.sec_slide, self.sec_walls, self.sec_slalom]
        spice = [self.sec_hurdles, self.sec_movers, self.sec_hazard]
        # length > 1 builds a tower: each deck runs the arc, ending in a lift up
        for deck in range(self.length):
            o = openers[:]
            self.rng.shuffle(o)
            deck_secs = (o + [self.sec_fork] + self.rng.sample(flow, 2)
                         + self.rng.sample(spice, 2) + [self.sec_tower])
            for sec in deck_secs:
                sec()
                self.maybe_turn()
                self.set_checkpoint()
            if deck < self.length - 1:
                self.sec_lift(deck)
                self.set_checkpoint()
        self.sec_finish()
        self.sections.append(("checkpoints", {"count": len(self.checkpoints)}))
        self.build_checkpoint_catches()
        self.add_void_and_sky()
        worldspawn = {
            "classname": "worldspawn",
            "message": f"STRAFE64 run {self.seed} "
                       f"(difficulty {self.diff}, length {self.length})",
            "music": pick_music(
                MUSIC_AMBIENT if not self.void else MUSIC_JUNGLE, self.seed),
        }
        if self.void:
            if self.voidrise is not None:
                rate = self.voidrise
            elif self.length > 1:
                rate = DECK_RISE / VOID_SECONDS_PER_DECK[self.diff]
            else:
                rate = VOID_FLAT_RATE[self.diff]
            delay = (self.voiddelay if self.voiddelay is not None
                     else VOID_DELAY[self.diff])
            worldspawn["voidbase"] = f"{self.void_floor:g}"
            worldspawn["voidrise"] = f"{rate:g}"
            worldspawn["voiddelay"] = f"{delay:g}"
        self.entities.insert(0, worldspawn)
        return self

    def _dojo_place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def _dojo_items(self):
        # DM bots roam toward items/enemies, not race finishes — so a bare
        # course gives them no reason to move. Drop a pickup on each section's
        # platform: the bot AI paths to the next item and runs the course as a
        # side effect. Weapons/armor are the strongest bot magnets.
        bait = ["weapon_railgun", "item_armor_combat", "item_health_large",
                "item_armor_body", "item_health"]
        for i, (x, y, z) in enumerate(self.waypoints):
            self._dojo_place(bait[i % len(bait)], x, y, z + 24)

    def _dojo_worldspawn(self):
        ws = {
            "classname": "worldspawn",
            "message": "STRAFE64 dojo: " + "/".join(self.recipe),
        }
        if self.void:
            rate = (self.voidrise if self.voidrise is not None
                    else VOID_FLAT_RATE[self.diff])
            delay = (self.voiddelay if self.voiddelay is not None
                     else VOID_DELAY[self.diff])
            ws["voidbase"] = f"{self.void_floor:g}"
            ws["voidrise"] = f"{rate:g}"
            ws["voiddelay"] = f"{delay:g}"
        return ws

    # ---- checkpoints ---------------------------------------------------
    def set_checkpoint(self):
        """Drop a respawn point + visible pad at the current cursor.

        Falls over the geometry built *after* this call are caught and
        teleported back here (build_checkpoint_catches), so a missed jump
        costs the player the current section, not the whole run. The void
        still rises, so checkpoints forgive execution but never the clock.
        """
        d = self.dir
        name = f"cp{len(self.checkpoints)}"
        # a safe standing spot pulled back from the platform's front edge
        bx = self.pos[0] - d[0] * 64
        by = self.pos[1] - d[1] * 64
        bz = self.pos[2]
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": name,
            "origin": f"{bx:g} {by:g} {bz + 48:g}",
            "angle": str(self.yaw()),
        })
        # glowing checkpoint pad, flush on the platform
        self.solids.append(make_box(
            (bx - 48, by - 48, bz - 2), (bx + 48, by + 48, bz + 4),
            tex=TEX_FLOOR, palette=PAL_CHECK))
        # idx marks where this section's geometry begins (the pad above is
        # tiny and belongs to the previous segment)
        self.checkpoints.append({"name": name, "idx": len(self.solids)})

    def build_checkpoint_catches(self):
        """One rescue slab per section, under its footprint, → its entrance.

        Sized to the bounding box of the solids built between consecutive
        checkpoints; a short drop below the section's lowest floor so a
        fall reads as a quick reset rather than a long plummet.
        """
        cps = self.checkpoints
        for i, cp in enumerate(cps):
            lo = cp["idx"]
            hi = cps[i + 1]["idx"] if i + 1 < len(cps) else len(self.solids)
            seg = self.solids[lo:hi]
            if not seg:
                continue
            xs = [b.mins[0] for b in seg] + [b.maxs[0] for b in seg]
            ys = [b.mins[1] for b in seg] + [b.maxs[1] for b in seg]
            zmin = min(b.mins[2] for b in seg)
            m = 96.0
            cb = make_box(
                (min(xs) - m, min(ys) - m, zmin - 360),
                (max(xs) + m, max(ys) + m, zmin - 200),
                tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
            self.triggers.append((cb, {
                "classname": "trigger_teleport",
                "target": cp["name"],
            }))

    def add_void_and_sky(self):
        xs = [b.mins[0] for b in self.solids] + [b.maxs[0] for b in self.solids]
        ys = [b.mins[1] for b in self.solids] + [b.maxs[1] for b in self.solids]
        zs = [b.mins[2] for b in self.solids] + [b.maxs[2] for b in self.solids]
        m = 768.0
        x0, x1 = min(xs) - m, max(xs) + m
        y0, y1 = min(ys) - m, max(ys) + m
        z0, z1 = min(zs) - 896.0, max(zs) + 1024.0
        # the rising kill plane starts at the bottom of the pit, below the
        # rescue teleporter at z0+256 — early falls are forgiven, then the
        # void overtakes the rescue plane and falls become fatal
        self.void_floor = z0 + 64.0
        # falling into the void teleports you home — keeps the run flowing
        vb = make_box((x0 + 8, y0 + 8, z0 + 256), (x1 - 8, y1 - 8, z0 + 320),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((vb, {
            "classname": "trigger_teleport",
            "target": "start_dest",
        }))
        self.solids += make_skybox(x0, y0, z0, x1, y1, z1)


# ======================================================================
# arena generation: banked velodrome ring around an item arena
# ======================================================================
class Arena:
    """Deathmatch bowl: a 16-segment banked velodrome rings an arena
    floor with a tiered center tower, pillars, jump pads and items.
    The bank is walkable (~30 deg < MIN_WALK_NORMAL limit) so bots can
    use it too; humans ride it at speed like a wall-of-death."""

    SEGS = 16

    def __init__(self, seed, difficulty):
        self.rng = random.Random(seed ^ 0x0A12E7A)
        self.seed = seed
        self.diff = difficulty
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def _pt(self, r, i):
        a = 2.0 * math.pi * i / self.SEGS
        return (r * math.cos(a), r * math.sin(a))

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def build(self):
        rng = self.rng
        R1 = 1280.0 + 64 * self.diff + rng.randrange(0, 257, 64)
        BANK_W = 384.0
        BANK_H = float(rng.randrange(192, 257, 16))  # 26-34 deg: ridable
        WALL_T = 64.0                   # AND bot-walkable (< 45.5 deg)
        WALL_H = BANK_H + 384.0
        R2 = R1 + BANK_W
        center = rng.choice(("tower", "crater"))
        self.sections.append(("arena floor", {"radius": int(R1)}))
        self.sections.append(("velodrome", {
            "segments": self.SEGS, "bank": f"{BANK_H:g}z/{BANK_W:g}u"}))

        def bank_z(r):
            return BANK_H * (r - R1) / BANK_W

        # floor: one convex prism
        foot = [self._pt(R1 + 8, i) for i in range(self.SEGS)]
        self.solids.append(make_prism(foot, -48, [0.0] * self.SEGS,
                                      palette=PAL_FLOORA))
        # banked ring + containment wall, segment by segment
        for i in range(self.SEGS):
            a0, a1 = self._pt(R1, i), self._pt(R1, i + 1)
            b0, b1 = self._pt(R2, i), self._pt(R2, i + 1)
            self.solids.append(make_prism(
                [a0, a1, b1, b0], -48, [0.0, 0.0, BANK_H, BANK_H],
                palette=PAL_BANK))
            w0, w1 = self._pt(R2 + WALL_T, i), self._pt(R2 + WALL_T, i + 1)
            self.solids.append(make_prism(
                [b0, b1, w1, w0], -48, [WALL_H] * 4,
                tex=TEX_WALL, palette=PAL_WALLA))

        # boost gates: trigger_push bands on the bank that throw riders a
        # segment forward along the lane — the loop is the fast way around
        gates = 3 + self.diff
        self.sections.append(("boost gates", {"count": gates}))
        seg_a = 2.0 * math.pi / self.SEGS
        g0 = rng.randrange(self.SEGS)
        Ra, Rb = R1 + 64.0, R2 - 64.0
        Rm = R1 + 0.45 * BANK_W
        for g in range(gates):
            seg = (g0 + g * self.SEGS // gates) % self.SEGS
            th = (seg + 0.5) * seg_a

            def lane_pt(r, dth):
                return (r * math.cos(th + dth), r * math.sin(th + dth))

            hw = seg_a * 0.18   # half the band's angular width
            za, zb = bank_z(Ra), bank_z(Rb)
            foot_g = [lane_pt(Ra, -hw), lane_pt(Ra, hw),
                      lane_pt(Rb, hw), lane_pt(Rb, -hw)]
            self.solids.append(make_prism(     # band sits 8u proud
                foot_g, -48, [za + 8, za + 8, zb + 8, zb + 8],
                palette=PAL_GATE))
            tb = make_prism(
                [lane_pt(Ra, -hw * 1.6), lane_pt(Ra, hw * 1.6),
                 lane_pt(Rb, hw * 1.6), lane_pt(Rb, -hw * 1.6)],
                za - 8, [zb + 128] * 4,
                tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER)
            self.triggers.append((tb, {
                "classname": "trigger_push",
                "target": f"gate_{g}",
            }))
            # apex one segment counter-clockwise along the lane
            tx, ty = lane_pt(Rm, 1.2 * seg_a)
            self.place("target_position", tx, ty, bank_z(Rm) + 224,
                       targetname=f"gate_{g}")
        # armor-shard speed line rewards staying in the loop
        for i in range(0, self.SEGS, 2):
            a = (i + 0.5) * seg_a
            self.place("item_armor_shard", Rm * math.cos(a),
                       Rm * math.sin(a), bank_z(Rm) + 24)

        # velodrome lap race: a start/finish line across the lane wires the
        # arena into the game's race layer (HUD timer + ghosts). Bots ignore
        # trigger_race_*, so deathmatch is untouched. The boost gates flow
        # counter-clockwise, so a lap runs CCW: cross start, ride ~340 deg,
        # cross finish (one seam behind start), and start re-stamps for the
        # next lap. Pick a seam clear of boost-gate bands for a clean line.
        gate_segs = {(g0 + g * self.SEGS // gates) % self.SEGS
                     for g in range(gates)}
        s_seam = next(
            (s for s in range(self.SEGS)
             if s not in gate_segs and (s - 1) % self.SEGS not in gate_segs),
            0)
        self.sections.append(("lap line", {"seam": s_seam}))

        def lap_line(seam, classname, palette):
            th = seam * seg_a

            def lp(r, dth):
                return (r * math.cos(th + dth), r * math.sin(th + dth))

            hw = seg_a * 0.10
            za, zb = bank_z(Ra), bank_z(Rb)
            self.solids.append(make_prism(   # painted stripe on the bank
                [lp(Ra, -hw), lp(Ra, hw), lp(Rb, hw), lp(Rb, -hw)],
                -48, [za + 2, za + 2, zb + 2, zb + 2], palette=palette))
            tw = seg_a * 0.16
            tb = make_prism(                 # full-lane-height race trigger
                [lp(R1, -tw), lp(R1, tw), lp(R2, tw), lp(R2, -tw)],
                -48, [bank_z(R1) + 220, bank_z(R1) + 220,
                      bank_z(R2) + 220, bank_z(R2) + 220],
                tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER)
            self.triggers.append((tb, {"classname": classname}))

        lap_line(s_seam, "trigger_race_start", PAL_START)
        lap_line((s_seam - 1) % self.SEGS, "trigger_race_finish", PAL_FINISH)

        # center structure, seeded: tiered tower or a quad crater.
        # rises are tuned to the movement mod: 56 single-jumpable,
        # 74 double-jump-only (LEDGE_SJ 63.6 < 74 < LEDGE_DJ 92.4)
        t1, t3 = 56.0, 176.0
        rim = 74.0
        assert LEDGE_SJ < rim < LEDGE_DJ
        if center == "tower":
            self.sections.append(("center tower", {"tiers": 3}))
            self.solids.append(make_box((-256, -256, 0), (256, 256, t1),
                                        palette=PAL_CENTER))
            self.solids.append(make_box((-160, -160, t1), (160, 160, 120),
                                        palette=PAL_CENTER))
            self.solids.append(make_box((-96, -96, 120), (96, 96, t3),
                                        palette=PAL_CENTER))
            self.place("item_quad", 0, 0, t3 + 24)
            self.place("item_health_large", 208, 208, t1 + 24)
            self.place("item_health_large", -208, -208, t1 + 24)
            apex_z = t3 + 256
        else:
            # crater: damage floor inside double-jump-only walls; the
            # quad sits on a safe pedestal — dive in, pay, climb out
            self.sections.append(("center crater", {"rim": int(rim)}))
            for mins, maxs in (((-288, 176), (288, 288)),
                               ((-288, -288), (288, -176)),
                               ((176, -176), (288, 176)),
                               ((-288, -176), (-176, 176))):
                self.solids.append(make_box(
                    (mins[0], mins[1], 0), (maxs[0], maxs[1], rim),
                    palette=PAL_CENTER))
            self.solids.append(make_box((-176, -176, 0), (176, 176, 8),
                                        palette=PAL_DANGER))
            self.solids.append(make_box((-64, -64, 8), (64, 64, 32),
                                        palette=PAL_LEDGE))
            self.place("item_quad", 0, 0, 56)
            for mins, maxs in (((-176, 80), (176, 176)),
                               ((-176, -176), (176, -80)),
                               ((80, -80), (176, 80)),
                               ((-176, -80), (-80, 80))):
                hb = make_box((mins[0], mins[1], 8), (maxs[0], maxs[1], 72),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER,
                              draw=set())
                self.triggers.append((hb, {
                    "classname": "trigger_hurt",
                    "dmg": "10", "spawnflags": "16",   # SLOW: 1 tick/sec
                }))
            self.place("item_health_large", 232, 232, rim + 24)
            self.place("item_health_large", -232, -232, rim + 24)
            apex_z = rim + 288
        # ramps up to the center, east and west; base sits 16u into the
        # floor so the outer face keeps area when the top edge drops to 0
        self.solids.append(make_box((256, -96, -16), (512, 96, t1),
                                    palette=PAL_CENTER,
                                    top_drop={5: t1, 7: t1}))
        self.solids.append(make_box((-512, -96, -16), (-256, 96, t1),
                                    palette=PAL_CENTER,
                                    top_drop={4: t1, 6: t1}))
        # kicker wedges north and south: hit them at speed (36.9 deg,
        # still bot-walkable) and ground speed becomes a launch arc
        self.sections.append(("kickers", {"count": 2}))
        self.solids.append(make_box((-96, 384, -16), (96, 512, 96),
                                    palette=PAL_GATE,
                                    top_drop={6: 96, 7: 96}))
        self.solids.append(make_box((-96, -512, -16), (96, -384, 96),
                                    palette=PAL_GATE,
                                    top_drop={4: 96, 5: 96}))

        # pillars for cover and walljumps: seeded count, size, layout
        n_pil = rng.randint(3, 6)
        self.sections.append(("pillars", {"count": n_pil}))
        pr = 0.55 * R1
        a_off = rng.uniform(0, 2 * math.pi)
        for k in range(n_pil):
            a = a_off + k * 2 * math.pi / n_pil
            px, py = pr * math.cos(a), pr * math.sin(a)
            half = rng.choice((48, 64, 80))
            tall = rng.randrange(256, 385, 32)
            self.solids.append(make_box((px - half, py - half, 0),
                                        (px + half, py + half, tall),
                                        tex=TEX_WALL, palette=PAL_PILLAR))

        # jump pads arcing onto the center
        self.sections.append(("jump pads", {"count": 4}))
        pad_r = 0.72 * R1
        for k in range(4):
            a = k * math.pi / 2
            px, py = pad_r * math.cos(a), pad_r * math.sin(a)
            self.solids.append(make_box((px - 48, py - 48, 0),
                                        (px + 48, py + 48, 10),
                                        palette=PAL_PAD))
            tb = make_box((px - 48, py - 48, 10), (px + 48, py + 48, 74),
                          tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER,
                          draw=set())
            self.triggers.append((tb, {
                "classname": "trigger_push",
                "target": f"padtarget_{k}",
            }))
            self.place("target_position", px * 0.30, py * 0.30, apex_z,
                       targetname=f"padtarget_{k}")

        # prize ledges at the top of the bank: ride the velodrome to shop.
        # seeded axis: north/south some seeds, east/west others
        self.sections.append(("bank ledges", {"items": "rail, mega"}))
        swap = rng.random() < 0.5
        for s, item in ((1, "weapon_railgun"), (-1, "item_health_mega")):
            lo, hi = sorted((s * (R2 - 192), s * (R2 - 16)))
            if swap:
                mins, maxs = (lo, -112), (hi, 112)
                ix, iy = s * (R2 - 104), 0
            else:
                mins, maxs = (-112, lo), (112, hi)
                ix, iy = 0, s * (R2 - 104)
            self.solids.append(make_box(
                (mins[0], mins[1], BANK_H - 128),
                (maxs[0], maxs[1], BANK_H), palette=PAL_LEDGE))
            self.place(item, ix, iy, BANK_H + 24)
        self.place("ammo_slugs", (R2 - 104) if swap else 88,
                   88 if swap else (R2 - 104), BANK_H + 24)

        # floor items
        self.sections.append(("items", {}))

        def ring(classname, r, k8, z=24.0):
            a = k8 * math.pi / 4
            self.place(classname, r * math.cos(a), r * math.sin(a), z)

        ring("weapon_rocketlauncher", 0.80 * R1, 0)
        ring("weapon_rocketlauncher", 0.80 * R1, 4)
        ring("ammo_rockets", 0.80 * R1 - 96, 0)
        ring("ammo_rockets", 0.80 * R1 - 96, 4)
        ring("weapon_lightning", 0.80 * R1, 2)
        ring("ammo_lightning", 0.80 * R1 - 96, 2)
        ring("weapon_plasmagun", 0.80 * R1, 6)
        ring("ammo_cells", 0.80 * R1 - 96, 6)
        ring("weapon_shotgun", 0.45 * R1, 1)
        ring("ammo_shells", 0.45 * R1 - 80, 1)
        ring("weapon_shotgun", 0.45 * R1, 5)
        ring("ammo_shells", 0.45 * R1 - 80, 5)
        ring("item_armor_combat", 0.80 * R1, 3)
        ring("item_armor_combat", 0.80 * R1, 7)
        for k in range(4):
            ring("item_health", 0.30 * R1, 2 * k + 1)
            ring("item_armor_shard", 0.62 * R1, 2 * k)

        # spawns face the tower
        for k in range(8):
            a = math.pi / 8 + k * math.pi / 4
            x, y = 0.60 * R1 * math.cos(a), 0.60 * R1 * math.sin(a)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, -0.7 * R1, 560, angle="90")

        # enclosure: the ceiling sits exactly on the wall top, so nothing
        # escapes over the rim and no void trigger is needed
        m = 64.0
        self.solids += make_skybox(
            -R2 - WALL_T - m, -R2 - WALL_T - m, -48 - m,
            R2 + WALL_T + m, R2 + WALL_T + m, WALL_H)
        self.entities.insert(0, {
            "classname": "worldspawn",
            "message": f"STRAFE64 arena {self.seed} "
                       f"({center}, difficulty {self.diff})",
            "music": pick_music(MUSIC_BREAKCORE, self.seed),
        })
        return self


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


def _face_brightness(n):
    if n[2] > 0.7:
        return 1.0
    if n[2] < -0.7:
        return 0.45
    return 0.80 if abs(n[0]) >= abs(n[1]) else 0.64


def _face_st(p, n):
    ax = max(range(3), key=lambda i: abs(n[i]))
    u, v = [i for i in range(3) if i != ax]
    return (p[u] / 64.0, p[v] / 64.0)


class BspWriter:
    def __init__(self, course):
        self.c = course
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
        cflags = CONTENTS_TRIGGER if is_trigger else CONTENTS_SOLID
        main_sid = self.shader_id(
            brush.faces[0].tex, SURF_NODRAW if is_trigger else 0, cflags)
        faces = []
        for f in brush.faces:
            sflags = SURF_NODRAW if (not f.draw or is_trigger) else 0
            if f.tex == TEX_SKY:
                sflags |= SURF_SKY | SURF_NOIMPACT
            faces.append((f, self.shader_id(f.tex, sflags, cflags),
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
            if f.draw and not is_trigger:
                surf_ids.append(self.emit_face(f, sid))
        self.brushes.append(struct.pack(
            "<3i", first_side, num_sides, main_sid))
        return surf_ids

    def emit_face(self, f, shader_id):
        poly = list(reversed(f.poly))  # engine winding: CW from outside
        first_vert = len(self.verts)
        br = _face_brightness(f.normal)
        color = tuple(min(255, int(c * br)) for c in f.palette) + (255,)
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
        self.surfaces.append(struct.pack(
            "<12i12f2i",
            shader_id, -1, MST_PLANAR,
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
        return bytes((105, 105, 118, 150, 145, 135, 96, 32)) * n

    # ---- assembly -------------------------------------------------------
    def write(self, path):
        c = self.c
        for b in c.solids:
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
    surfs = lump(LUMP_SURFACES)
    for i in range(n_surfs):
        v = struct.unpack_from("<12i", surfs, i * SZ_SURFACE)
        assert v[2] == MST_PLANAR and v[1] == -1
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


# ======================================================================
# packaging + CLI
# ======================================================================
def write_pk3(bsp_path, name, out_dir, aas_path=None):
    bots = 'bots "sarge major grunt"\n' if aas_path else ""
    arena = (f'{{\nmap "{name}"\nlongname "STRAFE64 {name}"\n'
             f'type "ffa tourney"\n{bots}}}\n')
    pk3 = os.path.join(out_dir, f"{name}.pk3")
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(bsp_path, f"maps/{name}.bsp")
        if aas_path:
            z.write(aas_path, f"maps/{name}.aas")
        z.writestr(f"scripts/{name}.arena", arena)
        z.writestr("scripts/strafe64.shader", SHADER_SCRIPT)
        for arc, data in build_detail_textures().items():
            z.writestr(arc, data)
    return pk3


def find_bspc():
    cand = os.environ.get("BSPC")
    if cand and os.path.isfile(cand):
        return cand
    here = os.path.dirname(os.path.abspath(__file__))
    local = os.path.join(here, "bspc")
    if os.path.isfile(local):
        return local
    import shutil
    return shutil.which("bspc")


def compile_aas(bsp_path):
    """Run bspc -bsp2aas so bots can navigate. Returns .aas path or None."""
    bspc = find_bspc()
    if not bspc:
        return None
    import subprocess
    out_dir = os.path.dirname(os.path.abspath(bsp_path)) or "."
    # NOTE: a "realistic" -cfg (aas_strafe64.cfg, phys tuned to the moveset)
    # was tried and REGRESSED nav — it made bspc conservative and bots stalled
    # (FLOW 152->0). bspc's default leaves physics at FLT_MAX, which treats the
    # bot as able to jump anywhere; combined with the enhanced moveset that
    # over-optimism navigates better than a realistic model. So: no -cfg.
    # (dojo_runs.jsonl iter2/iter3 — hypothesis rejected by no-regression.)
    r = subprocess.run(
        [bspc, "-forcesidesvisible", "-bsp2aas",
         os.path.abspath(bsp_path), "-output", out_dir],
        capture_output=True, text=True, cwd=out_dir)
    aas = os.path.splitext(bsp_path)[0] + ".aas"
    if r.returncode != 0 or not os.path.isfile(aas):
        sys.stderr.write(f"warning: bspc failed, no bot support\n"
                         f"{(r.stdout or '')[-400:]}\n")
        return None
    log = os.path.join(out_dir, "bspc.log")
    if os.path.isfile(log):
        os.remove(log)
    return aas


class Pit:
    """A small sealed combat arena for the dojo ARENA archetype — close spawns,
    cover pillars, weapons in reach. The velodrome was too spread for bots to
    ever meet (10 bots, 0 frags); this packs them in so they actually fight."""

    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []      # manifest (for the generate() report)

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def build(self):
        W, H, T = 448.0, 448.0, 16.0       # tighter pit -> more encounters
        self.sections.append(("combat pit", {"size": int(2 * W)}))
        # sealed room: floor, ceiling, four flush walls (no overlaps -> bspc-safe)
        self.solids.append(make_box((-W, -W, -T), (W, W, 0), palette=PAL_FLOORA))
        self.solids.append(make_box((-W, -W, H), (W, W, H + T),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W - T, -W, 0), (-W, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((W, -W, 0), (W + T, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, -W - T, 0), (W, -W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, W, 0), (W, W + T, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        # cover pillars
        for px, py in ((-220, -220), (220, -220), (-220, 220), (220, 220)):
            self.solids.append(make_box((px - 48, py - 48, 0), (px + 48, py + 48, 200),
                                        tex=TEX_WALL, palette=PAL_PILLAR))
        # eight close spawns in a tight ring, facing center
        for k in range(8):
            a = k * 2 * math.pi / 8
            x, y = 280 * math.cos(a), 280 * math.sin(a)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, 0, H - 96, angle="0")
        # weapons + armor: the reason to fight
        for cls, x, y in (("weapon_railgun", -340, 0), ("weapon_rocketlauncher", 340, 0),
                          ("weapon_shotgun", 0, -340), ("item_armor_combat", 0, 340),
                          ("item_health_large", 0, 0), ("item_quad", 0, 120)):
            self.place(cls, x, y, 24)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 combat pit"})
        return self


class SurfLine:
    """The ★ core-loop seed: a CS-surf-style line you flow through. Built from
    steep banked ramps (make_prism tops tilted past the walkable angle, so you
    don't stand — you surf), strung surf -> transition -> surf, ending in a
    finish that teleports back to the start (lap +1). v0 is a hand-laid two-ramp
    proof: it generates valid geometry and wires the full loop (race clock +
    MISSION REPORT fire on finish, teleport resets you). Ramp angles and the
    momentum hand-off between ramps are human-tuned from here — bots can't surf
    (AAS doesn't model air-strafe-on-ramps), so this is playtest-validated, not
    dojo-gated."""

    def __init__(self, seed=0, ramps=None):
        self.rng = random.Random(seed)
        self.ramps = ramps          # None = seed-driven 3-5; else a fixed count
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def surf_ramp(self, x0, x1, y0, y1, z_entry, fwd_drop, roll_drop, bank,
                  pal=PAL_SLIDE):
        """One surf ramp. The top is a single plane, steeply banked across Y
        (roll = roll_drop over the width, set past ~45 deg so it's non-walkable
        and you surf) and gently descending along X (fwd_drop over the length,
        for forward progress). bank=+1 tilts down toward +Y (you drift +Y),
        bank=-1 toward -Y. z_entry is the top height at the high entry edge.
        Returns (z_min, z_max) of the ramp top."""
        W, L = (y1 - y0), (x1 - x0)
        B = fwd_drop / L                 # +X lowers z (forward descent)
        C = (roll_drop / W) * bank       # +Y lowers z when bank=+1 (the roll)
        y_high = y0 if bank > 0 else y1  # the high edge the player enters on
        A = z_entry + B * x0 + C * y_high
        pz = lambda x, y: A - B * x - C * y
        foot = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
        top_zs = [pz(x, y) for (x, y) in foot]
        zb = min(top_zs) - 128.0
        self.solids.append(make_prism(foot, zb, top_zs, tex=TEX_FLOOR, palette=pal))
        return min(top_zs), max(top_zs)

    def pad(self, x0, x1, y0, y1, ztop, pal, thick=32.0):
        self.solids.append(make_box((x0, y0, ztop - thick), (x1, y1, ztop),
                                     tex=TEX_FLOOR, palette=pal))

    def build(self):
        rng = self.rng
        n = self.ramps if self.ramps else rng.randint(3, 5)
        self.sections.append(("surf line", {"ramps": n}))

        # one shared Y-corridor for the whole line. Alternating the bank each
        # ramp makes the player zig-zag inside it: a +1 ramp's LOW exit edge
        # (y1) is exactly a -1 ramp's HIGH entry edge (y1), so consecutive ramps
        # meet flush on the player's path with no realignment — that's what lets
        # an arbitrary seed-driven chain stay continuous.
        W = 512.0
        y0, y1 = -W / 2.0, W / 2.0

        # --- start pad (high) -------------------------------------------------
        sx0, sx1, stop = -384.0, -64.0, 1024.0
        cx = (sx0 + sx1) / 2.0
        self.pad(sx0, sx1, -176, 176, stop, PAL_START)
        # several spread spawns: a single spawn telefrags a field of bots on
        # respawn (and is cramped for humans racing together)
        for sy in (-120, -40, 40, 120):
            self.entities.append({"classname": "info_player_deathmatch",
                                  "origin": f"{cx:g} {sy:g} {stop + 40:g}", "angle": "0"})
        self.entities.append({"classname": "misc_teleporter_dest",
                              "targetname": "start_dest",
                              "origin": f"{cx:g} 0 {stop + 40:g}", "angle": "0"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"{cx:g} 0 {stop + 360:g}", "angle": "0"})
        self.triggers.append((make_box((sx0, -176, stop), (sx1, 176, stop + 256),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set()),
                              {"classname": "trigger_race_start"}))

        # --- procedural ramp chain -------------------------------------------
        x = 0.0
        entry_z = stop
        lo_z = stop
        for i in range(n):
            bank = +1 if (i % 2 == 0) else -1
            L = float(rng.randint(1280, 1792))
            angle = rng.uniform(49.0, 55.0)        # surfable band (non-walkable)
            roll_drop = round(W * math.tan(math.radians(angle)))
            fwd_drop = float(rng.randint(200, 320))
            x_start = x
            r_lo, _ = self.surf_ramp(x, x + L, y0, y1, entry_z,
                                     fwd_drop=fwd_drop, roll_drop=roll_drop, bank=bank)
            # bot bait: items down the ramp midline (y=0). AAS can't path a surf
            # face, so bots won't ride it for its own sake — but they'll chase a
            # pickup onto it, where BotSurfControl takes over. Item z follows the
            # ramp plane at y=0 (== entry_z - roll_drop/2 - fwd_drop*frac), lifted.
            for frac in (0.25, 0.5, 0.75):
                iz = entry_z - roll_drop / 2.0 - fwd_drop * frac + 56.0
                self.entities.append({
                    "classname": "item_armor_shard",
                    "origin": f"{x_start + frac * L:g} 0 {iz:g}"})
            lo_z = r_lo
            x += L
            entry_z = r_lo               # next ramp enters at this ramp's low corner
            if i < n - 1:                # transition pad, flush at the low edge
                gap = 384.0
                self.pad(x, x + gap, y0, y1, r_lo, PAL_PLAIN)
                x += gap

        # --- finish pad (low) + lap teleport back to start -------------------
        # the trigger is a TALL full-corridor wall (lo_z-64 .. +768): a surfer
        # arrives here airborne at 600+ ups, well above a short floor trigger —
        # a 256-tall box let fast runs (bot AND human) fly clean over the finish.
        fx0, fx1 = x, x + 448.0
        self.pad(fx0, fx1, y0, y1, lo_z, PAL_FINISH)
        # trigger reaches modestly back over the last ramp tail + is tall (below):
        # a fast surfer arrives airborne and a touch short of the pad. (Bots lose
        # the line earlier on the later ramps and fall well short — full bot lap
        # completion is an open surf-control-robustness problem, not a trigger one.)
        tx0 = fx0 - 256.0
        fb = make_box((tx0, y0, lo_z - 64), (fx1, y1, lo_z + 768),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((tx0, y0, lo_z - 64), (fx1, y1, lo_z + 768),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_teleport",
                                   "target": "start_dest"}))

        # --- sky box enclosing the whole line --------------------------------
        for b in make_skybox(sx0 - 128, y0 - 128, lo_z - 256,
                             fx1 + 128, y1 + 128, stop + 320):
            self.solids.append(b)

        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 surf line"})
        return self


class SurfTurn:
    """Banked surf TURN test piece — the last surf-depth primitive. A steep
    banked 180deg arc (inner edge low, outer high, like a velodrome corner but
    >45deg so you surf it): enter at one end, lean into the bank, carry speed
    around the curve, exit travelling the opposite way. This is the candidate
    geometry for closed surf circuits with corners; FEEL is human-validated (bots
    can't surf an arc), so v0 just proves it generates + loads. If it rides well,
    fold `arc()` into SurfLine for true 2D circuits."""

    def __init__(self, seed=0):
        self.rng = random.Random(seed)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def pad(self, x0, x1, y0, y1, ztop, pal, thick=32.0):
        self.solids.append(make_box((x0, y0, ztop - thick), (x1, y1, ztop),
                                     tex=TEX_FLOOR, palette=pal))

    def build(self):
        Ri, W = 512.0, 320.0            # inner radius, lane width
        Ro = Ri + W
        H = 420.0                       # bank height over W -> atan(420/320)=53deg, surfable
        SEGS = 14
        th0, th1 = math.pi / 2, 3 * math.pi / 2   # 90 -> 270 deg = 180deg U, bulging -X
        self.sections.append(("banked surf turn",
                              {"deg": 180, "bank": f"{int(H)}z/{int(W)}u~53deg"}))

        def arc(r, th):
            return (r * math.cos(th), r * math.sin(th))

        # banked arc, segment by segment (inner low z=0, outer high z=H)
        for i in range(SEGS):
            ta = th0 + (th1 - th0) * i / SEGS
            tb = th0 + (th1 - th0) * (i + 1) / SEGS
            ia, ib = arc(Ri, ta), arc(Ri, tb)
            oa, ob = arc(Ro, ta), arc(Ro, tb)
            self.solids.append(make_prism([ia, ib, ob, oa], -160.0,
                                          [0.0, 0.0, H, H], palette=PAL_SLIDE))

        # entry pad at the th0 mouth (y=+, x=0), exit pad at the th1 mouth (y=-)
        self.pad(-64, 64, Ri, Ro, 0.0, PAL_START)
        cx = 0.0
        self.entities.append({"classname": "info_player_deathmatch",
                              "origin": f"{cx:g} {Ri + W / 2:g} 48", "angle": "180"})
        self.entities.append({"classname": "misc_teleporter_dest",
                              "targetname": "start_dest",
                              "origin": f"{cx:g} {Ri + W / 2:g} 48", "angle": "180"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"0 0 {H + 200:g}", "angle": "0"})
        self.triggers.append((make_box((-96, Ri, 0), (96, Ro, 128),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set()),
                              {"classname": "trigger_race_start"}))

        self.pad(-64, 64, -Ro, -Ri, 0.0, PAL_FINISH)
        fb = make_box((-96, -Ro, 0), (96, -Ri, 256),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((-96, -Ro, 0), (96, -Ri, 256),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_teleport",
                                   "target": "start_dest"}))

        for b in make_skybox(-Ro - 192, -Ro - 192, -288, Ro + 192, Ro + 192, H + 384):
            self.solids.append(b)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 banked surf turn"})
        return self


def generate(seed, difficulty, length, out_dir, want_map, want_pk3,
             arena=False, name=None, void=True, voidrise=None,
             voiddelay=None, dojo=None, surf=False):
    os.makedirs(out_dir, exist_ok=True)
    if surf == "turn":
        name = name or (f"surfturn_{seed}" if seed else "surfturn_64")
        course = SurfTurn(seed).build()  # banked surf-turn test (human feel-validated)
    elif surf:
        name = name or (f"surf_{seed}" if seed else "surf_64")
        course = SurfLine(seed).build()  # ★ core-loop surf line (human-validated)
    elif dojo == "arena":
        name = name or "dojo_arena"
        course = Pit(seed).build()      # tight combat pit (was the velodrome)
    elif dojo:
        name = name or f"dojo_{dojo}"
        course = Course(seed, difficulty, 1, void=void, voidrise=voidrise,
                        voiddelay=voiddelay, recipe=DOJO_RECIPES[dojo]).build()
    elif arena:
        name = name or (f"strafe64dm_{seed}"
                        + ("" if difficulty == 1 else f"_d{difficulty}"))
        course = Arena(seed, difficulty).build()
    else:
        name = name or (f"strafe64_{seed}"
                        + ("" if difficulty == 1 else f"_d{difficulty}")
                        + ("" if length == 1 else f"_x{length}"))
        course = Course(seed, difficulty, length, void=void,
                        voidrise=voidrise, voiddelay=voiddelay).build()
    bsp_path = os.path.join(out_dir, f"{name}.bsp")
    stats = BspWriter(course).write(bsp_path)
    check_bsp(bsp_path)
    print(f"{name}: {stats['brushes']} brushes, {stats['surfaces']} surfaces, "
          f"{stats['leafs']} leafs, {stats['bytes'] / 1024:.0f} KB")
    for sec, info in course.sections:
        extra = (" " + " ".join(f"{k}={v}" for k, v in info.items())) if info else ""
        print(f"    {sec}{extra}")
    aas_path = compile_aas(bsp_path)
    if aas_path:
        print(f"    + {os.path.basename(aas_path)} (bots enabled)")
    else:
        print("    (no bspc found -> no .aas, bots can't navigate)")
    if want_map:
        write_map(course, os.path.join(out_dir, f"{name}.map"))
        print(f"    + {name}.map (Radiant source)")
    if want_pk3:
        pk3 = write_pk3(bsp_path, name, out_dir, aas_path)
        print(f"    + {os.path.basename(pk3)}")
    print(f"    play: copy into baseq3/maps and run \\map {name}")
    return bsp_path


def selftest():
    import tempfile
    ok = 0
    with tempfile.TemporaryDirectory() as td:
        for seed in (1, 2, 3, 7, 42, 1337):
            for diff in (0, 1, 2):
                course = Course(seed, diff, 1).build()
                p = os.path.join(td, f"t_{seed}_{diff}.bsp")
                BspWriter(course).write(p)
                check_bsp(p)
                ok += 1
        for seed in (99,):
            course = Course(seed, 1, 3).build()  # tower: 3 decks, 2 lifts
            lifts = sum(1 for s, _ in course.sections if s == "lift gate")
            assert lifts == 2, f"tower expected 2 lift gates, got {lifts}"
            classes = [e["classname"] for _, e in course.triggers]
            assert classes.count("trigger_race_start") == 1
            assert classes.count("trigger_race_finish") == 1
            ws = course.entities[0]
            assert float(ws["voidrise"]) > 0 and float(ws["voidbase"]) < 0
            p = os.path.join(td, "t_long.bsp")
            BspWriter(course).write(p)
            check_bsp(p)
            ok += 1
        # void can be disabled for free practice
        course = Course(5, 1, 1, void=False).build()
        assert "voidrise" not in course.entities[0]
        ok += 0
        for seed in (1, 7, 1337):
            for diff in (0, 2):
                arena = Arena(seed, diff).build()
                p = os.path.join(td, f"a_{seed}_{diff}.bsp")
                BspWriter(arena).write(p)
                check_bsp(p)
                ok += 1
    print(f"selftest: {ok} maps generated and validated, all invariants hold")
    print(f"physics: jump range @320 = {jump_range(320):.0f}u, "
          f"single-jump ledge <= {LEDGE_SJ:.0f}u, "
          f"double-jump ledge <= {LEDGE_DJ:.0f}u")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("seed", nargs="?", type=int, help="course seed")
    ap.add_argument("--dojo", choices=("speed", "flow", "ztrick", "arena", "all",
                                       "slalom", "hurdles", "hazard", "movers",
                                       "fork", "showcase"),
                    help="generate a bot-dojo scenario (or 'all' four)")
    ap.add_argument("--arena", action="store_true",
                    help="deathmatch arena with a velodrome ring instead "
                         "of a linear run")
    ap.add_argument("--surf", action="store_true",
                    help="generate the ★ core-loop surf line (steep banked "
                         "ramps you air-strafe along; finish laps back to start)")
    ap.add_argument("--surfturn", action="store_true",
                    help="generate a banked surf-TURN test piece (180deg steep "
                         "banked arc — candidate corner geometry, feel-test by hand)")
    ap.add_argument("--difficulty", type=int, default=1, choices=(0, 1, 2))
    ap.add_argument("--length", type=int, default=1,
                    help="course length multiplier (1 = ~6 sections)")
    ap.add_argument("--out", default="generated", help="output directory")
    ap.add_argument("--map", action="store_true",
                    help="also write a Radiant .map source")
    ap.add_argument("--pk3", action="store_true",
                    help="also pack a .pk3 with an .arena file")
    ap.add_argument("--check", metavar="FILE", help="validate a .bsp and exit")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--daily", action="store_true",
                    help="today's course (seed = UTC date): same tower "
                         "worldwide, share your time")
    ap.add_argument("--no-void", action="store_true",
                    help="omit the rising void (free practice)")
    ap.add_argument("--voidrise", type=float, default=None,
                    help="override void rise rate in ups/s")
    ap.add_argument("--voiddelay", type=float, default=None,
                    help="override void grace period in seconds")
    args = ap.parse_args()

    if args.check:
        stats = check_bsp(args.check)
        print(f"{args.check}: OK {stats}")
        return
    if args.selftest:
        selftest()
        return
    if args.surf or args.surfturn:
        # --daily --surf = the daily-speedrun core loop: one date-seeded surf
        # circuit, same worldwide, fresh each day (unifies the daily identity
        # with the ★ surf core loop instead of the tower course).
        dname = None
        if args.daily:
            import datetime
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d")
            seed = int(stamp)
            dname = f"surf_daily_{stamp}"
        else:
            seed = args.seed if args.seed is not None else 64
        generate(seed, args.difficulty, 1, args.out, args.map, args.pk3,
                 surf=("turn" if args.surfturn else True), name=dname)
        return
    if args.dojo:
        seed = args.seed if args.seed is not None else 64
        targets = (["speed", "flow", "ztrick", "arena"]
                   if args.dojo == "all" else [args.dojo])
        for d in targets:
            # the arena dojo reuses a velodrome seed bspc is known to digest
            # ("Tried parent" aborts on some seeds); movement dojos use 64
            s = 1337 if d == "arena" and args.seed is None else seed
            generate(s, args.difficulty, 1, args.out, args.map, args.pk3,
                     dojo=d, void=not args.no_void,
                     voidrise=args.voidrise, voiddelay=args.voiddelay)
        return
    name = None
    length = args.length
    if args.daily:
        if args.seed is not None:
            ap.error("--daily picks the seed from the date; drop the seed")
        import datetime
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d")
        args.seed = int(stamp)
        name = f"strafe64_daily_{stamp}"
        if length == 1:
            length = 3      # the daily is a tower
    if args.seed is None:
        ap.error("a seed is required (or use --daily / --check / --selftest)")
    generate(args.seed, args.difficulty, length, args.out,
             args.map, args.pk3, arena=args.arena, name=name,
             void=not args.no_void, voidrise=args.voidrise,
             voiddelay=args.voiddelay)


if __name__ == "__main__":
    main()
