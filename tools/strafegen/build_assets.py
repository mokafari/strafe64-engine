#!/usr/bin/env python3
"""build_assets — rebuild every photo asset strafegen ships, from assets_src/.

    python3 build_assets.py            # -> skytex/ (gitignored, regenerable)

skytex/ holds the built textures the shared pak bundles; it is gitignored
because it is derived. Everything it is derived FROM lives in assets_src/
(tracked), so a fresh clone on any OS rebuilds identical assets with plain
Python 3 + Pillow + NumPy -- no macOS tools involved:

  assets_src/materials/*.png  Codex-generated sources -> skytex/m/<name>.jpg
                              (2048 albedo) + <name>_nh.png (normal/height),
                              via build_materials.py
  assets_src/skies/*.png      Codex photo skies -> skytex/ethsky_* (dusk) and
                              skytex/ethnight_* (night) cube faces, via
                              ethsky_from_pano.py (sun/moon column below)
  assets_src/concrete/        crete_eth_source.png -> skytex/crete_eth.tga
                              (ethereal board-formed concrete), and lossless
                              PNGs of the concrete theme's photo assets ->
                              skytex/crete.tga + skytex/realsky_*.tga
"""
import os
import shutil
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "assets_src")
OUT = os.path.join(HERE, "skytex")
sys.path.insert(0, HERE)

# horizontal position of the sun/moon in each sky photo (measured once; the
# projection crops the photo to start there so the disc appears exactly once)
SKIES = {"ethsky": ("ethsky.png", 0.80, 0.353),
         "ethnight": ("ethnight.png", 0.81, 0.495)}


def _tga(png, tga):
    Image.open(png).convert("RGBA").save(tga)


def crete_eth(src_png, out_tga):
    """Board-formed concrete: native 1024 centre crop -> 512, 2x2 mirror tile
    (seamless), mean normalised to ~155 so crete's palettes/lighting carry over."""
    im = Image.open(src_png).convert("RGB")
    w, h = im.size
    c = 1024
    im = im.crop(((w - c) // 2, (h - c) // 2, (w + c) // 2, (h + c) // 2)).resize(
        (512, 512), Image.LANCZOS)
    t = Image.new("RGB", (1024, 1024))
    t.paste(im, (0, 0))
    t.paste(im.transpose(Image.FLIP_LEFT_RIGHT), (512, 0))
    t.paste(im.transpose(Image.FLIP_TOP_BOTTOM), (0, 512))
    t.paste(im.transpose(Image.ROTATE_180), (512, 512))
    px = np.asarray(t, np.float32)
    k = 155.0 / px[::4, ::4].mean()
    Image.fromarray(np.clip(px * k, 0, 255).astype(np.uint8)).save(out_tga)


def main():
    import build_materials
    import ethsky_from_pano

    os.makedirs(os.path.join(OUT, "m"), exist_ok=True)
    print("materials ->", os.path.join(OUT, "m"))
    for name, (png, strength, blur) in build_materials.SOURCES.items():
        build_materials.build(os.path.join(SRC, "materials", png + ".png"),
                              os.path.join(OUT, "m"), name, strength, blur)
        print("  ", name)

    for prefix, (png, horizon, sun_u) in SKIES.items():
        ethsky_from_pano.build(os.path.join(SRC, "skies", png), horizon_v=horizon,
                               prefix=prefix, out_dir=OUT, sun_u=sun_u)
        shutil.copy(os.path.join(SRC, "skies", png), os.path.join(OUT, prefix + "_source.png"))

    con = os.path.join(SRC, "concrete")
    crete_eth(os.path.join(con, "crete_eth_source.png"), os.path.join(OUT, "crete_eth.tga"))
    _tga(os.path.join(con, "crete.png"), os.path.join(OUT, "crete.tga"))
    for side in ("rt", "lf", "ft", "bk", "up", "dn"):
        _tga(os.path.join(con, f"realsky_{side}.png"), os.path.join(OUT, f"realsky_{side}.tga"))
    print("concrete + photo skies ->", OUT)


if __name__ == "__main__":
    main()
