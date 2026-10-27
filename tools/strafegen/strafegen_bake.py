"""strafegen_bake — q3map2 baked-lighting pipeline (Layer 3, Option A).

Bakes real lightmaps (sun shadows + ambient occlusion + radiosity bounce) into a
generated map via q3map2, while PRESERVING strafegen's vertex-color identity
(orange floors / grey walls). q3map2 compiles its own geometry from the .map and
ZEROES vertex colors, so after the bake we re-inject strafegen's per-face palette
back onto the baked vertices (matched per-surface by plane + normal).

Pipeline (build_baked_pk3):
  1. emit the .map + a standalone bake pk3 (LIGHTMAPPED shader + q3map_sun + textures)
  2. compile in an ISOLATED temp basepath holding ONLY that pk3, so q3map2 reads
     OUR shader (with q3map_sun) — not the dozen competing strafe64.shaders in baseoa
  3. q3map2 -bsp -vis -light -bounce N -dirty  -> baked BSP (lightmaps, black verts)
  4. re-inject the palette per-surface  -> identity restored; render = palette x detail x lightmap
  5. bspc AAS on the baked BSP, then pack the final standalone pk3

The world surf/wall shaders are rewritten to a lightmapped form
  { map $lightmap rgbGen identity }{ map detail blendFunc filter rgbGen exactVertex }
so q3map2 lightmaps them and the engine multiplies the baked light onto the
palette-tinted detail. Accent glow shaders stay additive/nolightmap (self-illum
beacons — re-injection keeps their bright palette); hull/chrome stay dynamic-lit.
"""
import os
import shutil
import struct
import subprocess
import tempfile
import zipfile

import strafegen_config as cfg
from strafegen_palettes import theme_remap

HERE = os.path.dirname(os.path.abspath(__file__))
# $Q3MAP2 first (any OS: point it at your own netradiant-custom build), then the
# macOS binary build-q3map2.sh leaves here, then q3map2 on PATH
Q3MAP2 = (os.environ.get("Q3MAP2")
          or (os.path.join(HERE, "q3map2") if os.path.isfile(os.path.join(HERE, "q3map2"))
              else shutil.which("q3map2") or os.path.join(HERE, "q3map2")))


def have_q3map2():
    """True if the q3map2 binary exists and is executable (gates --bake)."""
    return os.path.isfile(Q3MAP2) and os.access(Q3MAP2, os.X_OK)

# IBSP v46 lumps
L_SHADERS, L_DRAWVERTS, L_SURFACES = 1, 10, 13
SZ_SHADER, SZ_DRAWVERT, SZ_SURFACE = 72, 44, 104


# ---------------------------------------------------------------------------
# shader: make surf/wall lightmapped (only those two blocks; glow stays additive)
# ---------------------------------------------------------------------------
def lightmap_world_shaders(shader_script):
    out = shader_script
    for tex, img in (("surf", "d_floor"), ("wall", "d_wall")):
        head = "textures/strafe64/%s\n{\n" % tex
        h = out.find(head)
        if h < 0:
            continue
        e = out.find("\n}\n", h) + 3          # block close (brace at col 0)
        block = out[h:e]
        nb = block.replace("\tsurfaceparm nolightmap\n", "")
        nb = nb.replace(head, head + "\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n", 1)
        nb = nb.replace(
            "\t\tmap textures/strafe64/%s.tga\n\t\trgbGen exactVertex" % img,
            "\t\tmap textures/strafe64/%s.tga\n\t\tblendFunc filter\n\t\trgbGen exactVertex" % img)
        out = out[:h] + nb + out[e:]
    # theme materials (concrete): lightmap first, then the photo stage(s) as a
    # filter. Map-less stages (the vertex shader's faint wave overlay) are
    # dropped -- with no blendFunc they would REPLACE the lit result with white.
    for full in ("textures/strafe64/crete", "textures/strafe64/crete_eth") + _mats.ALL_TEX:
        head = full + "\n{\n"
        h = out.find(head)
        if h < 0:
            continue
        e = out.find("\n}\n", h) + 3
        body = out[h + len(head):e - 2]
        params, stages, depth, cur = [], [], 0, []
        for ln in body.splitlines(keepends=True):
            st = ln.strip()
            if depth == 0 and st == "{":
                depth, cur = 1, [ln]
            elif depth == 1 and st == "}":
                cur.append(ln)
                stages.append("".join(cur))
                depth = 0
            elif depth == 1:
                cur.append(ln)
            elif st != "surfaceparm nolightmap":
                params.append(ln)
        kept = []
        for stg in stages:
            if "map textures/" not in stg:
                continue
            lines = [l for l in stg.splitlines(keepends=True)
                     if not l.strip().startswith("blendFunc")]
            for i, l in enumerate(lines):
                if l.strip().startswith("map textures/"):
                    lines.insert(i + 1, "\t\tblendFunc filter\n")
                    break
            kept.append("".join(lines))
        nb = (head + "".join(params) + "\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n"
              + "".join(kept) + "}\n")
        out = out[:h] + nb + out[e:]
    return out


# Materials a baked map routes to lightmapped / sun-less variants. The variants
# live in the shared pak (strafegen_shaders.build_shader -> baked_variants) under
# distinct names, so the shared vertex-lit originals can never shadow them.
import strafegen_materials as _mats
LM_TEX = ("textures/strafe64/surf", "textures/strafe64/wall",
          "textures/strafe64/crete", "textures/strafe64/crete_eth") + _mats.ALL_TEX
BK_SKY = ("textures/strafe64/sky", "textures/strafe64/sky_ether",
          "textures/strafe64/sky_ether_night", "textures/strafe64/skyreal")


def baked_tex(tex):
    """Shader name a baked map uses for a (theme-remapped, glow-routed) tex."""
    if tex in LM_TEX:
        return tex + "_lm"
    if tex in BK_SKY:
        return tex + "_bk"
    return tex


def _block(script, name):
    head = name + "\n{\n"
    h = script.find(head)
    if h < 0:
        return None
    return script[h:script.find("\n}\n", h) + 3]


def baked_variants(shader_script):
    """Shader text for the baked-map variants of LM_TEX / BK_SKY.

    *_lm = the lightmapped form (lightmap_world_shaders) of each material;
    *_bk = each sky without its runtime q3gl2_sun (the sun + shadows are in the
    lightmap; a live sun on top is double light and ~3ms of shadow maps). The
    q3map_sun / q3map_skylight stay -- q3map2 bakes from them."""
    lit = lightmap_world_shaders(shader_script)
    out = ["\n// ---- baked-map variants (strafegen_bake.baked_variants) ----\n"]
    for t in LM_TEX:
        blk = _block(lit, t)
        if blk and "$lightmap" in blk:
            blk = blk.replace(t + "\n", t + "_lm\n", 1)
            if t in _mats.ALL_TEX:          # explicit relief, not rend2's auto _nh
                name = t[len(_mats.TEX_M):]
                blk = blk[:blk.rstrip().rfind("}")] + _mats.relief_stage(name) + "}\n"
            out.append(blk)
    for t in BK_SKY:
        blk = _block(shader_script, t)
        if blk:
            blk = "".join(l for l in blk.splitlines(keepends=True)
                          if not l.lstrip().startswith("q3gl2_sun"))
            out.append(blk.replace(t + "\n", t + "_bk\n", 1))
    return "".join(out)


# Flat ambient floor per theme (q3map2 worldspawn _ambient/_color) so enclosed
# layouts that the sky can't reach aren't black; the neon surfacelights and the
# bounce do the rest.
BAKE_AMBIENT = {
    "default":  ("6", "0.80 0.80 0.85"),
    "concrete": ("8", "0.85 0.88 0.95"),
    "ethereal": ("30", "0.60 0.65 0.78"),   # blue-grey shade, not black
}


# global q3map2 light multiplier per theme (domtest's hand bake used 1.7)
BAKE_SCALE = {"default": 1.7, "concrete": 1.7, "ethereal": 2.5}


# ---------------------------------------------------------------------------
# palette re-injection: match each baked surface back to a strafegen face
# ---------------------------------------------------------------------------
def _dom_axis(n):
    a = (abs(n[0]), abs(n[1]), abs(n[2]))
    return a.index(max(a))


def _pip(px, py, poly):
    inside = False
    n = len(poly)
    j = n - 1
    for i in range(n):
        xi, yi = poly[i]
        xj, yj = poly[j]
        if ((yi > py) != (yj > py)) and (px < (xj - xi) * (py - yi) / (yj - yi + 1e-12) + xi):
            inside = not inside
        j = i
    return inside


def _face_proj(course):
    """Precompute (normal, dist, keep-axes, 2D-poly, palette) for every face."""
    proj = []
    for b in course.solids:
        for f in b.faces:
            ax = _dom_axis(f.normal)
            o = [(1, 2), (0, 2), (0, 1)][ax]
            _t, pal = theme_remap(f.tex, f.palette, cfg.THEME)
            proj.append((f.normal, f.dist, o,
                         [(p[o[0]], p[o[1]]) for p in f.poly], pal))
    return proj


def _palette_for(centroid, surfnormal, proj):
    cands = []
    for n, dist, o, poly2d, pal in proj:
        if n[0] * surfnormal[0] + n[1] * surfnormal[1] + n[2] * surfnormal[2] < 0.99:
            continue
        pd = abs(n[0] * centroid[0] + n[1] * centroid[1] + n[2] * centroid[2] - dist)
        if pd > 2.0:
            continue
        if _pip(centroid[o[0]], centroid[o[1]], poly2d):
            return pal
        cands.append((pd, pal))
    if cands:
        cands.sort(key=lambda c: c[0])
        return cands[0][1]
    return None


def reinject_palette(bsp_path, course):
    """Overwrite each baked vertex's RGB with its source face's palette so the
    orange/grey identity survives q3map2 (which zeroed vertex colors). The
    lightmap then multiplies on top at render time. Returns (matched, total)."""
    d = bytearray(open(bsp_path, "rb").read())
    lumps = [struct.unpack("<ii", d[8 + i * 8:16 + i * 8]) for i in range(17)]
    sho = lumps[L_SHADERS][0]
    dvo = lumps[L_DRAWVERTS][0]
    suo, sul = lumps[L_SURFACES]
    proj = _face_proj(course)

    matched = 0
    nsurf = sul // SZ_SURFACE
    for si in range(nsurf):
        f = struct.unpack("<12i12f2i", d[suo + si * SZ_SURFACE:suo + (si + 1) * SZ_SURFACE])
        fv, nv = f[3], f[4]
        nrm = (f[12 + 9], f[12 + 10], f[12 + 11])   # lightmapVecs[2] = surface normal
        if nv == 0:
            continue
        cx = cy = cz = 0.0
        for k in range(fv, fv + nv):
            v = struct.unpack_from("<3f", d, dvo + k * SZ_DRAWVERT)
            cx += v[0]; cy += v[1]; cz += v[2]
        pal = _palette_for((cx / nv, cy / nv, cz / nv), nrm, proj)
        if pal is None:
            continue
        matched += 1
        r, g, b = pal
        for k in range(fv, fv + nv):
            off = dvo + k * SZ_DRAWVERT + 40       # RGBA bytes
            d[off] = r & 255
            d[off + 1] = g & 255
            d[off + 2] = b & 255
            # leave alpha as-is
    open(bsp_path, "wb").write(d)
    return matched, nsurf


# ---------------------------------------------------------------------------
# the bake: isolated q3map2 compile + light
# ---------------------------------------------------------------------------
def _run(args):
    r = subprocess.run([Q3MAP2] + args, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError("q3map2 %s failed:\n%s" % (args[0], (r.stdout or "")[-800:]))
    return r.stdout


def bake_bsp(name, course, map_path, bake_pk3_path, dest_bsp, bounce=3):
    scale = getattr(course, "bake_scale", None) or \
        BAKE_SCALE.get(cfg.THEME, BAKE_SCALE["default"])
    """Compile + light `map_path` in isolation, re-inject palette, write to
    dest_bsp. bake_pk3_path = a pk3 with the lightmapped shader + textures."""
    tmp = tempfile.mkdtemp(prefix="s64bake_")
    try:
        boa = os.path.join(tmp, "baseoa")
        maps = os.path.join(boa, "maps")
        os.makedirs(maps)
        shutil.copy(bake_pk3_path, os.path.join(boa, os.path.basename(bake_pk3_path)))
        bm = os.path.join(maps, name + ".map")
        shutil.copy(map_path, bm)
        common = ["-fs_basepath", tmp, "-fs_game", "baseoa"]
        _run(["-bsp"] + common + [bm])
        bb = os.path.join(maps, name + ".bsp")
        # NOTE: no -vis. It only builds runtime PVS (culling) and -light doesn't
        # need it; strafegen's direct BSPs never had vis either. (-vis also
        # crashes on some of these brush layouts.) The engine renders unvised.
        # -scale / -bouncescale as the hand-tuned domtest bake used: without
        # them sky light alone leaves enclosed layouts near-black
        light_args = ["-light", "-bounce", str(bounce),
                      "-bouncescale", "2",
                      "-scale", str(scale),
                      # 8u luxels + 2x supersampling: crisp sun-shadow edges
                      # (16u default stair-stepped them across the cladding)
                      "-samplesize", "8", "-super", "2",
                      # deluxe maps (per-luxel light direction) so the _nh
                      # normal maps respond to the BAKED sun, not just dlights
                      "-deluxe",
                      "-dirty", "-fast", "-patchshadows", "-samples", "2"]
        try:
            light = _run(light_args + common + [bb])
        except RuntimeError:
            # q3map2's threaded TraceGrid intermittently crashes on these
            # layouts (same input passes on a rerun); retry single-threaded
            light = _run(light_args + ["-threads", "1"] + common + [bb])
        matched, nsurf = reinject_palette(bb, course)
        shutil.copy(bb, dest_bsp)
        suns = "?"
        for ln in light.splitlines():
            if "sun/sky lights" in ln:
                suns = ln.strip().split()[0]
        return {"matched": matched, "surfaces": nsurf, "suns": suns}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------------------
# driver: generate -> bake -> re-inject -> AAS -> standalone pk3
# ---------------------------------------------------------------------------
def build_baked_pk3(name, course, out_dir, bounce=3):
    """Full Layer-3 bake for a built course. Emits a STANDALONE pk3
    (baked BSP + AAS + lightmapped shader + textures) at out_dir/<name>.pk3 and
    returns its path. Requires q3map2 (build-q3map2.sh)."""
    if not have_q3map2():
        raise RuntimeError("q3map2 not found at %s — run build-q3map2.sh" % Q3MAP2)
    # lazy import to avoid a cycle (strafegen is the facade that calls us)
    import strafegen as sg
    import strafegen_gfx as gfx
    import strafegen_shaders as shaders

    os.makedirs(out_dir, exist_ok=True)
    work = tempfile.mkdtemp(prefix="s64bake_")
    try:
        # 1. emit .map + the lightmapped bake shader + textures
        map_path = os.path.join(work, name + ".map")
        # a course can override the theme's bake exposure: open-air maps take far
        # more sky light than enclosed killboxes and blow out at their settings
        amb, col = getattr(course, "bake_ambient", None) or \
            BAKE_AMBIENT.get(cfg.THEME, BAKE_AMBIENT["default"])
        sg.write_map(course, map_path, baked=True,
                     extra_world={"_ambient": amb, "_color": col})
        # COMPILE shader = the shared script: it carries the *_lm lightmapped
        # materials and the *_bk skies (q3map_sun kept, runtime q3gl2_sun gone).
        shader = shaders.build_shader(True)
        # SHIP: the map pk3 carries ONLY the baked variants, under a unique file
        # name. A scripts/strafe64.shader here always lost to the shared pak's
        # and the baked surfaces rendered with the vertex-lit shaders (the
        # "white blowout"); the variants also live in the shared pak, so this
        # copy just makes the pk3 work standalone.
        ship_shader = baked_variants(gfx.augment(shaders.SHADER_SCRIPT))
        tex = dict(sg.build_detail_textures())
        tex.update(gfx.gfx_textures())
        bake_pk3 = os.path.join(work, "zzz_bake_assets.pk3")
        with zipfile.ZipFile(bake_pk3, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("scripts/strafe64.shader", shader)
            for arc, data in tex.items():
                z.writestr(arc, data)

        # 2. bake + re-inject palette
        baked = os.path.join(work, name + ".bsp")
        stats = bake_bsp(name, course, map_path, bake_pk3, baked, bounce=bounce)

        # 3. AAS (bots) on the baked bsp
        packmaps = os.path.join(work, "pack", "maps")
        os.makedirs(packmaps, exist_ok=True)
        bsp_pack = os.path.join(packmaps, name + ".bsp")
        shutil.copy(baked, bsp_pack)
        aas = sg.compile_aas(bsp_pack)

        # 4. standalone pk3
        bots = 'bots "sarge major grunt"\n' if aas else ""
        arena = ('{\nmap "%s"\nlongname "STRAFE64 %s (baked)"\n'
                 'type "ffa tourney"\n%s}\n' % (name, name, bots))
        pk3 = os.path.join(out_dir, name + ".pk3")
        with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(bsp_pack, "maps/%s.bsp" % name)
            if aas:
                z.write(aas, "maps/%s.aas" % name)
            z.writestr("scripts/%s.arena" % name, arena)
            z.writestr("scripts/%s_baked.shader" % name, ship_shader)
            for arc, data in tex.items():
                z.writestr(arc, data)
        stats["pk3"] = pk3
        stats["aas"] = bool(aas)
        return stats
    finally:
        shutil.rmtree(work, ignore_errors=True)
