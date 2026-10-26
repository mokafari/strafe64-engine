#!/usr/bin/env python3
"""build_materials — Codex source PNGs -> the shipped ethereal material set.

    python3 build_materials.py SRC_DIR [OUT_DIR=skytex/m]

For each material (strafegen_materials.MATERIALS):
  * <name>.jpg     2048 seamless albedo: a native-resolution 1024 centre crop,
                   2x2 mirror-tiled (seamless whatever the source edges do) --
                   no downscaling, so Codex's detail survives;
  * <name>_nh.png  1024 tangent-space normal (RGB, +Y up) with height in alpha.
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
    "board":   ("conc_board_fine", 5.0, 1.2),
    "slab":    ("conc_floor_slab", 3.0, 1.5),
    "panel":   ("conc_smooth_panel", 4.0, 1.2),
    "weather": ("conc_weathered", 6.0, 1.0),
    "grate":   ("metal_grate", 9.0, 0.8),
    "trim":    ("metal_trim", 2.5, 0.8),
}


def mirror(im):
    w, h = im.size
    t = Image.new(im.mode, (2 * w, 2 * h))
    t.paste(im, (0, 0))
    t.paste(im.transpose(Image.FLIP_LEFT_RIGHT), (w, 0))
    t.paste(im.transpose(Image.FLIP_TOP_BOTTOM), (0, h))
    t.paste(im.transpose(Image.ROTATE_180), (w, h))
    return t


def build(src_png, out_dir, name, strength, blur):
    im = Image.open(src_png).convert("RGB")
    w, h = im.size
    c = min(w, h, 1024)
    q = im.crop(((w - c) // 2, (h - c) // 2, (w + c) // 2, (h + c) // 2))
    if c < 1024:
        q = q.resize((1024, 1024), Image.LANCZOS)
    alb = mirror(q)
    alb.save(os.path.join(out_dir, name + ".jpg"), quality=93, subsampling=0)

    g = alb.resize((1024, 1024), Image.LANCZOS).convert("L")
    fine = np.asarray(g.filter(ImageFilter.GaussianBlur(blur)), np.float32) / 255.0
    broad = np.asarray(g.filter(ImageFilter.GaussianBlur(40)), np.float32) / 255.0
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
