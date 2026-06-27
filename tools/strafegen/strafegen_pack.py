"""strafegen_pack — pk3 packaging, bspc/aas, the shared-asset pak and backups.

The shader + procedural textures are shipped ONCE in zzz_strafe64_shader.pk3
(write_shared_assets) instead of being copied into every map pk3 — which both
removes the ~1.8 MB-per-map duplication and fixes the FS name-dedupe collision
(a random map's bundled copy silently overriding everyone's). Map paks are LEAN
by default (bsp/aas/arena only); --standalone restores the portable full bundle.
"""
import os
import zipfile
import sys

import strafegen_gfx as gfx
from strafegen_shaders import build_shader
from strafegen_textures import build_detail_textures

# repo root = three up from tools/strafegen/strafegen_pack.py (matches the
# hand-started backups/maps-<date>/ convention at the repo root)
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The canonical identity-override pak. Reuses the established name (the global
# identity override that "bundles ALL textures") so strafegen becomes its single
# reproducible source and deploying OVERWRITES any stale hand-built copy — rather
# than introducing a second zzz_* pak that would itself collide on strafe64.shader.
SHARED_ASSETS_PK3 = "zzz_strafe64_shader.pk3"


def backup_path(path, kind="maps"):
    """If *path* exists, move it into backups/<kind>-<YYYYMMDD>/ before it would be
    overwritten. De-collides same-day repeats with a numeric suffix. Returns the
    backup path, or None if there was nothing to back up."""
    if not os.path.exists(path):
        return None
    import datetime
    import shutil
    stamp = datetime.datetime.now().strftime("%Y%m%d")
    dest_dir = os.path.join(_REPO_ROOT, "backups", f"{kind}-{stamp}")
    os.makedirs(dest_dir, exist_ok=True)
    base = os.path.basename(path)
    dest = os.path.join(dest_dir, base)
    n = 1
    while os.path.exists(dest):
        root, ext = os.path.splitext(base)
        dest = os.path.join(dest_dir, f"{root}.{n}{ext}")
        n += 1
    shutil.move(path, dest)
    return dest


def write_shared_assets(out_dir, gfx_on=True):
    """Write the single canonical identity-asset pak: scripts/strafe64.shader +
    every procedural texture, shared by ALL generated maps. The zzz_ prefix sorts
    it last so it deterministically wins the Quake3 FS name-dedupe. Idempotent."""
    pk3 = os.path.join(out_dir, SHARED_ASSETS_PK3)
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("scripts/strafe64.shader", build_shader(gfx_on))
        for arc, data in build_detail_textures().items():
            z.writestr(arc, data)
        if gfx_on:
            for arc, data in gfx.gfx_textures().items():
                z.writestr(arc, data)
    return pk3


def write_pk3(bsp_path, name, out_dir, aas_path=None, gfx_on=True,
              standalone=False):
    """Pack a map. LEAN by default: only the map's own bsp/aas/arena — the shared
    shader + textures live in zzz_strafe64_shader.pk3 (write_shared_assets), so N
    maps no longer ship N identical copies. standalone=True bundles the shader +
    textures INTO this pak for a portable single-file map (pre-split behaviour)."""
    bots = 'bots "sarge major grunt"\n' if aas_path else ""
    arena = (f'{{\nmap "{name}"\nlongname "STRAFE64 {name}"\n'
             f'type "ffa tourney"\n{bots}}}\n')
    pk3 = os.path.join(out_dir, f"{name}.pk3")
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(bsp_path, f"maps/{name}.bsp")
        if aas_path:
            z.write(aas_path, f"maps/{name}.aas")
        z.writestr(f"scripts/{name}.arena", arena)
        if standalone:
            z.writestr("scripts/strafe64.shader", build_shader(gfx_on))
            for arc, data in build_detail_textures().items():
                z.writestr(arc, data)
            if gfx_on:
                for arc, data in gfx.gfx_textures().items():
                    z.writestr(arc, data)
    if gfx_on:
        # sibling cfg for booting the map directly with the full GL2 look
        # (parallax + ssao default off; the rest already default on).
        with open(os.path.join(out_dir, f"{name}_gfx.cfg"), "w") as f:
            f.write(gfx.render_cfg())
    return pk3


def find_bspc():
    cand = os.environ.get("BSPC")
    if cand and os.path.isfile(cand):
        return cand
    here = os.path.dirname(os.path.abspath(__file__))
    local = os.path.join(here, "bspc")
    if os.path.isfile(local):
        return local
    import shutil
    return shutil.which("bspc")


def compile_aas(bsp_path):
    """Run bspc -bsp2aas so bots can navigate. Returns .aas path or None."""
    bspc = find_bspc()
    if not bspc:
        return None
    import subprocess
    out_dir = os.path.dirname(os.path.abspath(bsp_path)) or "."
    # NOTE: a "realistic" -cfg (aas_strafe64.cfg, phys tuned to the moveset)
    # was tried and REGRESSED nav — it made bspc conservative and bots stalled
    # (FLOW 152->0). bspc's default leaves physics at FLT_MAX, which treats the
    # bot as able to jump anywhere; combined with the enhanced moveset that
    # over-optimism navigates better than a realistic model. So: no -cfg.
    # (dojo_runs.jsonl iter2/iter3 — hypothesis rejected by no-regression.)
    try:
        r = subprocess.run(
            [bspc, "-forcesidesvisible", "-bsp2aas",
             os.path.abspath(bsp_path), "-output", out_dir],
            capture_output=True, text=True, cwd=out_dir)
    except OSError as e:
        # bspc present but not runnable (wrong arch / corrupt / not executable).
        # Degrade to "no bots" exactly like a nonzero exit — a broken compiler
        # must not abort the whole map build.
        sys.stderr.write(f"warning: bspc not runnable ({e}), no bot support\n")
        return None
    aas = os.path.splitext(bsp_path)[0] + ".aas"
    if r.returncode != 0 or not os.path.isfile(aas):
        sys.stderr.write(f"warning: bspc failed, no bot support\n"
                         f"{(r.stdout or '')[-400:]}\n")
        return None
    log = os.path.join(out_dir, "bspc.log")
    if os.path.isfile(log):
        os.remove(log)
    return aas


