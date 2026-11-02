"""Nocturne: open marble filming plaza. Run this file to bake its distributable pk3."""
import os
from pathlib import Path
from strafegen_marble import Marble
from strafegen_geom import make_skybox, make_box, validate_spawns
import strafegen_materials as M

# Planar mirror (Q3 portal surface with no camera = mirror). Needs a misc_portal_surface
# within 64u of the plane. The dark constant stage is blended over the mirror view:
# result = view * MIRROR_ALPHA + tint * (1 - MIRROR_ALPHA).
MIRROR_SHADER = 'textures/strafe64/mirror_pool'
MIRROR_ALPHA = 0.78      # (kept for reference; water uses alphaGen portal range below)
WATER_RANGE = 1500       # distance (u) at which the pool becomes a full mirror; closer = darker, deeper water
GLINT_TEX = 'textures/strafe64/m/nrx_water_glint.png'
WATER_NORMAL_TEX = 'textures/strafe64/m/nrx_water_n.png'


def _water_glint_png():
    """Tileable moon-glint texture: a sum of random-phase plane waves (integer wave vectors so it
    tiles) gives a natural, non-periodic ripple height; glints are where a facet's slope turns
    toward the moon half-vector. Two scrolling layers of it make the moving sparkle.
    (The pack's water height map is procedural and periodic: it showed as a dot grid.)"""
    import io
    import numpy as np
    from PIL import Image
    N = 512
    rng = np.random.RandomState(7)
    y, x = np.mgrid[0:N, 0:N].astype(np.float32) / N
    ht = np.zeros((N, N), np.float32)
    for _ in range(90):
        kx, ky = rng.randint(-40, 41, 2)
        kk = float(np.hypot(kx, ky))
        if kk < 3:
            continue
        ht += (kk ** -1.15) * np.sin(2 * np.pi * (kx * x + ky * y) + rng.uniform(0, 2 * np.pi))
    dx = (np.roll(ht, -1, 1) - np.roll(ht, 1, 1)) * 0.5 * N / (2 * np.pi)
    dy = (np.roll(ht, -1, 0) - np.roll(ht, 1, 0)) * 0.5 * N / (2 * np.pi)
    k = 0.9 / max(np.hypot(dx, dy).std(), 1e-6) * 0.35
    n = np.stack([-dx * k, -dy * k, np.ones_like(ht)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    h = np.array([0.0, 0.40, 0.92]); h /= np.linalg.norm(h)        # moon half-vector, mostly up
    g = np.clip((n * h).sum(-1), 0, 1)
    lo = np.percentile(g, 82)
    g = np.clip((g - lo) / max(g.max() - lo, 1e-3), 0, 1) ** 1.4
    g = (g * 255).astype(np.uint8)
    out = io.BytesIO(); Image.fromarray(g, 'L').save(out, 'PNG')
    return out.getvalue()


def _water_normal_png():
    """Tileable ripple slopes for the engine water pass (r_water): rg = slope * 0.5 + 0.5, b = 255.
    Same random-phase plane-wave sum as the glints, integer wave vectors so it tiles."""
    import io
    import numpy as np
    from PIL import Image
    N = 512
    rng = np.random.RandomState(11)
    y, x = np.mgrid[0:N, 0:N].astype(np.float32) / N
    ht = np.zeros((N, N), np.float32)
    for _ in range(90):
        kx, ky = rng.randint(-20, 21, 2)
        kk = float(np.hypot(kx, ky))
        if kk < 1.5:
            continue
        ht += (kk ** -2.3) * np.sin(2 * np.pi * (kx * x + ky * y) + rng.uniform(0, 2 * np.pi))
    dx = (np.roll(ht, -1, 1) - np.roll(ht, 1, 1)) * 0.5
    dy = (np.roll(ht, -1, 0) - np.roll(ht, 1, 0)) * 0.5
    sd = max(float(np.hypot(dx, dy).std()), 1e-9)
    rg = np.clip(np.stack([dx, dy], -1) / (3.0 * sd), -1, 1) * 0.5 + 0.5
    img = np.dstack([rg, np.ones((N, N, 1))]) * 255
    out = io.BytesIO(); Image.fromarray(img.astype(np.uint8), 'RGB').save(out, 'PNG')
    return out.getvalue()


EXTRA_SHADER = (
    '\n// nocturne reflecting pool: planar mirror + dark water body + moving moon glints\n'
    + MIRROR_SHADER + '\n{\n\tqer_editorimage textures/strafe64/d_wall.tga\n'
    '\tsurfaceparm nolightmap\n\tportal\n'
    # engine water pass (r_water 1): distort tint(rgb) moonDir(xyz) glintCol(rgb) specPower; the stages below are the r_water 0 fallback
    '\twater 0.075  0.010 0.022 0.040  0.0 0.98 0.19  4.0 4.4 5.4  650\n'
    '\twaterNormal ' + WATER_NORMAL_TEX + '\n'
    '\t{\n\t\tmap $whiteimage\n'
    '\t\tblendFunc GL_ONE_MINUS_SRC_ALPHA GL_SRC_ALPHA\n'
    '\t\tdepthWrite\n'
    '\t\trgbGen const ( 0.02 0.045 0.08 )\n'
    '\t\talphaGen portal @RANGE@\n\t}\n'
    # two counter-scrolling glint layers (additive, moon-tinted)
    '\t{\n\t\tmap ' + GLINT_TEX + '\n\t\tblendFunc GL_ONE GL_ONE\n'
    '\t\trgbGen const ( 0.26 0.31 0.40 )\n'
    '\t\ttcMod scale 0.173 0.173\n\t\ttcMod scroll 0.006 0.004\n\t}\n'
    '\t{\n\t\tmap ' + GLINT_TEX + '\n\t\tblendFunc GL_ONE GL_ONE\n'
    '\t\trgbGen const ( 0.18 0.23 0.32 )\n'
    '\t\ttcMod scale 0.291 0.291\n\t\ttcMod rotate 3\n\t\ttcMod scroll -0.004 0.007\n\t}\n}\n').replace('@RANGE@', str(WATER_RANGE))


# ---- Nocturne-Reservoir texture pack (neutral warm stone, see docs/COLOUR_SCIENCE.md) -----
# Names live under textures/strafe64/m/ so the .map writer gives them 64u-per-repeat UVs;
# tcMod scale = 64 / tile size in world units. Shaders are lightmapped (+ deluxe) with the
# pack's _nh normal maps. The pack pk3 stays on the LaCie drive (88 MB); the bake mounts it
# and it is deployed next to the map pk3 at runtime.
RES_PK3 = os.environ.get('NOCTURNE_RESERVOIR_PK3',
                         '/Volumes/LaCie/STRAFE64/Nocturne-Reservoir/nocturne_reservoir.pk3')
# shader suffix -> (pack file stem, tile size in units, normalScale)
RES = {
    'nrx_pavers':   ('causeway_pavers',   256, 0.6),   # white marble floor slabs
    'nrx_ashlar':   ('pyramid_ashlar',    256, 0.8),   # terraces, lintels
    'nrx_limestone':('honed_limestone',   192, 0.4),   # column shafts
    'nrx_obelisk':  ('obelisk_marble',    256, 0.3),   # portal pillars, caps
    'nrx_basalt':   ('basalt_foundation', 128, 0.5),   # flush inlays, perimeter walls
    'nrx_coping':   ('pool_coping',       128, 0.5),   # pool rim
}


def _res_shader(suffix, stem, tile, ns):
    sc = 64.0 / tile
    tcm = '\t\ttcMod scale %g %g\n' % (sc, sc)
    return (
        'textures/strafe64/m/%s\n{\n\tqer_editorimage textures/strafe64/d_wall.tga\n'
        '\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n'
        '\t{\n\t\tmap textures/reservoir/%s.jpg\n\t\tblendFunc filter\n%s\t}\n'
        '\t{\n\t\tstage normalMap\n\t\tmap textures/reservoir/%s_nh.png\n%s'
        '\t\tnormalScale %g %g\n\t}\n}\n' % (suffix, stem, tcm, stem, tcm, ns, ns))


# warm amber light strip: emissive face + baked light (lightRGB matches the pack's lamp_amber)
LAMP_SHADER = 'textures/strafe64/m/nrx_lamp'
LAMP_RGB = (1.0, 0.66, 0.30)
EXTRA_SHADER += (
    '\n// Nocturne-Reservoir materials\n'
    + ''.join(_res_shader(k, *v) for k, v in RES.items())
    + LAMP_SHADER + '\n{\n\tqer_editorimage textures/strafe64/d_wall.tga\n'
    '\tsurfaceparm nolightmap\n\tq3map_surfacelight 260\n'
    '\tq3map_lightRGB %g %g %g\n\t{\n\t\tmap $whiteimage\n'
    '\t\trgbGen const ( %g %g %g )\n\t}\n}\n' % (LAMP_RGB + LAMP_RGB))


class Nocturne(Marble):
    def lamp(self, mins, maxs):
        b = make_box(mins, maxs, tex=LAMP_SHADER, palette=(255, 190, 110))
        b.no_clad = True
        self.solids.append(b)

    def build(self):
        if not os.path.exists(RES_PK3):
            raise SystemExit('texture pack not found: %s (set NOCTURNE_RESERVOIR_PK3)' % RES_PK3)
        self.extra_pk3 = [RES_PK3]
        self.extra_files = {GLINT_TEX: _water_glint_png(), WATER_NORMAL_TEX: _water_normal_png()}
        # Lighting balance (see docs/COLOUR_SCIENCE.md): the plaza was baked ~1 stop too
        # hot for its unlit moon sky, so no exposure could serve both. Bake lighter, and
        # keep the warm pools as accents rather than floodlights.
        self.bake_scale = 1.1          # Marble default for night is 2.2
        P = 2400
        self.sections.append(('Nocturne open plaza', {'width': 4800, 'clear_center': 2400}))
        # Floor in four slabs around a shallow reflecting pool on the central axis.
        PX, PY, PD = 560, 1000, 12      # pool half-width (x), half-length (y), depth
        self.extra_shader = EXTRA_SHADER
        self.box((-P,-P,-96),(-PX,P,0),'nrx_pavers')
        self.box((PX,-P,-96),(P,P,0),'nrx_pavers')
        self.box((-PX,-P,-96),(PX,-PY,0),'nrx_pavers')
        self.box((-PX,PY,-96),(PX,P,0),'nrx_pavers')
        pool = make_box((-PX,-PY,-96),(PX,PY,-PD),tex=M.tex('marble_black'),
                        palette=(40,40,44),face_tex={'+z':MIRROR_SHADER})
        pool.no_clad = True
        self.solids.append(pool)
        # coping frame around the pool, flush with the floor
        for mn,mx in (((-PX-24,-PY-24,0),(-PX,PY+24,2)),((PX,-PY-24,0),(PX+24,PY+24,2)),
                      ((-PX,-PY-24,0),(PX,-PY,2)),((-PX,PY,0),(PX,PY+24,2))):
            self.box(mn,mx,'nrx_coping')
        # warm lamp strips set into the floor beside the pool
        for x in (-PX-60, PX+60):
            self.lamp((x-4,-PY,0),(x+4,PY,2))
        self.place('misc_portal_surface',0,0,6)   # open air, <64u from the plane: its leaf drives the mirror's PVS
        # Flush black inlays define the broad promenade without interrupting movement.
        for x in (-1800,1800):
            self.box((x-64,-P,0),(x+64,P,1),'nrx_basalt')
        for y in (-1800,1800):
            self.box((-P,y-64,0),(P,y+64,1),'nrx_basalt')
        for y in (-2380,2380):
            self.box((-P,y-20,0),(P,y+20,64),'nrx_basalt')
            self.neon((-P,y-22,48),(P,y-20,52))
        for x in (-2380,2380):
            self.box((x-20,-2360,0),(x+20,2360,64),'nrx_basalt')
        # Two spacious terraces, accessible by 640-wide shallow stairs.
        for side in (-1,1):
            x0,x1 = (-2280,-1640) if side<0 else (1640,2280)
            self.box((x0,-1100,0),(x1,1100,192),'nrx_ashlar')
            for i in range(12):
                a=1160+i*40
                lo,hi=(a,a+40) if side>0 else (-a-40,-a)
                self.box((lo,-320,0),(hi,320,(i+1)*16),'nrx_ashlar')
                # lit nosing on the stair front (the concept art's amber step lights)
                nx = lo if side>0 else hi-2
                self.lamp((nx,-300,(i+1)*16-3),(nx+2,300,(i+1)*16))
            self.neon((x0,-1104,164),(x1,-1100,168))
            self.neon((x0,1100,164),(x1,1104,168))
        # Monumental portals frame long views, leaving a 1280-unit opening.
        for y,h in ((-1500,640),(1500,800)):
            for x in (-720,720):
                self.box((x-80,y-80,0),(x+80,y+80,h),'nrx_obelisk')
                self.box((x-84,y-84,0),(x+84,y+84,32),'nrx_ashlar')
                self.neon((x-5,y-83,48),(x+5,y-80,h-32))
                self.place('light',x,y-150,100,light=300,_color='0.55 0.67 1')
            self.box((-800,y-80,h),(800,y+80,h+96),'nrx_ashlar')
            self.neon((-640,y-40,h-4),(640,y+40,h))
        # Sparse perimeter colonnade and warm pools against the cool moonlight.
        for x in (-2100,2100):
            for y in (-2000,-800,800,2000):
                z=192 if abs(y)<1100 else 0
                self.box((x-64,y-64,z),(x+64,y+64,z+480),'nrx_limestone')
                self.box((x-80,y-80,z+480),(x+80,y+80,z+512),'nrx_obelisk')
                self.neon((x-50,y-67,z+24),(x+50,y-64,z+28))
                self.place('light',x,y-160,z+160,light=280,_color='1 0.78 0.55')
        for i,(x,y) in enumerate(((-1000,-1000),(1000,-1000),(-1000,1000),(1000,1000),(0,-2100),(0,2100))):
            self.place('info_player_deathmatch',x,y,40,angle=90 if y<0 else 270)
        self.place('info_player_intermission',-1800,-2100,850,angles='15 45 0')
        for cls,x,y in (('weapon_rocketlauncher',-1900,0),('weapon_railgun',1900,0),('item_health_mega',0,1900),('item_armor_combat',0,-1900)):
            self.place(cls,x,y,224 if x else 24)
        hull=make_skybox(-2700,-2700,-300,2700,2700,2600)
        for b in hull:
            for f in b.faces:
                if f.tex.endswith('/sky'): f.tex='textures/strafe64/sky_ether_night'
        self.solids+=hull
        self.entities.insert(0,{'classname':'worldspawn','message':'STRAFE64 // NOCTURNE'})
        return self

if __name__=='__main__':
    import strafegen_config as cfg
    from strafegen_bake import build_baked_pk3
    import strafegen_gfx as gfx
    original_textures = gfx.gfx_textures
    gfx.gfx_textures = lambda: {k:v for k,v in original_textures().items() if any(t in k for t in ("marble", "ethnight", "d_floor", "d_wall"))}
    cfg.THEME='ethereal'
    course=Nocturne(64,night=True).build()
    validate_spawns(course)
    out=Path(__file__).resolve().parents[2]/'generated'/'nocturne'
    out.mkdir(parents=True,exist_ok=True)
    print(build_baked_pk3('strafe64_nocturne',course,str(out),bounce=3))
