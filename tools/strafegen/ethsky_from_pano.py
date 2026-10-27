#!/usr/bin/env python3
"""ethsky_from_pano — the ethereal theme's sky cube from one wide sky photo.

skybox_from_photo.py is built for a daylight photo whose lower part is ground:
below the horizon it fades to plain haze. The ethereal sky (Codex image gen:
cirrus over a low gold sun, a cloud sea below, faint megastructures on the
horizon) keeps its best part BELOW the horizon, so this projects it as:

  * above the horizon: elevation 0..EL_TOP maps the photo's sky band
    (horizon_v .. top row); beyond EL_TOP the top row eases into the photo's
    mean zenith colour so the 'up' face has no pinch/mirror seam;
  * below the horizon: elevation 0..-EL_BOT maps the cloud sea
    (horizon_v .. bottom row), then fades to the sea's own deep tone;
  * azimuth: the photo spans `fov` degrees, triangle-mirrored around the back.
    The mirror is only seamless at the back (+-180deg) when 360 is a multiple of
    2*fov, so fov is 180 (200 left a hard seam behind the sun).

Same tr_sky.c face basis as skybox_from_photo (FACES), so it can't shear.

    python3 ethsky_from_pano.py SRC [--horizon 0.80] [--fov 200] [--size 1024]
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageFilter

from skybox_from_photo import FACES
from strafegen_tga import _tga32

EL_TOP = np.radians(55.0)
EL_BOT = np.radians(22.0)


def _cut_disc(pil, sun_u, horizon_v, hfov, crop_w):
    """Find the sun/moon disc near column sun_u and return a sprite + its sky
    direction + angular size, so it can be drawn round. The background mapping
    stretches this photo ~2x horizontally (too narrow for 180deg of azimuth),
    which turned the disc into a wide ellipse."""
    a = np.asarray(pil, np.float32)
    H, W, _ = a.shape
    lum = a.mean(axis=2)
    # detect on a blurred copy: single-pixel stars are as bright as the disc's
    # core, but only the disc survives a blur
    blur = np.asarray(pil.convert("L").filter(ImageFilter.GaussianBlur(6)), np.float32)
    cx = int(sun_u * W)
    band = blur[:, max(0, cx - W // 20): cx + W // 20]
    cy = int(np.argmax(band.max(axis=1)))
    cx = max(0, cx - W // 20) + int(np.argmax(band[cy]))
    # radius: how far the bright core extends horizontally from the centre
    row = lum[cy]
    thr = row[cx] * 0.92                                 # the bright core, not the glow
    r = 4
    while cx + r < W - 1 and row[cx + r] > thr and r < W // 50:
        r += 1
    r = int(r * 2.0)                                     # include a little halo
    y0, y1 = max(0, cy - r), min(H, cy + r)
    x0, x1 = max(0, cx - r), min(W, cx + r)
    sprite = a[y0:y1, x0:x1].copy()
    # erase the disc from the photo (row-wise interpolation from its left/right
    # neighbours) so the stretched original can't show under the round sprite
    for yy in range(y0, y1):
        lft, rgt = a[yy, max(0, x0 - 1)], a[yy, min(W - 1, x1)]
        t = np.linspace(0.0, 1.0, x1 - x0)[:, None]
        a[yy, x0:x1] = lft * (1 - t) + rgt * t
    pil.paste(Image.fromarray(a.astype(np.uint8)))
    deg_per_px = hfov / crop_w                           # horizontal scale = true
    el = (1.0 - cy / (horizon_v * H)) * EL_TOP           # same as the background
    sdir = np.array([np.cos(el) * np.cos(np.pi / 2), np.cos(el) * np.sin(np.pi / 2), np.sin(el)])
    return sprite, sdir, deg_per_px, (cy - y0, cx - x0)


def _composite_disc(px, dx, dy, dz, sprite, sdir, ang_per_px, centre):
    """Gnomonic-project the sprite around sdir; blend it in with a soft edge
    (lighten, so the halo adds to the sky instead of pasting a rectangle)."""
    up = np.array([0.0, 0.0, 1.0])
    e1 = np.cross(sdir, up); e1 /= np.linalg.norm(e1)    # horizontal, screen-right
    e2 = np.cross(e1, sdir)                               # screen-up
    dot = dx * sdir[0] + dy * sdir[1] + dz * sdir[2]
    x = (dx * e1[0] + dy * e1[1] + dz * e1[2]) / np.maximum(dot, 1e-6)
    y = (dx * e2[0] + dy * e2[1] + dz * e2[2]) / np.maximum(dot, 1e-6)
    sx = centre[1] + x / ang_per_px
    sy = centre[0] - y / ang_per_px
    h, w, _ = sprite.shape
    ok = (dot > 0) & (sx >= 0) & (sx < w - 1) & (sy >= 0) & (sy < h - 1)
    out = px.copy()
    xi, yi = sx[ok].astype(int), sy[ok].astype(int)
    src = sprite[yi, xi]
    rr = np.hypot((sx[ok] - centre[1]) / (w / 2), (sy[ok] - centre[0]) / (h / 2))
    a = np.clip((1.0 - rr) / 0.35, 0, 1)[:, None]          # feather to the edge
    out[ok] = np.maximum(out[ok], out[ok] * (1 - a) + src * a)
    return out


def build(src, horizon_v=0.80, fov_deg=180.0, size=1024, prefix="ethsky", out_dir=None,
          sun_u=None):
    """sun_u: the sun/moon's horizontal position in the photo (0..1). The mirror
    wrap shows every column twice except the photo's two EDGES, so the photo is
    cropped to start at the sun: it then appears exactly once, at azimuth +90deg
    (aim the baked/runtime sun there)."""
    pil = Image.open(src).convert("RGB")
    disc = None
    if sun_u is not None:
        disc = _cut_disc(pil, sun_u, horizon_v, np.radians(fov_deg), pil.width * (1 - sun_u))
        pil = pil.crop((int(sun_u * pil.width), 0, pil.width, pil.height))
    im = np.asarray(pil, np.float32)
    H, W, _ = im.shape
    top = im[: max(2, H // 40)].mean(axis=(0, 1))          # zenith tone
    deep = im[-max(2, H // 30):].mean(axis=(0, 1)) * 0.80  # cloud-sea depths
    hfov = np.radians(fov_deg)
    out_dir = out_dir or os.path.join(os.path.dirname(os.path.abspath(__file__)), "skytex")

    j, i = np.mgrid[0:size, 0:size].astype(np.float32)
    sx = 2.0 * i / (size - 1) - 1.0
    sy = 1.0 - 2.0 * j / (size - 1)
    for side, (F, R, U) in FACES.items():
        v3 = [F[k] + sx * R[k] + sy * U[k] for k in range(3)]
        n = np.sqrt(v3[0] ** 2 + v3[1] ** 2 + v3[2] ** 2)
        dx, dy, dz = v3[0] / n, v3[1] / n, v3[2] / n
        el = np.arcsin(np.clip(dz, -1, 1))
        az = np.arctan2(dy, dx)
        frac = (0.5 - az / hfov) % 2.0
        u = np.clip(np.where(frac > 1.0, 2.0 - frac, frac), 0.0, 1.0)
        v = np.where(el >= 0,
                     horizon_v * (1.0 - np.clip(el / EL_TOP, 0, 1)),
                     horizon_v + (1.0 - horizon_v) * np.clip(-el / EL_BOT, 0, 1))
        px = im[np.clip((v * (H - 1)).astype(int), 0, H - 1),
                np.clip((u * (W - 1)).astype(int), 0, W - 1)]
        # ease past the photo's coverage into flat tones (no pinch at the poles)
        # fades start INSIDE the photo's coverage so its stretched edge rows
        # (streaks below the cloud sea, ray-pinch at the zenith) never show
        up = np.clip((el - EL_TOP * 0.70) / np.radians(22.0), 0, 1)[..., None]
        dn = np.clip((-el - EL_BOT * 0.55) / np.radians(14.0), 0, 1)[..., None]
        up, dn = up * up * (3 - 2 * up), dn * dn * (3 - 2 * dn)
        px = px * (1 - up) + top * up
        px = px * (1 - dn) + deep * dn
        if disc is not None:
            px = _composite_disc(px, dx, dy, dz, *disc)
        rgb = [tuple(p) for p in np.clip(px, 0, 255).astype(np.uint8).reshape(-1, 3)]
        with open(os.path.join(out_dir, f"{prefix}_{side}.tga"), "wb") as fh:
            fh.write(_tga32(size, size, rgb))
    print(f"wrote 6 faces -> {out_dir}/{prefix}_*.tga")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--horizon", type=float, default=0.80)
    ap.add_argument("--fov", type=float, default=180.0)
    ap.add_argument("--size", type=int, default=1024)
    a = ap.parse_args()
    build(a.src, a.horizon, a.fov, a.size)
