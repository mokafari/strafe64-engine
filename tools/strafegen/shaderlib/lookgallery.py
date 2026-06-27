#!/usr/bin/env python3
"""STRAFE 64 — IDENTITY LOOK gallery.

Where gallery.py showcases the ported "stunning" eye-candy shaders
(`shaderlib/*`), THIS gallery showcases the shaders that define how the *game*
looks — the identity set authored in strafegen's SHADER_SCRIPT and shipped in
every map. It borrows the original gallery's CLEAN presentation (a dark neon-grid
room so vivid panels pop) and shows each identity shader as a lit panel, with:

  * the 90s HOLOGRAPHIC RENDERER sky overhead (the real animated identity sky —
    a calm wireframe-globe dome with neon mesh DRIFTING across it), so you can
    judge and tune the sky live, and
  * arena-trail neon light-bars (strafe64/trailglow) framing the room, the
    glowing speed-trail look the project likes.

    python3 lookgallery.py              # build + deploy (loose, like gallery.py)
    # then in-engine:  /map strafe64_look

The identity *shader script* + textures are served globally by
zzz_strafe64_shader.pk3 (alphabetically-last, so it wins the FS dedup); the clean
room shader (shaderlib/room) + its grid textures come from gallery.py. This
script ensures both are deployed, then writes the bsp.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STRAFEGEN = os.path.dirname(HERE)
sys.path.insert(0, STRAFEGEN)
from strafegen import (  # noqa: E402
    make_box, BspWriter, build_detail_textures,
    TEX_FLOOR, TEX_WALL, TEX_SKY, PAL_PLAIN,
    SRC_ORANGE, SRC_GREY,
)
import gallery  # noqa: E402  (clean room shader + grid textures)

BASEOA = gallery.BASEOA
MAP_NAME = "strafe64_look"

ROOM = "shaderlib/room"          # clean dark neon-grid (from gallery.py)
TRAIL = "strafe64/trailglow"     # arena speed-trail glow (identity)

# bright per-panel vertex tints for the shaders that are vertex/entity-driven
CYAN    = (60, 230, 255)
MAGENTA = (255, 70, 190)

# identity shaders to mount as panels, left -> right, with the vertex tint used
# for the vertex-driven ones (surf/wall reproduce their real in-game tint).
PANELS = [
    ("textures/strafe64/surf",   SRC_ORANGE, "SURF  (dev floor)"),
    ("textures/strafe64/wall",   SRC_GREY,   "WALL  (dev wall)"),
    ("textures/strafe64/sky",    PAL_PLAIN,  "SKY   (holo dome)"),
    ("textures/strafe64/matrix", PAL_PLAIN,  "MATRIX rain"),
    ("strafe64/void",            PAL_PLAIN,  "VOID  kill-plane"),
    (TRAIL,                      CYAN,       "TRAILGLOW (arena)"),
    ("strafe64/ghost",           CYAN,       "GHOST silhouette"),
]

# room: a clean dark hall. Wide enough to clear the panel row, tall enough that
# the holographic sky ceiling reads.
Y0, Y1 = -340.0, 1180.0
H, T = 540.0, 16.0
PW, PH, GAP = 210.0, 210.0, 76.0
PANEL_Y = Y1 - 4.0


def _half_width():
    row_w = len(PANELS) * PW + (len(PANELS) - 1) * GAP
    return max(560.0, row_w / 2.0 + 220.0)   # clear the row with side margin


def _panel_rects(hw):
    row_w = len(PANELS) * PW + (len(PANELS) - 1) * GAP
    z0 = H * 0.5 - PH * 0.5
    rects, x = [], -row_w / 2.0
    for _ in range(len(PANELS)):
        rects.append((x, z0, x + PW, z0 + PH))
        x += PW + GAP
    return rects


class LookRoom:
    def __init__(self):
        self.solids = []
        self.triggers = []
        self.movers = []
        self.entities = []
        self.sections = []

    def build(self):
        HW = _half_width()

        # ---- clean dark shell: grid floor/walls, HOLOGRAPHIC SKY ceiling ----
        self.solids.append(make_box((-HW, Y0, -T), (HW, Y1, 0.0),
                                    tex=ROOM, draw={"+z"}, face_tex={"+z": ROOM}))
        self.solids.append(make_box((-HW, Y0, H), (HW, Y1, H + T),
                                    tex=TEX_SKY, palette=PAL_PLAIN,
                                    draw={"-z"}, face_tex={"-z": TEX_SKY}))
        self.solids.append(make_box((-HW - T, Y0, -T), (-HW, Y1, H + T),
                                    tex=ROOM, draw={"+x"}, face_tex={"+x": ROOM}))
        self.solids.append(make_box((HW, Y0, -T), (HW + T, Y1, H + T),
                                    tex=ROOM, draw={"-x"}, face_tex={"-x": ROOM}))
        self.solids.append(make_box((-HW, Y1, -T), (HW, Y1 + T, H + T),
                                    tex=ROOM, draw={"-y"}, face_tex={"-y": ROOM}))
        self.solids.append(make_box((-HW, Y0 - T, -T), (HW, Y0, H + T),
                                    tex=ROOM, draw={"+y"}, face_tex={"+y": ROOM}))

        # ---- identity panels on the far wall, facing the spawn (-y) ---------
        rects = _panel_rects(HW)
        for (shader, tint, _label), (x0, z0, x1, z1) in zip(PANELS, rects):
            self.solids.append(make_box(
                (x0, PANEL_Y, z0), (x1, Y1 + T, z1),
                tex=ROOM, palette=tint,
                draw={"-y"}, face_tex={"-y": shader}))

        # ---- arena-trail neon light-bars: a glowing baseboard rail along each
        # side wall + under the panels, the speed-trail look as room lighting.
        rail_z = 6.0
        self.solids.append(make_box((-HW + 2, Y0, rail_z), (-HW + 14, Y1, rail_z + 22),
                                    tex=TRAIL, palette=CYAN,
                                    draw={"+x"}, face_tex={"+x": TRAIL}))
        self.solids.append(make_box((HW - 14, Y0, rail_z), (HW - 2, Y1, rail_z + 22),
                                    tex=TRAIL, palette=MAGENTA,
                                    draw={"-x"}, face_tex={"-x": TRAIL}))
        self.solids.append(make_box((-HW, Y1 - 14, rail_z), (HW, Y1 - 2, rail_z + 22),
                                    tex=TRAIL, palette=CYAN,
                                    draw={"-y"}, face_tex={"-y": TRAIL}))

        # ---- a low VOID slab mid-hall you can walk over to read it underfoot -
        self.solids.append(make_box((-260.0, 420.0, 8.0), (260.0, 820.0, 16.0),
                                    tex="strafe64/void", palette=PAL_PLAIN,
                                    draw={"+z"}, face_tex={"+z": "strafe64/void"}))

        # ---- entities ------------------------------------------------------
        self.entities.append({"classname": "worldspawn",
                              "message": "STRAFE 64 identity look gallery"})
        self.entities.append({"classname": "info_player_deathmatch",
                              "origin": f"0 {Y0 + 140:g} 48", "angle": "90"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"0 {Y0 + 220:g} {H - 90:g}", "angle": "90"})
        return self


def build():
    # 1) ensure the clean room shader (shaderlib/room) + its grid textures are
    #    deployed — that's gallery.py's job; running it also keeps the eye-candy
    #    gallery fresh. Cheap and idempotent.
    gallery.build()

    # 2) deploy the identity textures loose so the map renders self-contained.
    #    We do NOT write a loose strafe64.shader — the identity shader is served
    #    by zzz_strafe64_shader.pk3; a loose copy would re-create the duplicate-
    #    definition collision this whole pass cleaned up.
    for arc, data in build_detail_textures().items():
        dst = os.path.join(BASEOA, arc)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)

    maps_dir = os.path.join(BASEOA, "maps")
    os.makedirs(maps_dir, exist_ok=True)
    bsp = os.path.join(maps_dir, MAP_NAME + ".bsp")
    stats = BspWriter(LookRoom().build()).write(bsp)

    print("STRAFE 64 identity look gallery (clean room + holographic sky)")
    for shader, _tint, label in PANELS:
        print(f"  panel  {label:<20} {shader}")
    print(f"deployed textures -> {BASEOA}/textures/strafe64/")
    print(f"deployed map      -> {bsp}  ({stats})")
    print()
    print(f"in-engine:  /map {MAP_NAME}")


if __name__ == "__main__":
    build()
