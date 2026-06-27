"""strafegen_palettes — texture names, content/surface flags, section palettes.

The Source dev-texture colour scheme + the accent-glow routing (_glow_tex). Pure
constants and one small router; a leaf module everything else imports.
"""

import os

# ---- content / surface flags (game/surfaceflags.h) ----
CONTENTS_SOLID   = 0x00000001
CONTENTS_FOG     = 0x00000040   # volumetric fog (non-solid; renderer global fog)
CONTENTS_TRIGGER = 0x40000000
SURF_SKY         = 0x00000004
SURF_NOIMPACT    = 0x00000010
SURF_NODRAW      = 0x00000080

# ---- identity shader texture names ----
TEX_FLOOR   = "textures/strafe64/surf"
TEX_WALL    = "textures/strafe64/wall"
TEX_SKY     = "textures/strafe64/sky"
TEX_CAULK   = "textures/common/caulk"
TEX_TRIGGER = "textures/common/trigger"
# Concrete material for the lun3dm5 brutalist theme. A single pale-grey shader
# (one material does the whole map, like lun3dm5's c_crete6gs) on a 64u tile with
# a faint 16u panel grid. Always shipped in the shared pak; only referenced when
# the concrete theme is active, so it costs nothing under the default look.
TEX_CONCRETE = "textures/strafe64/crete"
# Photoreal sky + pale day fog for the concrete theme (see strafegen_shaders).
TEX_SKY_REAL = "textures/strafe64/skyreal"
TEX_FOG      = "textures/strafe64/fog"
TEX_FOG_DAY  = "textures/strafe64/fog_day"


# ---- Source dev-texture palettes + accent-glow routing ----
SRC_ORANGE = (222, 138, 70)    # dev_measuregeneric01b orange — bulk floors/decks
SRC_GREY   = (152, 152, 158)   # dev_measuregeneric01  grey   — bulk walls/pillars
SRC_TRIM   = (240, 182, 120)   # lighter orange — ledges / mantle edges
SRC_BLUE   = (64, 104, 196)    # dev_measuregeneric blue — deep-blue velodrome ring

# structural sections -> Source dev base (geometry you run on / along)
PAL_GAPS   = SRC_ORANGE
PAL_BHOP   = SRC_ORANGE
PAL_SLIDE  = SRC_ORANGE
PAL_TOWER  = SRC_ORANGE
PAL_PLAIN  = SRC_ORANGE
PAL_WALLS  = SRC_GREY          # wallrun walls are grey base now (see note)

# gameplay-critical ACCENTS -> keep vivid identity (read the point at speed)
PAL_START  = (150, 255, 150)   # spawn pad — green "you are here"
PAL_FINISH = (150, 255, 220)   # goal line — teal
PAL_CHECK  = (120, 240, 255)   # checkpoint pads — bright cyan

# arena palettes
PAL_FLOORA = SRC_ORANGE
PAL_BANK   = SRC_BLUE           # velodrome ring — deep-blue dev grid (gallery look)
PAL_WALLA  = SRC_GREY
PAL_CENTER = SRC_ORANGE
PAL_PILLAR = SRC_GREY
PAL_LEDGE  = SRC_TRIM           # mantle ledges — lighter-orange trim
PAL_PAD    = (255, 255, 150)    # jump pads — yellow (accent)
PAL_GATE   = (255, 245, 120)    # gates / forks / portal frames — yellow (accent)
PAL_DANGER = (255, 95, 95)      # hazards — red (accent)
# killbox arena: bulk deck/walls join the Source dev base; the neon portal
# frames + magenta wall-jump columns stay as accents. (Old cold-neon identity:
# DECK (70,80,110), WALL (60,110,190) — repoint these two to revert.)
PAL_KB_DECK   = SRC_ORANGE
PAL_KB_WALL   = SRC_GREY
PAL_KB_NEON   = (90, 230, 255)     # cyan: portals, edges, the spire crown (accent)
PAL_KB_COLUMN = (210, 90, 255)     # magenta wall-jump columns (accent)

# Accent palettes that should read as self-illuminated NEON BEACONS rather than
# matte dev panels. Faces wearing one of these are routed (by _glow_tex, at
# shader-selection time) to the additive glow_floor/glow_wall shaders, so the
# bright accent colour blooms under the GL2 HDR path. Geometry is untouched —
# only which shader the face references changes. The bulk orange/grey dev base
# (SRC_ORANGE/SRC_GREY) is deliberately excluded so the track stays readable.
ACCENT_GLOW = frozenset((
    PAL_START, PAL_FINISH, PAL_CHECK, PAL_PAD, PAL_GATE, PAL_DANGER,
    PAL_KB_NEON, PAL_KB_COLUMN,
))
TEX_FLOOR_GLOW = "textures/strafe64/glow_floor"
TEX_WALL_GLOW  = "textures/strafe64/glow_wall"


def _glow_tex(tex, palette):
    """Map an accent-coloured floor/wall face to its additive glow shader."""
    if palette in ACCENT_GLOW:
        if tex == TEX_FLOOR:
            return TEX_FLOOR_GLOW
        if tex == TEX_WALL:
            return TEX_WALL_GLOW
    return tex


# ---- concrete theme (lun3dm5 brutalist look) ------------------------------
# Concrete vertex palettes. crete.tga is now a REAL concrete photo (mean grey
# ~155), and rgbGen exactVertex multiplies the vertex colour THROUGH it, so these
# palettes are near-white brightness/shade controls (not the grey itself — that
# lives in the photo). A few shade variants (lun3dm5 ships c_crete6g / 6gs / _lo)
# tint adjacent cube faces so the sun-dome + variation read as eroded mass, not
# one flat tone. Final on-screen ≈ photo(~0.6) × palette × light-dome.
PAL_CRETE      = (228, 228, 232)   # walls / bulk concrete (neutral)
PAL_CRETE_WARM = (232, 228, 220)   # floors / decks (a hair warm, like sunlit slab)
PAL_CRETE_TRIM = (245, 243, 240)   # ledges / lighter trim
PAL_CRETE_DARK = (196, 196, 204)   # recessed / greeble shadow blocks

# Bulk dev palettes the concrete theme repaints (accents are deliberately NOT
# listed — start/finish/pads/hazards keep their vivid identity and pop against
# the grey). SRC_BLUE (velodrome ring) folds into plain concrete too.
_CONCRETE_PAL = {
    SRC_ORANGE: PAL_CRETE_WARM,
    SRC_TRIM:   PAL_CRETE_TRIM,
    SRC_GREY:   PAL_CRETE,
    SRC_BLUE:   PAL_CRETE,
}
# Bulk dev shaders the concrete theme reroutes: floors/walls -> one crete
# material. The sky -> photoreal box swap is handled separately in theme_remap,
# gated on the baked cube actually being present (see _have_baked_sky). (The fog
# volume is swapped separately in the BSP writer, which builds it directly.)
_CONCRETE_TEX = {TEX_FLOOR: TEX_CONCRETE, TEX_WALL: TEX_CONCRETE}


def _have_baked_sky():
    """True once the photoreal sky cube has been baked into ``skytex/`` by
    skybox_from_photo.py (realsky_<side>.tga). Until those photos exist the
    concrete theme keeps the procedural 'Bryce' synthsky (textures/strafe64/sky)
    so a map NEVER references a missing skybox — the photoreal sky is a drop-in
    OVERRIDE, not a hard dependency. Cheap stat; the bake is a one-time step."""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.exists(os.path.join(here, "skytex", "realsky_ft.tga"))


def theme_fog(theme="default"):
    """Fog shader name for the active theme's global fog volume."""
    return TEX_FOG_DAY if theme == "concrete" else TEX_FOG


def theme_remap(tex, palette, theme="default"):
    """Effective (tex, palette) for a face under the active art theme.

    "default" returns the inputs unchanged (maps stay byte-identical). "concrete"
    repaints the bulk dev floors/walls to the single pale-concrete material +
    grey palettes, leaving accents (and already-concrete greeble) untouched.
    Composed BEFORE _glow_tex, so accent faces still route to their glow shaders.
    """
    if theme == "concrete":
        palette = _CONCRETE_PAL.get(palette, palette)
        tex = _CONCRETE_TEX.get(tex, tex)
        # Photoreal sky is an override: swap only when the baked cube is present,
        # else leave the procedural Bryce synthsky in place (no broken skybox).
        if tex == TEX_SKY and _have_baked_sky():
            tex = TEX_SKY_REAL
    return tex, palette
