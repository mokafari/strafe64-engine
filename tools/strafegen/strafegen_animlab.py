#!/usr/bin/env python3
"""strafegen_animlab.py -- strafe64_animlab: a test bench for the character
animation states. One lane per behaviour, all lit and open to the sky:

  y  800  run/strafe track, 2400u of flat floor
  y  400  wall-run corridor: walls either side, 160u apart
  y    0  ledges 24..128u high, then a stair flight (vault, mantle, step)
  y -400  drop tower: stairs up to 128/256/384 decks for soft/hard landings
  y -800  slide ramp and jump pads
"""
from strafegen_geom import make_skybox
from strafegen_marble import Marble


class AnimLab(Marble):
    def build(self):
        P = 1400.0
        self.sections.append(("animation lab", {"plaza": int(2 * P)}))
        self.box((-P, -P, -64), (P, P, 0), "marble_tiles")
        for (mn, mx) in (((-P, -P, 0), (P, -P + 32, 256)), ((-P, P - 32, 0), (P, P, 256)),
                         ((-P, -P + 32, 0), (-P + 32, P - 32, 256)), ((P - 32, -P + 32, 0), (P, P - 32, 256))):
            self.box(mn, mx, "marble_black")
        # lane dividers on the floor
        for y in (600, 200, -200, -600):
            self.neon((-P + 32, y - 3, 0), (P - 32, y + 3, 1), floor=True)

        # wall-run corridor
        for side in (-1, 1):
            y0 = 400 + side * 80
            self.box((-900, min(y0, y0 + side * 32), 0), (900, max(y0, y0 + side * 32), 320), "marble_white")
            self.neon((-900, min(y0, y0 + side * 32), 320), (900, max(y0, y0 + side * 32), 322), floor=True)

        # ledges, rising, then stairs
        for i, h in enumerate((24, 48, 64, 96, 128)):
            x = -1000 + i * 240
            self.box((x, -96, 0), (x + 120, 96, h), "granite_grey", faces={"+z": "granite_flamed"})
        for i in range(12):
            x = 300 + i * 32
            self.box((x, -96, 0), (x + 32, 96, 16 * (i + 1)), "marble_white")
        self.box((684, -96, 0), (900, 96, 192), "marble_white")

        # drop tower decks with a stair up the back of each
        for i, h in enumerate((128, 256, 384)):
            x = -900 + i * 640
            self.box((x, -500, 0), (x + 200, -300, h), "granite_grey", faces={"+z": "granite_flamed"})
            for k in range(int(h // 16)):
                self.box((x + 200 + k * 24, -480, 0), (x + 224 + k * 24, -320, h - k * 16), "marble_white")

        # slide lane: long low-ceiling-free run with a shallow wedge of steps, plus pads
        for i in range(4):
            self.pad(i, -900 + i * 500, -800, -900 + i * 500 + 300, -800, 300 + i * 120)
        self.box((-1100, -1000, 0), (1100, -980, 64), "granite_grey")

        # sword practice: a slice of open floor at the run track's end has spawns
        for i, (x, y, a) in enumerate(((-1200, 800, 0), (-1200, 400, 0), (-1200, 0, 0), (-1200, -400, 0),
                                       (-1200, -800, 0), (1200, 800, 180), (1200, 0, 180), (1200, -800, 180))):
            self.place("info_player_deathmatch", x, y, 40, angle=a)
        self.place("info_player_intermission", 0, 0, 700, angle="0")
        self.place("item_health_large", 0, 800, 24)
        self.place("item_armor_combat", 0, 400, 24)
        self.place("item_health_large", 800, 0, 216)
        self.place("item_armor_shard", -800, -400, 152)
        self.place("item_armor_shard", -160, -400, 280)
        self.place("item_armor_combat", 480, -400, 408)

        self.solids += make_skybox(-2000, -2000, -320, 2000, 2000, 2000)
        self.entities.insert(0, {"classname": "worldspawn", "message": "STRAFE64 // ANIMATION LAB"})
        return self
