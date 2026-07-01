#!/usr/bin/env python3
"""skybox_from_photo — fake a seamless Q3 skybox cube from one flat sky photo.

The concrete (lun3dm5) theme wants a photoreal sky. We have a single wide-angle
photo (sun upper-left, mountains on the horizon), NOT a 360 panorama, so we
project it onto the full sphere by azimuth/elevation:

  * elevation 0 (horizon) -> the photo's mountain line; elevation +90 (zenith) ->
    the photo's top row; below horizon -> the mountain/haze strip darkened to
    ground.
  * azimuth maps across the photo width over H_FOV degrees, centered on the sun
    side as the "front"; beyond +/-H_FOV/2 the azimuth fraction is TRIANGLE-
    MIRRORED so the cloudscape repeats seamlessly around the back (no hard seam).

Faces are emitted with the exact basis ioquake3's tr_sky.c expects (shared with
strafegen_textures._build_synthsky) so the sky doesn't shear as you turn. The
sun's world direction (detected as the photo's brightest spot) is printed so the
vertex-light dome (strafegen_bsp.build_light_model) can be aimed to match.

    python3 skybox_from_photo.py [--fov 150] [--horizon 0.80] [--size 512]
"""
import argparse
import math
import os

from PIL import Image

from strafegen_tga import _tga32

# tr_sky.c face basis: dir = F + sx*R + sy*U (sx left->right, sy bottom->top).
FACES = {
    "rt": ((1, 0, 0),  (0, -1, 0), (0, 0, 1)),
    "lf": ((-1, 0, 0), (0, 1, 0),  (0, 0, 1)),
    "bk": ((0, 1, 0),  (1, 0, 0),  (0, 0, 1)),
    "ft": ((0, -1, 0), (-1, 0, 0), (0, 0, 1)),
    "up": ((0, 0, 1),  (0, -1, 0), (-1, 0, 0)),
    "dn": ((0, 0, -1), (0, -1, 0), (1, 0, 0)),
}

# below this elevation (rad) the photo fades to clean haze; full photo above HZ_HI
HZ_LO, HZ_HI = 0.02, 0.40
HAZE = (172, 186, 205)          # pale day haze, ~ the fog_day colour


def _find_sun(im):
    """Brightest-cluster (u, v) in the photo's upper half -> the sun."""
    small = im.convert("RGB").resize((96, 54))
    px = small.load()
    best, bx, by = -1, 0, 0
    for y in range(54 // 2):            # upper half only
        for x in range(96):
            r, g, b = px[x, y]
            lum = r + g + b
            if lum > best:
                best, bx, by = lum, x, y
    return (bx + 0.5) / 96.0, (by + 0.5) / 54.0


def build(src, fov_deg=150.0, horizon_v=0.80, size=512, out_dir=None):
    im = Image.open(src).convert("RGB")
    W, H = im.size
    px = im.load()
    h_fov = math.radians(fov_deg)
    us, vs = _find_sun(im)

    def uv_from_dir(dx, dy, dz):
        el = math.asin(max(-1.0, min(1.0, dz)))      # -pi/2..pi/2
        az = math.atan2(dy, dx)                       # 0=front(+x), + = left(+y)
        # azimuth -> horizontal fraction, triangle-mirrored to wrap the back
        frac = 0.5 - az / h_fov                       # az>0 (left) -> u<0.5
        frac = frac % 2.0
        if frac > 1.0:
            frac = 2.0 - frac                         # mirror into [0,1]
        u = min(1.0, max(0.0, frac))
        # elevation -> vertical: horizon at horizon_v, zenith at top (v=0)
        if el >= 0.0:
            v = horizon_v * (1.0 - el / (math.pi / 2.0))
        else:
            t = min(1.0, -el / (math.pi / 2.0))
            v = horizon_v + (1.0 - horizon_v) * t     # into the mountain strip
        return u, v

    def sample(u, v):
        x = min(W - 1, max(0, int(u * (W - 1))))
        y = min(H - 1, max(0, int(v * (H - 1))))
        return px[x, y]

    out_dir = out_dir or os.path.join(os.path.dirname(__file__), "skytex")
    os.makedirs(out_dir, exist_ok=True)
    written = {}
    for side, (F, R, U) in FACES.items():
        buf = []
        for j in range(size):
            sy = 1.0 - 2.0 * j / (size - 1)
            for i in range(size):
                sx = 2.0 * i / (size - 1) - 1.0
                vx = F[0] + sx * R[0] + sy * U[0]
                vy = F[1] + sx * R[1] + sy * U[1]
                vz = F[2] + sx * R[2] + sy * U[2]
                inv = 1.0 / math.sqrt(vx * vx + vy * vy + vz * vz)
                dx, dy, dz = vx * inv, vy * inv, vz * inv
                u, v = uv_from_dir(dx, dy, dz)
                r, g, b = sample(u, v)
                # The photo can only fill the upper sky; folding it around the
                # full sphere mirrors clouds into a busy reflected band at the
                # horizon. Since the scene floats ABOVE the clouds, fade the photo
                # to clean pale haze as elevation drops to the horizon and below —
                # clouds up high, smooth haze at/under the horizon, no reflection.
                el = math.asin(max(-1.0, min(1.0, dz)))
                f = (el - HZ_LO) / (HZ_HI - HZ_LO)    # 0 at/below horizon..1 high
                f = max(0.0, min(1.0, f))
                f = f * f * (3.0 - 2.0 * f)           # smoothstep
                r = int(HAZE[0] + (r - HAZE[0]) * f)
                g = int(HAZE[1] + (g - HAZE[1]) * f)
                b = int(HAZE[2] + (b - HAZE[2]) * f)
                buf.append((r, g, b))
        path = os.path.join(out_dir, f"realsky_{side}.tga")
        with open(path, "wb") as fh:
            fh.write(_tga32(size, size, buf))
        written[side] = path

    # sun world direction from its photo (u, v) -> aim the light dome here
    az_s = (0.5 - us) * h_fov
    el_s = max(0.0, (1.0 - vs / horizon_v)) * (math.pi / 2.0)
    sun = (math.cos(el_s) * math.cos(az_s),
           math.cos(el_s) * math.sin(az_s),
           math.sin(el_s))
    print(f"photo sun at u={us:.3f} v={vs:.3f}  ->  az={math.degrees(az_s):.1f} "
          f"el={math.degrees(el_s):.1f}")
    print(f"_SUN_DIR (concrete day dome) = ({sun[0]:.4f}, {sun[1]:.4f}, {sun[2]:.4f})")
    print(f"wrote {len(written)} faces -> {out_dir}/realsky_*.tga")
    return sun


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(
        os.path.dirname(__file__), "..", "..", "assets",
        "ultra_realistic_skybox_sunny_mountain.png"))
    ap.add_argument("--fov", type=float, default=150.0)
    ap.add_argument("--horizon", type=float, default=0.80)
    ap.add_argument("--size", type=int, default=512)
    a = ap.parse_args()
    build(a.src, a.fov, a.horizon, a.size)
