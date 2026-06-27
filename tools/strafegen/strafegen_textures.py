"""strafegen_textures — procedural detail maps, skybox, clouds, starfield, music.

All the code-generated TGAs (build_detail_textures) + the Bryce dusk skybox
(_build_synthsky) + per-seed music selection. Uses the shared TGA writer.
"""
import math
import os
import random

from strafegen_tga import _tga32, _clamp8

TEX_SIZE = 64


def build_detail_textures():
    """Return {arcname: tga_bytes} for the tech-panel detail maps."""
    rng = random.Random(0xC0FFEE)
    n = TEX_SIZE
    noise = [rng.randint(-6, 6) for _ in range(n * n)]

    # --- Source measure grid (subtle): a uniform unit grid, pure grayscale so
    # the section palette supplies the hue. 64px over a 64u tile = 1 texel/unit,
    # so lines land on real world units — minor 16u, mid 32u, major 64u (the
    # tile edge) — graded by depth into a clean dev_measuregeneric read. Lines
    # DARKEN the near-white base ("subtle" = gentle), so orange floors / grey
    # walls dominate and silhouettes still win at MACH speed. Floors and walls
    # share one measure map (Source uses the same grid on both); only the brush
    # palette differs (SRC_ORANGE vs SRC_GREY). ---
    def _measure_grid(base):
        out = []
        for y in range(n):
            for x in range(n):
                v = base + noise[y * n + x]
                if x % 16 == 0 or y % 16 == 0:     # 16u minor lines
                    v -= 16
                if x % 32 == 0 or y % 32 == 0:     # 32u mid lines (stack)
                    v -= 16
                if x % 64 < 2 or y % 64 < 2:       # 64u major (tile edge)
                    v -= 30
                g = _clamp8(v)
                out.append((g, g, g))
        return out

    # LOWER the map light: the detail map multiplies the vertex colour
    # (rgbGen exactVertex), so dropping the near-white base from ~235 to ~165
    # darkens every floor/wall globally (via the override pk3) — a moodier scene
    # that lets the glowing dusk sky + fog do the work.
    floor = _measure_grid(118)
    wall  = _measure_grid(116)

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

    # --- soft radial glow for the plasma-ish speed-trail datamosh chips: bright
    # centre falling smoothly to black at the edge, so an additive vertex-coloured
    # quad reads as a soft translucent plasma blob rather than a hard square. ---
    glow = []
    gc = (n - 1) / 2.0
    for y in range(n):
        for x in range(n):
            dx = (x - gc) / gc
            dy = (y - gc) / gc
            r = math.sqrt(dx * dx + dy * dy)
            gv = max(0.0, 1.0 - r)
            gv = gv * gv * gv          # soft cubic falloff -> plasma core + haze
            iv = _clamp8(gv * 255)
            glow.append((iv, iv, iv))

    # --- matrix digital-rain: green streaks on black for the velodrome
    # wall-of-death (textures/strafe64/matrix). Each column gets a few rain
    # "heads" with a tail fading upward; the tail wraps vertically (mod n) so
    # the tile scrolls seamlessly under tcMod scroll. The shader is additive,
    # so black reads as transparent and only the streaks glow under HDR. ---
    mrng = random.Random(0x4D7258)        # 'MRX'
    mbright = [0.0] * (n * n)
    MTAIL = 18
    for x in range(n):
        if mrng.random() < 0.40:          # ~40% of columns stay dry
            continue
        for hy in mrng.sample(range(n), mrng.randint(1, 3)):
            for d in range(MTAIL):
                yy = ((hy + d) % n) * n + x
                b = 1.0 - d / MTAIL
                if b > mbright[yy]:
                    mbright[yy] = b
            mbright[hy * n + x] = 1.0      # bright head pip
    matrix = []
    for y in range(n):
        for x in range(n):
            b = mbright[y * n + x]
            matrix.append((_clamp8(b * 50), _clamp8(b * 255), _clamp8(b * 80)))

    # --- concrete: a pale near-white tile for the lun3dm5 brutalist theme. Like
    # the dev grid it stays near-white (rgbGen exactVertex multiplies the grey
    # PAL_CRETE through it), but instead of a measure grid it carries blotchy
    # tonal variation + faint 16u panel seams + scattered pitting, so a single
    # material reads as poured concrete across big eroded cube masses. Greyscale
    # so the vertex palette + sun-dome supply all the colour/shading. ---
    crng = random.Random(0xC0AC5E7E)
    cgrain = [crng.randint(-7, 7) for _ in range(n * n)]
    pits = {crng.randrange(n * n) for _ in range(28)}
    tau = 2.0 * math.pi
    crete = []
    for y in range(n):
        for x in range(n):
            u, w = x / n, y / n
            # low-freq blotch (seamless sum-of-sines) — gentle large-scale mottle
            blot = (math.sin(tau * u) * math.sin(tau * w)
                    + 0.6 * math.sin(tau * 2 * u + 1.3) * math.sin(tau * w + 0.5))
            v = 226 + int(9 * blot) + cgrain[y * n + x]
            if x % 16 == 0 or y % 16 == 0:     # faint panel seam (form lines)
                v -= 9
            if x % 32 == 0 or y % 32 == 0:     # slightly stronger major seam
                v -= 7
            i = y * n + x
            if i in pits:                      # weathering pit / stain
                v -= 34
            elif (i - 1) in pits or (i + 1) in pits:
                v -= 14
            g = _clamp8(v)
            crete.append((g, g, g))

    tex = {
        "textures/strafe64/d_floor.tga": _tga32(n, n, floor),
        "textures/strafe64/d_wall.tga": _tga32(n, n, wall),
        "textures/strafe64/crete.tga": _tga32(n, n, crete),
        "textures/strafe64/accent.tga": _tga32(n, n, accent),
        "textures/strafe64/void_hex.tga": _tga32(n, n, voidtex),
        "textures/strafe64/trailglow.tga": _tga32(n, n, glow),
        "textures/strafe64/matrix.tga": _tga32(n, n, matrix),
        "textures/strafe64/sky_stars.tga": _build_starfield(),
        "textures/strafe64/env/clouds.tga": _build_clouds(),
    }
    tex.update(_build_synthsky())   # 90s holographic-renderer skybox (6 faces)
    tex.update(_load_baked())       # photoreal skybox + concrete for concrete theme
    return tex


def _load_baked():
    """Pre-baked photo assets for the concrete (lun3dm5) theme, from skytex/.

    The realsky_<side>.tga cube (skybox_from_photo.py) AND crete.tga (a real
    concrete photo, baked square by the crete tile step) live in skytex/ so the
    generator stays PIL-free at run time — it just reads the TGAs. If crete.tga is
    present it OVERRIDES the procedural concrete built above (so we ship the photo,
    not the synthetic grid). Absent files => just not bundled; the default theme
    never references any of them."""
    here = os.path.dirname(os.path.abspath(__file__))
    sky_dir = os.path.join(here, "skytex")
    out = {}
    for side in ("rt", "lf", "ft", "bk", "up", "dn"):
        p = os.path.join(sky_dir, f"realsky_{side}.tga")
        if os.path.exists(p):
            with open(p, "rb") as fh:
                out[f"textures/strafe64/env/realsky_{side}.tga"] = fh.read()
    crete = os.path.join(sky_dir, "crete.tga")
    if os.path.exists(crete):
        with open(crete, "rb") as fh:
            out["textures/strafe64/crete.tga"] = fh.read()
    return out


def _build_clouds(n=128):
    """Seamless soft cloud tile for the sky's animated cloud layers.

    The sky shader maps this onto the dome as slow-scrolling cloud stages
    (idTech3 renders shader stages as cloud layers when skyparms sets a cloud
    height) so soft Bryce-style clouds DRIFT overhead — gentle real motion in the
    sky. Built from a seamless sum-of-sines field (every frequency an integer
    number of cycles, so it wraps) pushed through a soft high-end ramp: only the
    crests become bright puffs, everything else is black so the additive
    (GL_ONE GL_ONE) stage adds only the clouds, not a grey wash.
    """
    tau = 2.0 * math.pi
    waves = [(1, 0, 0.0), (0, 1, 1.1), (2, 1, 2.3),
             (1, 2, 0.6), (3, 1, 1.7), (1, 3, 2.9)]
    px = []
    for y in range(n):
        v = y / n
        for x in range(n):
            u = x / n
            s = sum(math.sin(tau * (fx * u + fy * v) + ph) for fx, fy, ph in waves)
            s = s / len(waves) * 0.5 + 0.5                   # 0..1 smooth field
            # HAZY veil: lower threshold = broader coverage, gentler power = soft
            # diffuse edges (not crisp puffs), and dimmer so it reads as drifting
            # haze rather than bright clouds.
            c = max(0.0, (s - 0.40) / 0.60)
            c = c ** 1.4
            # SUN-LIT tint: warm amber crests (R>G>B) so the clouds read as lit by
            # the dusk sun rather than a cold grey-blue veil. The shader stages run
            # these additive at ~60% (rgbGen const/wave below) so they stay wispy
            # and the dusk gradient shows through — "more transparent" clouds.
            px.append((_clamp8(c * 190), _clamp8(c * 132), _clamp8(c * 90)))
    return _tga32(n, n, px)


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
    """Six-face procedural skybox — a COLOURFUL DIRECTIONAL DUSK.

    Every face samples one direction->colour function, so the cube is seamless
    by construction (a shared edge resolves to the same world direction on both
    faces). The look is a realistic single-scatter dusk: a big bright sun low on
    the horizon, with the whole warm bright sky (orange->coral->pink->magenta)
    concentrated AROUND the sun's bearing and falling to a cool deep-blue night
    on the far side and overhead. No mountains — clean sky; drifting soft clouds
    are added on top by the sky shader's animated cloud stages. Emits
    textures/strafe64/env/synth_<side>.tga for rt/lf/ft/bk/up/dn (Q3 skyparms).
    """
    global _SYNTHSKY_CACHE
    if _SYNTHSKY_CACHE is not None:
        return _SYNTHSKY_CACHE

    # COLOURFUL DUSK, DIRECTIONAL (realistic single-scatter). Two vertical
    # gradients, blended by how close the view is to the sun's azimuth: SKY_SUN
    # (warm, bright — orange horizon up through coral/pink/magenta to a night
    # zenith) on the sun side, SKY_ANTI (cool, dark blue) on the far side. So the
    # whole bright warm wedge sits AROUND the sun and the sky falls to cool night
    # away from it — no mountains, just sky + drifting clouds.
    SKY_SUN = [(0.00, (255, 150, 46)), (0.09, (255, 100, 78)),
               (0.20, (236, 72, 120)), (0.34, (154, 62, 158)),
               (0.54, (78, 52, 150)), (0.78, (28, 28, 92)),
               (1.00, (6, 8, 30))]                            # horizon -> zenith
    SKY_ANTI = [(0.00, (48, 60, 116)), (0.18, (42, 50, 122)),
                (0.44, (38, 38, 108)), (0.68, (20, 22, 80)),
                (1.00, (6, 8, 30))]
    GROUND = (10, 9, 22)                                       # below-horizon haze

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

    # a big bright sun low toward +x — the brightest point in the sky
    SUN_AZ, SUN_EL = 0.0, 0.10
    sun = (math.cos(SUN_EL) * math.cos(SUN_AZ),
           math.cos(SUN_EL) * math.sin(SUN_AZ),
           math.sin(SUN_EL))

    def color(dx, dy, dz):
        el = dz                                              # -1..1, +z up
        az = math.atan2(dy, dx)

        # azimuthal proximity to the sun (1 at the sun bearing, 0 opposite),
        # smoothstepped — this is what concentrates the bright warm sky AROUND
        # the sun and lets it fall to cool night on the far side.
        dazi = abs(math.atan2(math.sin(az - SUN_AZ), math.cos(az - SUN_AZ)))
        w = 1.0 - dazi / math.pi
        w = w * w * (3.0 - 2.0 * w)
        f = max(0.0, el) ** 0.55                             # horizon-weighted vertical
        warm = grad(SKY_SUN, f)
        cool = grad(SKY_ANTI, f)
        c = [cool[i] + (warm[i] - cool[i]) * w for i in range(3)]

        if el < 0.0:                                         # dark haze below horizon
            t2 = min(1.0, -el / 0.25)
            c = [c[i] * (1.0 - t2) + GROUND[i] * t2 for i in range(3)]

        # the sun itself: a CONTAINED warm-gold glow, not a wide near-white blob.
        # The old core pushed toward (255,236,190) over a ~70deg halo, so the GL2
        # bloom clipped it to a flat white wall whenever you looked skyward. Now
        # the core is SATURATED dusk gold (high R, low B): even when bloom lifts
        # it, it blooms gold rather than white. Tighter halo (~40deg) + small disc.
        cosd = dx * sun[0] + dy * sun[1] + dz * sun[2]
        ang = math.acos(max(-1.0, min(1.0, cosd)))
        halo = max(0.0, 1.0 - ang / 0.70) ** 3.5
        disc = 1.0 if ang < 0.045 else 0.0
        SUN_R, SUN_G, SUN_B = 255, 168, 78          # saturated dusk gold
        c[0] += (SUN_R - c[0]) * min(1.0, 0.95 * halo + disc)
        c[1] += (SUN_G - c[1]) * min(1.0, 0.80 * halo + disc)
        c[2] += (SUN_B - c[2]) * min(1.0, 0.42 * halo + disc * 0.85)

        # a gentle low warm WASH toward the sun: lifts the bright wedge near the
        # horizon so the sun side reads as glowing atmosphere — softened (0.55 ->
        # 0.32) so it no longer overdrives the bloom into a white wall.
        wash = w * max(0.0, 1.0 - abs(el) / 0.50) * 0.32
        c[0] += 48 * wash
        c[1] += 26 * wash
        c[2] += 9 * wash

        # stars on the dark side: only where the sky is cool/high, fading out
        # toward the bright sun side
        if el > 0.2:
            dark = (1.0 - w) * min(1.0, (el - 0.2) / 0.5)
            if dark > 0.22:
                hx, hy = int((az + math.pi) * 180.0), int(el * 180.0)
                if ((hx * 73856093) ^ (hy * 19349663)) & 3071 == 0:
                    c = [c[i] + 150 * dark for i in range(3)]

        return (_clamp8(c[0]), _clamp8(c[1]), _clamp8(c[2]))

    # Per-face basis: dir = F + sx*R + sy*U, with sx left->right [-1,1] and sy
    # bottom->top [-1,1] (the loop below sets sy=+1 at the image's TOP row). F is
    # the face's world normal, R the world dir of image-right, U of image-top.
    #
    # These MUST match how ioquake3's tr_sky.c maps each named image onto a cube
    # side, or the sky lands on the wrong faces and shears as you turn (the sun
    # ends up 90 deg off, "doesn't follow the mouse"). The engine convention is
    # suf[]={rt,bk,lf,ft,up,dn} drawn under sky_texorder={0,2,1,3,4,5} via
    # st_to_vec, which resolves to: rt=+X, lf=-X, bk=+Y, ft=-Y, up=+Z, dn=-Z,
    # with the right/top axes below. All four sides still share U=+z, so the
    # horizon ring stays level and seamless. (Q3 coords: +x fwd, +y left, +z up.)
    faces = {
        "rt": ((1, 0, 0),  (0, -1, 0), (0, 0, 1)),   # +X  right=-Y  top=+Z
        "lf": ((-1, 0, 0), (0, 1, 0),  (0, 0, 1)),   # -X  right=+Y  top=+Z
        "bk": ((0, 1, 0),  (1, 0, 0),  (0, 0, 1)),   # +Y  right=+X  top=+Z
        "ft": ((0, -1, 0), (-1, 0, 0), (0, 0, 1)),   # -Y  right=-X  top=+Z
        "up": ((0, 0, 1),  (0, -1, 0), (-1, 0, 0)),  # +Z  right=-Y  top=-X
        "dn": ((0, 0, -1), (0, -1, 0), (1, 0, 0)),   # -Z  right=-Y  top=+X
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


# SOURCE DEV-TEXTURE palette. The bulk world wears the classic Hammer
# measure-grid look: orange floors, grey walls, lighter-orange trim. Identity
# no longer lives in rainbow per-mechanic hues — it lives on the ACCENTS
# (start / finish / checkpoints / hazards / pads / portals), which keep their
# vivid colours and pop hard against the neutral dev base. The detail TGAs
# stay near-white grids; rgbGen exactVertex multiplies these palette colours
# through, so floors read Source-orange and walls Source-grey while the subtle
# grid carves scale into both. Final on-screen colour ≈ palette × ~0.92 (the
# texture base), so the constants sit a touch brighter than the rendered hue.


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

