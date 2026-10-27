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
from PIL import Image

from skybox_from_photo import FACES
from strafegen_tga import _tga32

EL_TOP = np.radians(55.0)
EL_BOT = np.radians(22.0)


def build(src, horizon_v=0.80, fov_deg=180.0, size=1024, prefix="ethsky", out_dir=None,
          sun_u=None):
    """sun_u: the sun/moon's horizontal position in the photo (0..1). The mirror
    wrap shows every column twice except the photo's two EDGES, so the photo is
    cropped to start at the sun: it then appears exactly once, at azimuth +90deg
    (aim the baked/runtime sun there)."""
    pil = Image.open(src).convert("RGB")
    if sun_u is not None:
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
