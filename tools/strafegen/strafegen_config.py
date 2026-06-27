"""strafegen_config — shared runtime toggles.

A single live home for flags that several modules read at build time and the CLI
mutates. Reference as ``cfg.GFX`` (not ``from strafegen_config import GFX``) so the
``--no-gfx`` toggle in main() propagates to every module.
"""

# Apply the graphics-recipe shaders + sun (strafegen_gfx) to packed maps and route
# centerpieces to the machined-metal hull/chrome materials. Cleared by --no-gfx.
GFX = True

# Active art theme, read at BSP-emit time (strafegen_bsp) to remap the bulk
# dev-texture surfaces to an alternate material. "concrete" = the HOUSE STYLE
# since the realism pivot: surrealist brutalist pale-concrete + Bryce sun/sky
# (see ART_DIRECTION.md, strafegen_palettes.theme_remap + the greeble pass in
# strafegen_geom). "default" = the legacy Source dev look (orange floors / grey
# walls), kept for diffing / nostalgia. Set by --theme.
THEME = "concrete"


class GenMods:
    """Geometry modifiers threaded into the builders. Defaults are IDENTITY — a
    fresh GenMods() leaves every dimension/count/section untouched, so output is
    byte-for-byte unchanged unless the caller dials a knob. Builders multiply
    their own named scale/count variables by these and consult ``enabled()`` to
    drop optional sections.

      vscale   — vertical dimension multiplier (heights, deck rise, bank rise)
      hscale   — horizontal dimension / spacing multiplier (widths, radii, gaps)
      density  — placement-count multiplier (pillars, hazards, pads, enemies)
      sections — None = all section types; else a set of enabled section keys
    """

    __slots__ = ("vscale", "hscale", "density", "sections")

    def __init__(self, vscale=1.0, hscale=1.0, density=1.0, sections=None):
        self.vscale = float(vscale)
        self.hscale = float(hscale)
        self.density = float(density)
        self.sections = set(sections) if sections else None

    def enabled(self, key):
        """True if a section keyed *key* should be generated (always True when no
        explicit section filter is set)."""
        return self.sections is None or key in self.sections

    def count(self, n, lo=0):
        """Scale an integer placement count by density, clamped to >= lo."""
        return max(lo, int(round(n * self.density)))

    @property
    def identity(self):
        return (self.vscale == 1.0 and self.hscale == 1.0
                and self.density == 1.0 and self.sections is None)

