"""strafegen_materials — the ethereal material library (Codex-generated set).

Each material is a real-world-scaled concrete/steel texture with a vertex-lit
shader (rgbGen exactVertex, like crete) and a lightmapped *_lm twin that the
bake routes to (strafegen_bake.baked_tex / baked_variants). Textures live in
skytex/m/ (built by build_materials.py from the Codex sources; gitignored like
the other photo assets) and ship in the shared pak via
strafegen_textures._load_baked: <name>.jpg 2048 albedo + <name>_nh.png
normal/height, which rend2 auto-loads for lightmapped surfaces (parallax from
the alpha) once the bake carries deluxe maps.

World texture ST is 1 repeat / 64u, so `scale` s makes one tile span 64/s
units; each scale below is set so the texture's features land at believable
sizes against a 56u player (panels ~128u, boards ~8u, grating bars ~4u).
"""

TEX_M = "textures/strafe64/m/"

# name -> (tga file, tcMod scale, one-line description)
MATERIALS = {
    # grid materials are 2x2 mirror tiles (the source twice per axis); organic
    # ones (build_materials.ORGANIC) are one seamless image per tile
    "board":    ("board.jpg",    0.20,  "refined board-formed concrete, ~8u boards"),
    "panel":    ("panel.jpg",    0.0625, "precast panels ~128u, bolt caps"),
    "slab":     ("slab.jpg",     0.083, "troweled floor slab, saw-cut joints ~128u"),
    "weather":  ("weather.jpg",  0.25,  "softly streaked exterior concrete"),
    "grate":    ("grate.jpg",    1.00,  "dark galvanized bar grating, ~4u cells"),
    "trim":     ("trim.jpg",     0.50,  "brushed gunmetal plate with bolts"),
    "polished": ("polished.jpg", 0.125, "dark honed terrazzo concrete, large grid"),
    "glass":    ("glass.jpg",    0.0625, "black smoked-glass curtain wall, gunmetal mullions"),
    # marble & granite set (strafe64_marble)
    "marble_white":   ("marble_white.jpg",   0.125, "polished Carrara, soft grey veining"),
    "marble_black":   ("marble_black.jpg",   0.125, "polished Nero Marquina, white veins"),
    "marble_tiles":   ("marble_tiles.jpg",   0.0625, "large-format white marble floor tiles"),
    "granite_grey":   ("granite_grey.jpg",   0.5,   "polished salt-and-pepper granite"),
    "granite_flamed": ("granite_flamed.jpg", 0.5,   "flamed charcoal granite, matte"),
    "brass":          ("brass.jpg",          0.5,   "brushed champagne brass"),
}


# per-material relief in the lightmapped (_lm) shader: (normalScale, parallaxDepth).
# Parallax only on strongly structured materials -- on noisy concrete heights it
# smears ("liquid" swim at grazing angles); those get plain normal mapping.
RELIEF = {
    # structure-only relief (build_materials blurs pores away); parallax only
    # where real depth exists -- on concrete it melted the surface into lumps
    "board":    (0.7, 0.0),
    "panel":    (0.7, 0.0),
    "slab":     (0.3, 0.0),
    "weather":  (0.6, 0.0),
    "grate":    (1.2, 0.020),
    "trim":     (0.4, 0.0),
    "polished": (0.25, 0.0),
    "glass":    (0.5, 0.0),
    "marble_white":   (0.12, 0.0),     # polished stone is optically flat
    "marble_black":   (0.12, 0.0),
    "marble_tiles":   (0.5, 0.0),      # grout lines only
    "granite_grey":   (0.15, 0.0),
    "granite_flamed": (0.6, 0.0),
    "brass":          (0.3, 0.0),
}


def relief_stage(name):
    """rend2 normal(/parallax) stage for a material's _lm shader."""
    ns, depth = RELIEF[name]
    nh = TEX_M + name + "_nh.png"
    # rend2 copies this stage's own texmods into the normal bundle, so it must
    # repeat the diffuse's tcMod or the relief lands at the wrong size
    sc = MATERIALS[name][1]
    tcm = f"\t\ttcMod scale {sc:g} {sc:g}\n"
    if depth > 0:
        return (f"\t{{\n\t\tstage normalParallaxMap\n\t\tmap {nh}\n"
                + tcm + f"\t\tnormalScale {ns:g} {ns:g}\n\t\tparallaxDepth {depth:g}\n\t}}\n")
    return (f"\t{{\n\t\tstage normalMap\n\t\tmap {nh}\n" + tcm
            + f"\t\tnormalScale {ns:g} {ns:g}\n\t}}\n")


def tex(name):
    """Shader name for a material (vertex-lit form)."""
    return TEX_M + name


ALL_TEX = tuple(tex(n) for n in MATERIALS)


def shader_blocks():
    """Vertex-lit shader text for every material (the *_lm twins are derived
    from these by strafegen_bake.lightmap_world_shaders)."""
    out = ["// ---- ethereal material library (strafegen_materials) ----\n"]
    for name, (tga, scale, desc) in MATERIALS.items():
        out.append(
            f"// {desc}\n"
            f"{tex(name)}\n{{\n"
            f"\tqer_editorimage textures/strafe64/d_wall.tga\n"
            f"\tsurfaceparm nolightmap\n"
            f"\t{{\n"
            f"\t\tmap {TEX_M}{tga}\n"
            f"\t\ttcMod scale {scale:g} {scale:g}\n"
            f"\t\trgbGen exactVertex\n"
            f"\t}}\n}}\n")
    return "".join(out)
