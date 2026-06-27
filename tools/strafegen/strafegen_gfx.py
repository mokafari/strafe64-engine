"""strafegen_gfx — graphics-recipe module for strafegen.

Implements the runtime light/graphics tricks from
``docs/graphics-tricks-recipes.md`` as drop-in additions to strafegen's identity
shader library. strafegen writes vertex-lit IBSP directly (no q3map2 bake), so
only the LAYER-1 (shader-script) and LAYER-2 (GL2/rend2 runtime) tricks apply —
the baked Layer-3 lightmap tricks do not. What this module adds:

  * q3gl2_sun  — a directional dusk sun + cascaded shadow maps, injected into the
                 existing sky shader. Direction matches the painted Bryce sun
                 (see strafegen._build_synthsky: azimuth +X, low elevation), so
                 the cast shadows agree with where the sky says the sun is.
  * hull       — a full PBR-lite material (diffuse + normalMap + specularMap +
                 normalParallaxMap). Self-contained rend2 stages, so it lights
                 and self-shadows correctly regardless of the vertex-lit world.
  * chrome     — fake reflective metal via tcGen environment (no cubemap needed).
  * plasma     — turbulent additive energy panel (tcMod turb), optionally
                 audio-reactive through the au_* shader waveforms.
  * beam       — a deformVertexes autosprite2 strip that always faces the camera,
                 for laser / conduit / trail decor.

All component textures are generated procedurally (a few KB, deterministic) so
the module is self-contained and adds no hand-painted art. The renderer cvars
that arm these (sun shadows, normal/specular, hdr, tonemap) default ON in our
renderergl2 fork; render_cfg() emits the few that do not (parallax, ssao).

Public API used by strafegen.py:
    augment(shader_script) -> str      # inject sun + append component shaders
    gfx_textures()         -> dict     # {arcname: tga_bytes}
    render_cfg()           -> str      # cvar block to exec for the full look

Standalone (no strafegen import) so it can be unit-tested alone.
"""

import math
import random
import struct

# ----------------------------------------------------------------------
# self-contained TGA writer (mirrors strafegen._tga32: 32-bit BGRA, bottom-up)
# ----------------------------------------------------------------------


# Shader names for the component materials, so generators can route faces to
# them by name (e.g. tex=gfx.TEX_HULL). HULL/CHROME are opaque and solid-safe;
# PLASMA/BEAM are nonsolid additive decals (need a nonsolid decorative brush).
TEX_HULL   = "textures/strafe64/hull"
TEX_CHROME = "textures/strafe64/chrome"
TEX_PLASMA = "textures/strafe64/plasma"
TEX_BEAM   = "strafe64/beam"


def _tga32(w, h, px):
    """Uncompressed 32-bit TGA. px is a flat list of (r,g,b) rows top->bottom."""
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


# ======================================================================
# SUN — directional dusk light + cascaded shadows
# ======================================================================
#
# Painted sun (strafegen._build_synthsky): SUN_AZ = 0 (toward +X), SUN_EL ~= 6deg.
# We keep the azimuth so shadows fall away from the visible sun, but lift the
# elevation well above the painted disc. A low sun throws long, GRAZING shadows
# whose cascade edges stair-step and self-shadow (acne) — the "jagged shadow in
# the corners" on the big (≈2800u) arenas/killboxes. 38deg keeps a warm raking
# afternoon feel while the shadows land frontally enough to stay clean; the
# shadow map size (render_cfg) does the rest. Don't drop this below ~30.
SUN_AZIMUTH_DEG   = 0.0      # bearing, matches the painted sun (+X / "rt" face)
SUN_ELEVATION_DEG = 38.0     # well above the painted ~6deg: kills grazing-shadow acne
SUN_COLOR         = (1.00, 0.92, 0.78)   # warm dusk (engine re-normalizes)
SUN_INTENSITY     = 100.0    # scale applied to the normalized colour
SUN_SHADOW_SCALE  = 0.5      # GL2 cascade darkness/coverage (0..1-ish)


def sun_keyword():
    """The q3gl2_sun shader line. Arg order verified against our renderergl2
    fork (tr_shader.c): r g b  intensity  degrees(azimuth)  elevation  shadowScale."""
    r, g, b = SUN_COLOR
    return ("\tq3gl2_sun %g %g %g %g %g %g %g\n"
            % (r, g, b, SUN_INTENSITY,
               SUN_AZIMUTH_DEG, SUN_ELEVATION_DEG, SUN_SHADOW_SCALE))


def inject_sun(shader_script):
    """Insert the q3gl2_sun keyword into the existing sky shader block.

    Anchors on the unique 'surfaceparm sky' line in textures/strafe64/sky and
    inserts the sun keyword right after it (keyword order within a shader is
    irrelevant). Idempotent and a no-op if the anchor isn't present."""
    if "q3gl2_sun" in shader_script:
        return shader_script
    anchor = "\tsurfaceparm sky\n"
    idx = shader_script.find(anchor)
    if idx < 0:
        return shader_script          # no sky block — nothing to do
    cut = idx + len(anchor)
    return shader_script[:cut] + sun_keyword() + shader_script[cut:]


# ======================================================================
# COMPONENT SHADERS (recipes 1.5, 1.7, 2.1, 2.2)
# ======================================================================
#
# These are additional materials a generator can route faces to. They do NOT
# change any existing surf/wall/glow geometry — appending them is inert until
# something references them. Texture names match gfx_textures() below.
COMPONENT_SHADERS = """\
// ===================================================================
// strafegen_gfx component materials (docs/graphics-tricks-recipes.md)
// Appended by strafegen_gfx.augment(). Inert until a face references them.
// ===================================================================

// HULL — PBR-lite metal. A self-contained rend2 material: diffuse + normal +
// specular + parallax. Unlike the vertex-lit world (rgbGen exactVertex), this
// lights from the directional sun and self-shadows via the normal/parallax
// stages, so greebled panels gain real depth and a moving highlight. Use on
// pillars / hull walls where you want the surface to read as solid machined
// metal rather than a flat dev panel. (r_parallaxMapping must be >0; see
// render_cfg.) normalParallaxMap carries the height in the normal map's alpha.
textures/strafe64/hull
{
	qer_editorimage textures/strafe64/hull.tga
	q3map_normalImage textures/strafe64/hull_n.tga
	{
		stage diffuseMap
		map textures/strafe64/hull.tga
	}
	{
		stage normalParallaxMap
		map textures/strafe64/hull_n.tga
		normalScale 1.3 1.3
		parallaxDepth 0.04
	}
	{
		stage specularMap
		map textures/strafe64/hull_s.tga
		specularExponent 96
		specularReflectance 0.18
	}
}

// CHROME — fake reflective metal via tcGen environment (recipe 1.5). Projects a
// blurry sky-ish reflection from the view angle; no cubemap probe required, so
// it works on the GL1 path too. Additive over a dark base so it only adds
// highlight, never darkens. Good for trim, rails, accent struts.
textures/strafe64/chrome
{
	surfaceparm nolightmap
	{
		map $whiteimage
		rgbGen const ( 0.04 0.05 0.07 )
	}
	{
		map textures/strafe64/chrome.tga
		tcGen environment
		blendFunc GL_ONE GL_ONE
		rgbGen identity
	}
}

// PLASMA — turbulent energy panel (recipes 1.3 / 1.9). Two additive layers of a
// churning noise texture, swirled by tcMod turb and crawled by tcMod scroll, so
// the surface boils like contained plasma. Brightness rides the music bass
// envelope (rgbGen wave bass) so it pulses on the kick — set to a plain sine if
// you want it music-independent. Blooms hard under the GL2 HDR path.
textures/strafe64/plasma
{
	surfaceparm nolightmap
	surfaceparm nonsolid
	cull none
	{
		map textures/strafe64/plasma.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave bass 0.35 0.45 0 0
		tcMod turb 0 0.12 0 0.30
		tcMod scroll 0.05 0.03
	}
	{
		map textures/strafe64/plasma.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.25 0.20 0 0.13
		tcMod scale 2 2
		tcMod turb 0 0.08 0.5 0.21
		tcMod scroll -0.07 -0.02
	}
}

// BEAM — camera-facing energy strip (recipe 1.7, deformVertexes autosprite2).
// The quad keeps its long axis but always rotates its face toward the camera,
// so a flat strip reads as a round volumetric beam from every angle: lasers,
// conduits, tractor lines, the spine of a gate. Additive + bright core texture
// so bloom lifts it to neon. Map a thin tall quad and let the shader do the work.
strafe64/beam
{
	nopicmip
	cull none
	deformVertexes autosprite2
	{
		map textures/strafe64/beam.tga
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.7 0.3 0 0.4
	}
}
"""


def gfx_shaders():
    return COMPONENT_SHADERS


def augment(shader_script):
    """Full transform applied by strafegen at pk3-build time: arm the sun on the
    sky shader and append the component materials."""
    return inject_sun(shader_script) + "\n" + COMPONENT_SHADERS


# ======================================================================
# PROCEDURAL TEXTURES for the component shaders
# ======================================================================

_GFX_TEX_CACHE = None


def _value_noise(n, cells, seed, octaves=4):
    """Tileable value-noise field in [0,1], n x n, summed octaves."""
    rng = random.Random(seed)
    field = [0.0] * (n * n)
    amp_total = 0.0
    amp = 1.0
    c = cells
    for _ in range(octaves):
        # random lattice on a c x c grid, wrapping for seamless tiling
        lat = [[rng.random() for _ in range(c)] for _ in range(c)]

        def sample(fx, fy):
            gx, gy = fx * c, fy * c
            x0, y0 = int(gx) % c, int(gy) % c
            x1, y1 = (x0 + 1) % c, (y0 + 1) % c
            tx, ty = gx - int(gx), gy - int(gy)
            sx = tx * tx * (3 - 2 * tx)
            sy = ty * ty * (3 - 2 * ty)
            a = lat[y0][x0] + (lat[y0][x1] - lat[y0][x0]) * sx
            b = lat[y1][x0] + (lat[y1][x1] - lat[y1][x0]) * sx
            return a + (b - a) * sy

        for y in range(n):
            for x in range(n):
                field[y * n + x] += amp * sample(x / n, y / n)
        amp_total += amp
        amp *= 0.5
        c *= 2
    return [v / amp_total for v in field]


def _hull_height(n):
    """Greebled machined-panel height field in [0,1] (1 = raised)."""
    noise = _value_noise(n, 4, 0x4E11, octaves=4)
    h = [0.0] * (n * n)
    panel = n // 2                       # 2x2 panels per tile
    for y in range(n):
        for x in range(n):
            v = 0.62 + 0.18 * noise[y * n + x]      # base plate + grain
            # recessed seams between panels
            if x % panel < 2 or y % panel < 2:
                v -= 0.45
            # raised rivets near panel corners
            qx, qy = x % panel, y % panel
            for rx, ry in ((8, 8), (panel - 8, 8), (8, panel - 8),
                           (panel - 8, panel - 8)):
                if (qx - rx) ** 2 + (qy - ry) ** 2 <= 5:
                    v = 0.95
            # a diagonal vent slot across each panel
            if 10 < qx < panel - 10 and abs((qx) - (qy)) < 2:
                v -= 0.25
            h[y * n + x] = max(0.0, min(1.0, v))
    return h


def _build_gfx_textures():
    n = 128                              # hull at 128 for relief detail
    sn = 64                              # others at 64 (PSX-crisp, like the rest)

    # ---- HULL diffuse: mid-steel machined metal. Kept bright enough to read as
    # metal (not a dark blob) on the nolightmap vertex-lit world, where there's
    # little lighting to lift a dark albedo. Height bakes a faint AO so the
    # recessed seams/vents stay legible even with the sun nearly flat. ----
    h = _hull_height(n)
    diff = []
    base = (104, 110, 122)               # cool brushed steel
    for i in range(n * n):
        hv = h[i]
        k = 0.62 + 0.42 * hv             # recessed seams darken, plates stay bright
        diff.append((_clamp8(base[0] * k), _clamp8(base[1] * k),
                     _clamp8(base[2] * k)))

    # ---- HULL normal map from the height field (Sobel -> tangent normal) ----
    # Encode OpenGL-style (green = up). Height also written to ALPHA for the
    # normalParallaxMap stage (rend2 reads parallax depth from normalmap alpha).
    nrm = bytearray(struct.pack("<BBBHHBHHHHBB",
                                0, 0, 2, 0, 0, 0, 0, 0, n, n, 32, 8))
    STR = 2.2                            # bump strength
    # build rows bottom-up to match TGA orientation
    for y in range(n - 1, -1, -1):
        for x in range(n):
            hl = h[y * n + (x - 1) % n]
            hr = h[y * n + (x + 1) % n]
            hd = h[((y - 1) % n) * n + x]
            hu = h[((y + 1) % n) * n + x]
            nx = (hl - hr) * STR
            ny = (hd - hu) * STR
            nz = 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            nx, ny, nz = nx * inv, ny * inv, nz * inv
            r = _clamp8((nx * 0.5 + 0.5) * 255)
            g = _clamp8((ny * 0.5 + 0.5) * 255)
            b = _clamp8((nz * 0.5 + 0.5) * 255)
            a = _clamp8(h[y * n + x] * 255)     # height -> parallax
            nrm += bytes((b, g, r, a))
    hull_n = bytes(nrm)

    # ---- HULL specular/gloss: raised metal is shinier, recessed seams dull ----
    spec = []
    for i in range(n * n):
        s = _clamp8(40 + 150 * h[i])
        spec.append((s, s, _clamp8(s * 1.1)))

    # ---- CHROME env map: blurry reflected sky — bright warm top, dark floor,
    # a hot horizon band. tcGen environment projects this by view angle. ----
    chrome = []
    for y in range(sn):
        t = y / (sn - 1)                 # 0 top .. 1 bottom (image space)
        if t < 0.5:                      # upper hemisphere: warm dusk sky
            f = t / 0.5
            r = 180 - 120 * f
            g = 150 - 110 * f
            b = 120 - 70 * f
        else:                            # horizon flare -> dark ground
            f = (t - 0.5) / 0.5
            r = 220 - 200 * f
            g = 170 - 160 * f
            b = 120 - 110 * f
        for x in range(sn):
            chrome.append((_clamp8(r), _clamp8(g), _clamp8(b)))

    # ---- PLASMA: tileable turbulent noise, hot core, for additive turb panel --
    pn = _value_noise(sn, 3, 0xB10C, octaves=4)
    plasma = []
    for i in range(sn * sn):
        v = pn[i]
        v = v * v                        # sharpen into filaments
        r = _clamp8(40 + 215 * v)
        g = _clamp8(10 + 120 * v * v)
        b = _clamp8(80 + 175 * v)
        plasma.append((r, g, b))

    # ---- BEAM: bright vertical core falling off to the sides (for autosprite2)
    beam = []
    for y in range(sn):
        for x in range(sn):
            d = abs((x / (sn - 1)) - 0.5) * 2.0     # 0 centre .. 1 edge
            core = max(0.0, 1.0 - d)
            core = core * core * core               # tight hot core + haze
            iv = _clamp8(core * 255)
            beam.append((iv, _clamp8(iv * 0.9), _clamp8(iv * 0.8 + 30 * core)))

    return {
        "textures/strafe64/hull.tga":   _tga32(n, n, diff),
        "textures/strafe64/hull_n.tga": hull_n,
        "textures/strafe64/hull_s.tga": _tga32(n, n, spec),
        "textures/strafe64/chrome.tga": _tga32(sn, sn, chrome),
        "textures/strafe64/plasma.tga": _tga32(sn, sn, plasma),
        "textures/strafe64/beam.tga":   _tga32(sn, sn, beam),
    }


def gfx_textures():
    """{arcname: tga_bytes} for the component materials. Cached (deterministic)."""
    global _GFX_TEX_CACHE
    if _GFX_TEX_CACHE is None:
        _GFX_TEX_CACHE = _build_gfx_textures()
    return _GFX_TEX_CACHE


# ======================================================================
# RENDER CFG
# ======================================================================
#
# The renderergl2 fork defaults sun shadows, normal/specular, hdr and tonemap to
# ON, so q3gl2_sun and the hull material light correctly out of the box. The two
# features that default OFF and that these recipes want are parallax and SSAO.
# scripts/showcase.sh already sets most of this for the beauty launcher; this
# block is for booting a generated map directly (e.g. via engine_open) and still
# getting the full look.
def render_cfg():
    return (
        "// strafegen_gfx — recommended render cvars for the graphics recipes.\n"
        "// exec this after the map loads (or fold into your launcher).\n"
        "seta r_sunShadows 1\n"          # arm q3gl2_sun cascades (default 1)
        "seta r_sunlightMode 1\n"        # sun contributes to lighting (default 1)
        # sun ambient term: lower = deeper shadows = more contrast between the
        # sun-lit and shaded faces. 0.45 keeps the hull just legible while the
        # shaded side reads dark (paired with the forced tonemap below).
        "seta r_forceSunAmbientScale 0.45\n"
        # CONTRAST: pin the filmic tonemap curve instead of letting it auto-flatten.
        # Crushed blacks (min) + a hard white point (max) give the scene punch —
        # dark hull monoliths vs blown neon — not a flat mid-grey wash. Raise
        # r_forceToneMapMin toward -8 to lift the blacks (less contrast).
        "seta r_forceToneMap 1\n"
        "seta r_forceToneMapMin -5.5\n"
        "seta r_forceToneMapAvg -2.1\n"
        "seta r_forceToneMapMax 0.1\n"
        "seta r_shadowFilter 2\n"        # softest PCF — hides residual stair-stepping
        # these arenas are ~2800u across; the 1024 default spreads each shadow
        # texel over many units → jagged cascade edges in corners. 4096 keeps
        # the sun shadows crisp at this scale.
        "seta r_shadowMapSize 4096\n"
        "seta r_normalMapping 1\n"
        "seta r_specularMapping 1\n"
        "seta r_parallaxMapping 2\n"     # 2 = relief (off by default) — hull depth
        # SSAO OFF: renderergl2 computes it at quarter-res (tr.quarterImage) then
        # upscales, so on big flat dev surfaces it blocks up — "blocky at distance
        # and in the corners". Its only real contribution here is darkening corners,
        # which is exactly what aliases, so off looks cleaner than on. (Re-enabling
        # cleanly would need a full-res SSAO rework in the renderer.)
        "seta r_ssao 0\n"
        "seta r_hdr 1\n"
        "seta r_toneMap 1\n"
        # auto-exposure OVER-brightens the flat dev textures (it adapts to the
        # dark floor and blows the bright dusk sky to white). Use a fixed low
        # exposure instead. 0.55 keeps the map dampened/moody so the bloom + neon
        # do the work (paired with the darker dev textures + closer fog).
        "seta r_autoExposure 0\n"
        "seta r_cameraExposure 0.55\n"
        "vid_restart\n"                  # parallax/ssao need a renderer restart
    )
