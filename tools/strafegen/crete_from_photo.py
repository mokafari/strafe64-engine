#!/usr/bin/env python3
"""crete_from_photo — bake a real concrete PHOTO into the brutalist crete.tga.

The concrete (house-style) theme multiplies a near-white vertex colour THROUGH a
single grey concrete tile (textures/strafe64/crete, rgbGen exactVertex), so the
tile carries all the real surface detail — pitting, panel seams, weathering —
while the palette + sun-dome do the shading. Until a real photo is baked, the
generator falls back to a procedural tile (strafegen_textures.build_detail_
textures); drop a photo through this script to ship the real thing.

    python3 crete_from_photo.py --src skytex_src/concrete.jpg

Writes skytex/crete.tga (square, power-of-two), which _load_baked() picks up and
OVERRIDES the procedural tile with. Needs Pillow (`pip install Pillow`) — run it
on a workstation, not in a headless/PIL-free build box.

Notes:
  * CC0 concrete from ambientCG / Poly Haven is ALREADY seamless — just square-
    resize (the default). Pass --tileable only for a non-tiling photo; it offset-
    feathers the seams (softer, but no hard edge when the tile repeats).
  * --mean re-centres the average grey so the exactVertex multiply lands at the
    intended brightness (~155 keeps pale concrete pale, matching ART_DIRECTION).
"""
import argparse
import os

from PIL import Image, ImageChops

from strafegen_tga import _tga32


def bake(src, size=512, tileable=False, mean=155):
    im = Image.open(src).convert("RGB")
    # centre square crop, then resize to the power-of-two tile size
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))
    im = im.resize((size, size), Image.LANCZOS)

    if tileable:
        # offset by half a tile and feather a cross-blend over the (now interior)
        # seams so the edges wrap without a hard line. Good enough for concrete;
        # a natively-seamless source (ambientCG/Poly Haven) needs none of this.
        off = ImageChops.offset(im, size // 2, size // 2)
        im = Image.blend(im, off, 0.5)

    px = list(im.getdata())
    if mean:
        cur = sum(r + g + b for r, g, b in px) / (3 * len(px))
        adj = mean - cur
        px = [(_c(r + adj), _c(g + adj), _c(b + adj)) for r, g, b in px]

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "skytex")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "crete.tga")
    with open(path, "wb") as fh:
        fh.write(_tga32(size, size, px))
    print(f"wrote {size}x{size} concrete tile (mean grey {mean}) -> {path}")
    return path


def _c(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(
        os.path.dirname(__file__), "skytex_src", "concrete.jpg"),
        help="source concrete photo (jpg/png)")
    ap.add_argument("--size", type=int, default=512, help="tile size (pow2)")
    ap.add_argument("--tileable", action="store_true",
                    help="offset-feather the seams (skip for seamless sources)")
    ap.add_argument("--mean", type=int, default=155,
                    help="re-centre average grey (0 disables)")
    a = ap.parse_args()
    bake(a.src, a.size, a.tileable, a.mean)
