#!/usr/bin/env python3
"""STRAFE 64 — shader-library SHOWCASE level.

A hand-designed synthwave atrium that puts the shaderlib assets to work in real
level design (not just flat gallery panels): you spawn at the south end and run
north toward a giant glowing SUN vista (the landmark that pulls you forward),
weaving between PLASMA energy pillars (cover + rhythm), leaping a LAVA gap
(movement beat), under an AURORA sky, past a spinning VORTEX portal on one wall
and a DIGITAL-RAIN data-wall on the other, with a ramp up to a MEZZANINE ledge
for a high line. Everything is audio-reactive — play a track and the room pumps.

    python3 showcase.py            # build + deploy (also ensures gallery assets)
    # then in-engine:  /map shaderlib_showcase

Reuses the shaderlib procedural textures + tiling shaders (shaderlib/room|plasma|
rain|aurora) and adds showcase-only shaders for the single-image features that
need a per-surface fit (sun vista, vortex portal) plus a tiling lava.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STRAFEGEN = os.path.dirname(HERE)
sys.path.insert(0, STRAFEGEN)
from strafegen import (make_box, make_prism, BspWriter, TEX_TRIGGER,  # noqa: E402
                       CONTENTS_TRIGGER)
import gallery  # noqa: E402

BASEOA = gallery.BASEOA
MAP_NAME = "shaderlib_showcase"

# ---- level dimensions (x = width, y = south->north length, z = up) ----
WX = 640.0                 # half width
LY1 = 2048.0               # north wall (the vista)
ZC = 704.0                 # ceiling
T = 16.0                   # shell thickness
PITZ = -160.0              # lava channel bottom
PY0, PY1 = 950.0, 1206.0   # lava gap (256u jump)

# feature rects (used for both the geometry and the per-surface fit transforms)
SUN = (-360.0, 360.0, 170.0, 610.0)    # north wall: x0,x1,z0,z1
VTX = (300.0, 640.0, 180.0, 520.0)     # east wall: y0,y1,z0,z1


def _fit(u0, u1, v0, v1):
    """tcMod transform mapping a face's two in-plane world axes (over [u0,u1] and
    [v0,v1], in /64 texel units) to the texture's 0..1 — single-image fit."""
    return ("tcMod transform %g 0 0 %g %g %g"
            % (64.0 / (u1 - u0), 64.0 / (v1 - v0),
               -u0 / (u1 - u0), -v0 / (v1 - v0)))


def showcase_shaders():
    sunfit = _fit(*SUN)
    vtxfit = _fit(*VTX)
    return f"""
// ---- STRAFE 64 shader-library SHOWCASE shaders (generated) ----
showcase/sun_vista
{{
\tnopicmip
\t{{ map textures/shaderlib/bezel.tga rgbGen identity }}
\t{{
\t\tmap textures/shaderlib/sunglow.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t{sunfit}
\t\trgbGen wave bass 0.28 0.9 0 0
\t}}
\t{{
\t\tmap textures/shaderlib/sun.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t{sunfit}
\t\trgbGen wave bass 0.80 0.4 0 0
\t}}
}}

showcase/vortex_portal
{{
\tnopicmip
\t{{ map textures/shaderlib/bezel.tga rgbGen identity }}
\t{{
\t\tclampMap textures/shaderlib/vortex.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t{vtxfit}
\t\ttcMod rotate 16
\t\trgbGen wave bass 0.6 0.5 0 0
\t}}
\t{{
\t\tclampMap textures/shaderlib/vortex.tga
\t\tblendFunc GL_ONE GL_ONE
\t\t{vtxfit}
\t\ttcMod rotate -9
\t\trgbGen wave sin 0.3 0.16 0 0.11
\t}}
}}

showcase/lava
{{
\tnopicmip
\t{{ map textures/shaderlib/bezel.tga rgbGen identity }}
\t{{
\t\tmap textures/shaderlib/fire.tga
\t\tblendFunc GL_ONE GL_ONE
\t\ttcMod scroll 0.010 0.05
\t\ttcMod turb 0 0.05 0 0.3
\t\trgbGen wave bass 0.85 0.4 0 0
\t}}
\t{{
\t\tmap textures/shaderlib/fire.tga
\t\tblendFunc GL_ONE GL_ONE
\t\ttcMod scale 1.6 1.6
\t\ttcMod scroll -0.013 0.035
\t\ttcMod turb 0.4 0.05 0 0.4
\t\trgbGen wave sin 0.5 0.18 0 0.15
\t}}
}}
"""


# shader names (shaderlib/* are deployed by gallery.py and tile freely)
ROOM = "shaderlib/room"
PLASMA = "shaderlib/plasma"
RAIN = "shaderlib/rain"
AURORA = "shaderlib/aurora"
SUN_SH = "showcase/sun_vista"
VTX_SH = "showcase/vortex_portal"
LAVA = "showcase/lava"


class Showcase:
    def __init__(self):
        self.solids = []
        self.triggers = []
        self.movers = []
        self.entities = []
        self.sections = []

    def box(self, mins, maxs, faces):
        """faces: {key: shader}; only those faces draw."""
        self.solids.append(make_box(mins, maxs, tex=ROOM, draw=set(faces),
                                    face_tex=faces))

    def build(self):
        # --- shell: floor split around the lava gap, walls, aurora ceiling ---
        self.box((-WX, -T, PITZ - T), (WX, 0.0, 0.0), {"+y": ROOM})          # S wall base/spawn back
        self.box((-WX, 0.0, PITZ), (WX, PY0, 0.0), {"+z": ROOM, "+y": LAVA})  # south floor (its N face = lava wall)
        self.box((-WX, PY1, PITZ), (WX, LY1, 0.0), {"+z": ROOM, "-y": LAVA})  # north floor (its S face = lava wall)
        self.box((-WX, PY0, PITZ), (WX, PY1, PITZ + 16), {"+z": LAVA})        # lava channel bottom
        self.box((-WX - T, -T, PITZ), (-WX, LY1, ZC + T), {"+x": ROOM})       # west wall
        self.box((WX, -T, PITZ), (WX + T, LY1, ZC + T), {"-x": ROOM})         # east wall
        self.box((-WX, -T, PITZ), (WX, 0.0, ZC + T), {"+y": ROOM})            # south wall
        self.box((-WX, LY1, PITZ), (WX, LY1 + T, ZC + T), {"-y": ROOM})       # north wall
        self.box((-WX, -T, ZC), (WX, LY1 + T, ZC + T), {"-z": AURORA})        # aurora sky ceiling

        # --- SUN vista on the north wall (the landmark you run toward) ---
        x0, x1, z0, z1 = SUN
        self.box((x0, LY1 - 16, z0), (x1, LY1, z1), {"-y": SUN_SH})

        # --- VORTEX portal recessed on the east wall ---
        y0, y1, z0, z1 = VTX
        self.box((WX - 16, y0, z0), (WX, y1, z1), {"-x": VTX_SH})

        # --- RAIN data-wall on the west wall, north section ---
        self.box((-WX, 1500.0, 60.0), (-WX + 16, 1900.0, 520.0), {"+x": RAIN})

        # --- PLASMA energy pillars: cover + rhythm down the runway ---
        for py in (380.0, 720.0, 1440.0, 1760.0):
            for px in (-300.0, 300.0):
                self.box((px - 40, py - 40, 0.0), (px + 40, py + 40, ZC),
                         {"+x": PLASMA, "-x": PLASMA, "+y": PLASMA, "-y": PLASMA})

        # --- verticality: ramp up the west side -> mezzanine ledge ---
        ramp = [(-WX, 200.0), (-WX + 230, 200.0), (-WX + 230, 600.0), (-WX, 600.0)]
        self.solids.append(make_prism(ramp, PITZ, [0.0, 0.0, 224.0, 224.0],
                                      tex=ROOM))
        self.box((-WX, 600.0, 208.0), (-WX + 230, 1500.0, 224.0), {"+z": ROOM})

        # --- lava-gap recovery: a push pad at the bottom flings you out north ---
        tb = make_box((-WX + 48, PY0 + 24, PITZ + 16), (WX - 48, PY1 - 24, PITZ + 96),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER)
        self.triggers.append((tb, {"classname": "trigger_push",
                                   "target": "show_boost"}))
        self.entities.append({"classname": "target_position",
                              "targetname": "show_boost",
                              "origin": f"0 {PY1 + 220:g} 320"})

        # --- entities ---
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE 64 shader showcase"})
        self.entities.append({"classname": "info_player_deathmatch",
                              "origin": "0 90 40", "angle": "90"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"0 240 {ZC - 80:g}", "angle": "90"})
        return self


def build():
    # make sure all shaderlib textures + the shaderlib/* shaders are deployed
    gallery.build()

    scripts = os.path.join(BASEOA, "scripts")
    os.makedirs(scripts, exist_ok=True)
    with open(os.path.join(scripts, "strafe64_showcase.shader"), "w") as fh:
        fh.write(showcase_shaders())

    bsp = os.path.join(BASEOA, "maps", MAP_NAME + ".bsp")
    stats = BspWriter(Showcase().build()).write(bsp)
    print(f"deployed showcase shader -> {scripts}/strafe64_showcase.shader")
    print(f"deployed map -> {bsp}  ({stats})")
    print(f"\nin-engine:  /map {MAP_NAME}")


if __name__ == "__main__":
    build()
