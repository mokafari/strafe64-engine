#!/usr/bin/env python3
"""
obj2md3.py - minimal Wavefront OBJ -> Quake III MD3 converter.

Single-frame, single-surface static meshes only (weapons, pickups, props).
No Blender / addon dependency: writes the MD3 binary directly per the format
in engine/code/qcommon/qfiles.h (IDENT "IDP3", version 15).

Usage:
    obj2md3.py in.obj out.md3 --shader models/weapons2/sword/sword
               [--scale S] [--uniform-len L] [--rot-x DEG --rot-y DEG --rot-z DEG]
               [--center]
"""
import argparse, math, struct, sys

MD3_IDENT   = (ord('3') << 24) | (ord('P') << 16) | (ord('D') << 8) | ord('I')
MD3_VERSION = 15
MD3_XYZ_SCALE = 1.0 / 64.0
MAX_QPATH = 64


def parse_obj(path):
    verts, uvs, norms, faces = [], [], [], []
    with open(path, "r", errors="replace") as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "v":
                verts.append((float(t[1]), float(t[2]), float(t[3])))
            elif t[0] == "vt":
                # MD3 (and OpenGL) want V flipped relative to OBJ
                u = float(t[1]); v = float(t[2])
                uvs.append((u, 1.0 - v))
            elif t[0] == "vn":
                norms.append((float(t[1]), float(t[2]), float(t[3])))
            elif t[0] == "f":
                poly = []
                for c in t[1:]:
                    a = (c.split("/") + ["", ""])[:3]
                    vi = int(a[0]) - 1
                    ti = int(a[1]) - 1 if a[1] else -1
                    ni = int(a[2]) - 1 if a[2] else -1
                    poly.append((vi, ti, ni))
                # fan-triangulate any n-gon
                for i in range(1, len(poly) - 1):
                    faces.append((poly[0], poly[i], poly[i + 1]))
    return verts, uvs, norms, faces


def rotate(p, rx, ry, rz):
    x, y, z = p
    for axis, ang in (("x", rx), ("y", ry), ("z", rz)):
        if ang == 0.0:
            continue
        c, s = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        if axis == "x":
            y, z = y * c - z * s, y * s + z * c
        elif axis == "y":
            x, z = x * c + z * s, -x * s + z * c
        else:
            x, y = x * c - y * s, x * s + y * c
    return (x, y, z)


def encode_normal(n):
    x, y, z = n
    if x == 0 and y == 0:
        return 0 if z > 0 else (128 << 8)
    lng = int(math.atan2(y, x) * 255.0 / (2 * math.pi)) & 0xFF
    lat = int(math.acos(max(-1.0, min(1.0, z))) * 255.0 / (2 * math.pi)) & 0xFF
    return (lat << 8) | lng


def cstr(s, n):
    b = s.encode("ascii", "replace")[: n - 1]
    return b + b"\x00" * (n - len(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("obj"); ap.add_argument("md3")
    ap.add_argument("--shader", default="")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--uniform-len", type=float, default=0.0,
                    help="scale so longest bbox axis == this many units")
    ap.add_argument("--rot-x", type=float, default=0.0)
    ap.add_argument("--rot-y", type=float, default=0.0)
    ap.add_argument("--rot-z", type=float, default=0.0)
    ap.add_argument("--center", action="store_true")
    ap.add_argument("--recenter-grip", action="store_true",
                    help="put the -X extreme (grip/pommel) at the origin and "
                         "center Y/Z on the axis — for held weapon view models")
    ap.add_argument("--tx", type=float, default=0.0, help="post-scale X nudge (units)")
    ap.add_argument("--ty", type=float, default=0.0, help="post-scale Y nudge (units)")
    ap.add_argument("--tz", type=float, default=0.0, help="post-scale Z nudge (units)")
    a = ap.parse_args()

    verts, uvs, norms, faces = parse_obj(a.obj)
    if not faces:
        sys.exit("no faces parsed")

    # transform positions: rotate, then scale, then optional recenter
    tv = [rotate(p, a.rot_x, a.rot_y, a.rot_z) for p in verts]
    mn = [min(p[i] for p in tv) for i in range(3)]
    mx = [max(p[i] for p in tv) for i in range(3)]
    scale = a.scale
    if a.uniform_len > 0:
        longest = max(mx[i] - mn[i] for i in range(3)) or 1.0
        scale = a.uniform_len / longest
    tv = [(p[0] * scale, p[1] * scale, p[2] * scale) for p in tv]
    if a.center:
        mn = [min(p[i] for p in tv) for i in range(3)]
        mx = [max(p[i] for p in tv) for i in range(3)]
        ctr = [(mn[i] + mx[i]) / 2 for i in range(3)]
        tv = [(p[0] - ctr[0], p[1] - ctr[1], p[2] - ctr[2]) for p in tv]
    if a.recenter_grip:
        mn = [min(p[i] for p in tv) for i in range(3)]
        mx = [max(p[i] for p in tv) for i in range(3)]
        # grip (-X extreme) -> origin; cross-section centered on the X axis
        sx = -mn[0]
        sy = -(mn[1] + mx[1]) / 2
        sz = -(mn[2] + mx[2]) / 2
        tv = [(p[0] + sx, p[1] + sy, p[2] + sz) for p in tv]
    # final manual nudge (post-scale units)
    tv = [(p[0] + a.tx, p[1] + a.ty, p[2] + a.tz) for p in tv]
    tn = [rotate(n, a.rot_x, a.rot_y, a.rot_z) for n in norms]

    # build unique (v,vt,vn) -> md3 vertex index
    uniq, remap, tris = {}, [], []
    for tri in faces:
        idx = []
        for (vi, ti, ni) in tri:
            key = (vi, ti, ni)
            if key not in uniq:
                uniq[key] = len(remap)
                remap.append((vi, ti, ni))
            idx.append(uniq[key])
        tris.append(idx)

    nverts = len(remap)
    ntris = len(tris)
    if nverts > 4096:
        sys.exit("surface exceeds MD3_MAX_VERTS (4096)")

    # frame bounds / radius from final positions
    mn = [min(p[i] for p in tv) for i in range(3)]
    mx = [max(p[i] for p in tv) for i in range(3)]
    ctr = [(mn[i] + mx[i]) / 2 for i in range(3)]
    radius = max(math.dist(p, ctr) for p in tv)

    # ---- assemble surface chunks ----
    surf_name = "sword"
    shaders = struct.pack("<%dsi" % MAX_QPATH, cstr(a.shader, MAX_QPATH), 0)
    tri_data = b"".join(struct.pack("<3i", *t) for t in tris)
    st_data = b"".join(struct.pack("<2f", *uvs[ti]) if ti >= 0 else struct.pack("<2f", 0, 0)
                        for (_, ti, _) in remap)
    xyz_data = b""
    for (vi, _, ni) in remap:
        x, y, z = tv[vi]
        sx = max(-32768, min(32767, int(round(x / MD3_XYZ_SCALE))))
        sy = max(-32768, min(32767, int(round(y / MD3_XYZ_SCALE))))
        sz = max(-32768, min(32767, int(round(z / MD3_XYZ_SCALE))))
        nrm = encode_normal(tn[ni]) if ni >= 0 else 0
        xyz_data += struct.pack("<3hH", sx, sy, sz, nrm)

    SURF_HDR = 4 + MAX_QPATH + 4 * 10  # ident + name + 10 ints
    ofsTriangles = SURF_HDR
    ofsShaders = ofsTriangles + len(tri_data)
    ofsSt = ofsShaders + len(shaders)
    ofsXyz = ofsSt + len(st_data)
    ofsEnd = ofsXyz + len(xyz_data)

    surf_hdr = struct.pack(
        "<i%dsiiiiiiiiii" % MAX_QPATH,
        MD3_IDENT, cstr(surf_name, MAX_QPATH), 0,
        1,            # numFrames
        1,            # numShaders
        nverts,
        ntris, ofsTriangles,
        ofsShaders, ofsSt, ofsXyz,
        ofsEnd)
    surface = surf_hdr + tri_data + shaders + st_data + xyz_data

    # ---- frame ----
    frame = struct.pack("<10f16s",
                        mn[0], mn[1], mn[2], mx[0], mx[1], mx[2],
                        ctr[0], ctr[1], ctr[2], radius, cstr("frame0", 16))

    # ---- header ----
    HDR = 4 + 4 + MAX_QPATH + 4 * 9  # ident,version,name,9 ints
    ofsFrames = HDR
    ofsTags = ofsFrames + len(frame)
    ofsSurfaces = ofsTags  # 0 tags
    file_end = ofsSurfaces + len(surface)

    header = struct.pack(
        "<ii%dsiiiiiiiii" % MAX_QPATH,
        MD3_IDENT, MD3_VERSION, cstr("sword", MAX_QPATH),
        0,            # flags
        1,            # numFrames
        0,            # numTags
        1,            # numSurfaces
        0,            # numSkins
        ofsFrames, ofsTags, ofsSurfaces, file_end)

    with open(a.md3, "wb") as f:
        f.write(header + frame + surface)

    print("wrote %s: %d verts, %d tris, scale=%.4f, radius=%.2f, %d bytes"
          % (a.md3, nverts, ntris, scale, radius, file_end))


if __name__ == "__main__":
    main()
