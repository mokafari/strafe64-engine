"""Nocturne: open marble filming plaza. Run this file to bake its distributable pk3."""
from pathlib import Path
from strafegen_marble import Marble
from strafegen_geom import make_skybox, make_box, validate_spawns
import strafegen_materials as M

# Planar mirror (Q3 portal surface with no camera = mirror). Needs a misc_portal_surface
# within 64u of the plane. The dark constant stage is blended over the mirror view:
# result = view * MIRROR_ALPHA + tint * (1 - MIRROR_ALPHA).
MIRROR_SHADER = 'textures/strafe64/mirror_pool'
MIRROR_ALPHA = 0.78
EXTRA_SHADER = (
    '\n// nocturne reflecting pool (planar mirror prototype)\n'
    + MIRROR_SHADER + '\n{\n\tqer_editorimage textures/strafe64/d_wall.tga\n'
    '\tsurfaceparm nolightmap\n\tportal\n\t{\n\t\tmap $whiteimage\n'
    '\t\tblendFunc GL_ONE_MINUS_SRC_ALPHA GL_SRC_ALPHA\n'
    '\t\tdepthWrite\n'
    '\t\trgbGen const ( 0.035 0.06 0.10 )\n'
    '\t\talphaGen const %g\n\t}\n}\n' % MIRROR_ALPHA)


class Nocturne(Marble):
    def build(self):
        # Lighting balance (see docs/COLOUR_SCIENCE.md): the plaza was baked ~1 stop too
        # hot for its unlit moon sky, so no exposure could serve both. Bake lighter, and
        # keep the warm pools as accents rather than floodlights.
        self.bake_scale = 1.1          # Marble default for night is 2.2
        P = 2400
        self.sections.append(('Nocturne open plaza', {'width': 4800, 'clear_center': 2400}))
        # Floor in four slabs around a shallow reflecting pool on the central axis.
        PX, PY, PD = 560, 1000, 12      # pool half-width (x), half-length (y), depth
        self.extra_shader = EXTRA_SHADER
        self.box((-P,-P,-96),(-PX,P,0),'marble_tiles')
        self.box((PX,-P,-96),(P,P,0),'marble_tiles')
        self.box((-PX,-P,-96),(PX,-PY,0),'marble_tiles')
        self.box((-PX,PY,-96),(PX,P,0),'marble_tiles')
        pool = make_box((-PX,-PY,-96),(PX,PY,-PD),tex=M.tex('marble_black'),
                        palette=(40,40,44),face_tex={'+z':MIRROR_SHADER})
        pool.no_clad = True
        self.solids.append(pool)
        self.place('misc_portal_surface',0,0,6)   # open air, <64u from the plane: its leaf drives the mirror's PVS
        # Flush black inlays define the broad promenade without interrupting movement.
        for x in (-1800,1800):
            self.box((x-64,-P,0),(x+64,P,1),'marble_black')
        for y in (-1800,1800):
            self.box((-P,y-64,0),(P,y+64,1),'marble_black')
        for y in (-2380,2380):
            self.box((-P,y-20,0),(P,y+20,64),'marble_black')
            self.neon((-P,y-22,48),(P,y-20,52))
        for x in (-2380,2380):
            self.box((x-20,-2360,0),(x+20,2360,64),'marble_black')
        # Two spacious terraces, accessible by 640-wide shallow stairs.
        for side in (-1,1):
            x0,x1 = (-2280,-1640) if side<0 else (1640,2280)
            self.box((x0,-1100,0),(x1,1100,192),'marble_white')
            for i in range(12):
                a=1160+i*40
                lo,hi=(a,a+40) if side>0 else (-a-40,-a)
                self.box((lo,-320,0),(hi,320,(i+1)*16),'marble_white')
            self.neon((x0,-1104,164),(x1,-1100,168))
            self.neon((x0,1100,164),(x1,1104,168))
        # Monumental portals frame long views, leaving a 1280-unit opening.
        for y,h in ((-1500,640),(1500,800)):
            for x in (-720,720):
                self.box((x-80,y-80,0),(x+80,y+80,h),'marble_black')
                self.box((x-84,y-84,0),(x+84,y+84,32),'marble_white')
                self.neon((x-5,y-83,48),(x+5,y-80,h-32))
                self.place('light',x,y-150,100,light=300,_color='0.55 0.67 1')
            self.box((-800,y-80,h),(800,y+80,h+96),'marble_white')
            self.neon((-640,y-40,h-4),(640,y+40,h))
        # Sparse perimeter colonnade and warm pools against the cool moonlight.
        for x in (-2100,2100):
            for y in (-2000,-800,800,2000):
                z=192 if abs(y)<1100 else 0
                self.box((x-64,y-64,z),(x+64,y+64,z+480),'marble_white')
                self.box((x-80,y-80,z+480),(x+80,y+80,z+512),'marble_black')
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
