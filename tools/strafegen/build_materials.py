#!/usr/bin/env python3
"""build_materials — Codex source PNGs -> the shipped ethereal material set.

    python3 build_materials.py SRC_DIR [OUT_DIR=skytex/m]

For each material (strafegen_materials.MATERIALS):
  * <name>.jpg     2048 seamless albedo: a native-resolution 1024 centre crop,
                   2x2 mirror-tiled (seamless whatever the source edges do) --
                   no downscaling, so Codex's detail survives;
  * <name>_nh.png  2048 tangent-space normal (RGB, +Y up) with height in alpha.
                   rend2 auto-loads <diffuse>_nh for lightmapped surfaces and
                   parallax-maps from the alpha. Height is RELIEF only: the
                   luminance minus its broad shading, so joints, board edges,
                   tie holes and grating gaps recess while stains stay flat.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

# name: (Codex source png, relief strength, detail blur px)
SOURCES = {
    # detail blur is wide enough to drop pores/aggregate (they made the relief
    # read as lumpy stucco) while keeping joints, board edges and bolt holes
    "board":    ("board2", 3.0, 2.6),
    "slab":     ("conc_floor_slab", 2.0, 3.0),
    "panel":    ("conc_smooth_panel", 3.0, 2.2),
    "weather":  ("weather2", 2.5, 2.4),
    "grate":    ("metal_grate", 9.0, 0.8),
    "trim":     ("metal_trim", 1.5, 2.0),
    "polished": ("polished", 1.5, 3.0),
    "glass":    ("glass", 3.0, 1.2),
    "marble_white":   ("marble_white", 1.0, 3.0),
    "marble_black":   ("marble_black", 1.0, 3.0),
    "marble_tiles":   ("marble_tiles", 2.5, 1.5),
    "granite_grey":   ("granite_grey", 1.0, 2.0),
    "granite_flamed": ("granite_flamed", 2.5, 1.2),
    "brass":          ("brass_trim", 1.0, 2.0),
}


def mirror(im):
    w, h = im.size
    t = Image.new(im.mode, (2 * w, 2 * h))
    t.paste(im, (0, 0))
    t.paste(im.transpose(Image.FLIP_LEFT_RIGHT), (w, 0))
    t.paste(im.transpose(Image.FLIP_TOP_BOTTOM), (0, h))
    t.paste(im.transpose(Image.ROTATE_180), (w, h))
    return t


# Organic materials (stone, weathering) get a true seamless tile: mirroring
# turns veins into kaleidoscope diamonds. Regular grids (panels, joints, tiles,
# boards, grating) keep the 2x2 mirror, which their symmetry hides.
ORGANIC = {"weather", "marble_white", "marble_black", "granite_grey",
           "granite_flamed", "polished", "brass"}


def seamless(im, feather=0.22):
    """Make a square image tile: blend it with its half-offset copy, the copy
    winning near the edges (where the original's opposite edges would meet)."""
    a = np.asarray(im, np.float32)
    n = a.shape[0]
    rolled = np.roll(np.roll(a, n // 2, 0), n // 2, 1)
    t = np.linspace(0.0, 1.0, n)
    edge = np.minimum(t, 1.0 - t) / feather          # 0 at edges -> 1 inside
    w1 = np.clip(edge, 0, 1)
    w1 = w1 * w1 * (3 - 2 * w1)
    w = (w1[:, None] * w1[None, :])[..., None]
    return Image.fromarray(np.clip(a * w + rolled * (1 - w), 0, 255).astype(np.uint8))


def build(src_png, out_dir, name, strength, blur):
    im = Image.open(src_png).convert("RGB")
    w, h = im.size
    if name in ORGANIC:
        c = min(w, h)                                  # the whole image, once
        q = im.crop(((w - c) // 2, (h - c) // 2, (w + c) // 2, (h + c) // 2))
        alb = seamless(q).resize((2048, 2048), Image.LANCZOS)
    else:
        c = min(w, h, 1024)
        q = im.crop(((w - c) // 2, (h - c) // 2, (w + c) // 2, (h + c) // 2))
        if c < 1024:
            q = q.resize((1024, 1024), Image.LANCZOS)
        alb = mirror(q)
    alb.save(os.path.join(out_dir, name + ".jpg"), quality=93, subsampling=0)

    g = alb.convert("L")                     # full 2048 relief, matching the albedo
    fine = np.asarray(g.filter(ImageFilter.GaussianBlur(blur * 2)), np.float32) / 255.0
    broad = np.asarray(g.filter(ImageFilter.GaussianBlur(80)), np.float32) / 255.0
    hgt = fine - broad
    lo, hi = np.percentile(hgt, 1), np.percentile(hgt, 99)
    hgt = np.clip((hgt - lo) / (hi - lo + 1e-6), 0.0, 1.0)
    # wrap-aware central differences (the tile is seamless)
    dx = (np.roll(hgt, -1, 1) - np.roll(hgt, 1, 1)) * 0.5
    dy = (np.roll(hgt, -1, 0) - np.roll(hgt, 1, 0)) * 0.5
    n = np.dstack((-dx * strength, dy * strength, np.ones_like(hgt)))
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    rgba = np.dstack(((n * 0.5 + 0.5) * 255.0, hgt * 255.0)).astype(np.uint8)
    Image.fromarray(rgba, "RGBA").save(os.path.join(out_dir, name + "_nh.png"),
                                       optimize=True)


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "skytex", "m")
    os.makedirs(out, exist_ok=True)
    for name, (png, strength, blur) in SOURCES.items():
        build(os.path.join(src, png + ".png"), out, name, strength, blur)
        print(name, "ok")


if __name__ == "__main__":
    main()
