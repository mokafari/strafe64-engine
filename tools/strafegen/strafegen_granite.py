"""strafegen_granite — a stepped granite ziggurat, built for vertical play.

The marble temple's sibling, same movement budget (jump ~45u, air jump ~56u,
stair-jump ~74u, walljumps ~80u):

  * a five-tier ziggurat, 96u per tier (jump + air jump), each tier a 104u-deep
    walkway so fights can run around it; the summit holds the quad;
  * four wall-run approaches on the axes: two tall, perfectly flat polished
    granite walls 192u apart, each run ending on a launch block that reaches the
    second tier;
  * four black-marble wall-jump shafts at the corners, a railgun on each top;
  * brass nosings on every tier edge, neon under every tier lip (shade side);
  * the island parapet + invisible player clip from the temple (knockback
    no longer throws anyone into the void), spawns pushed clear of footprints.

    python3 strafegen.py --granite --bake --pk3      # -> strafe64_granite
"""
import math

from strafegen_geom import make_box, make_skybox, avoid_footprints
from strafegen_palettes import TEX_CLIP, CONTENTS_PLAYERCLIP
from strafegen_marble import Marble


class Granite(Marble):
    def build(self):
        P = 1100.0
        self.sections.append(("granite ziggurat", {"plaza": int(2 * P)}))
        blockers = []

        # ---- island: flamed granite plaza, polished border, skirt, parapet, clip
        self.box((-P, -P, -64), (P, P, 0), "granite_flamed")
        for (mn, mx) in (((-P, -P, -360), (P, -P + 32, -64)), ((-P, P - 32, -360), (P, P, -64)),
                         ((-P, -P + 32, -360), (-P + 32, P - 32, -64)),
                         ((P - 32, -P + 32, -360), (P, P - 32, -64))):
            self.box(mn, mx, "granite_grey")
        self.box((-P, -P, -360), (P, P, -200), "granite_flamed")
        self.neon((-P, -P - 3, -76), (P, -P, -72))
        self.neon((-P, P, -76), (P, P + 3, -72))
        self.neon((-P - 3, -P, -76), (-P, P, -72))
        self.neon((P, -P, -76), (P + 3, P, -72))
        for (mn, mx) in (((-P, -P, 0), (P, -P + 24, 32)), ((-P, P - 24, 0), (P, P, 32)),
                         ((-P, -P + 24, 0), (-P + 24, P - 24, 32)),
                         ((P - 24, -P + 24, 0), (P, P - 24, 32))):
            self.box(mn, mx, "granite_grey")
            self.box((mn[0], mn[1], 32), (mx[0], mx[1], 36), "brass")
        for (mn, mx) in (((-P, -P, 36), (P, -P + 8, 1200)), ((-P, P - 8, 36), (P, P, 1200)),
                         ((-P, -P + 8, 36), (-P + 8, P - 8, 1200)),
                         ((P - 8, -P + 8, 36), (P, P - 8, 1200))):
            c = make_box(mn, mx, tex=TEX_CLIP, contents=CONTENTS_PLAYERCLIP, draw=set())
            c.no_clad = True
            self.solids.append(c)

        # ---- the ziggurat: five tiers, 96u each, 160u walkways ----------------
        tiers, rise, step = 5, 96.0, 104.0      # 104u walkways: 560..144 half-size
        base = 560.0
        for i in range(tiers):
            h = base - i * step
            z1 = rise * (i + 1)
            # polished sides, flamed (grippy-looking) walking top
            self.box((-h, -h, rise * i), (h, h, z1), "granite_grey",
                     faces={"+z": "granite_flamed"})
            # brass nosing on the edge + neon under the lip on the shade sides
            for (mn, mx) in (((-h, -h, z1), (h, -h + 6, z1 + 2)), ((-h, h - 6, z1), (h, h, z1 + 2)),
                             ((-h, -h + 6, z1), (-h + 6, h - 6, z1 + 2)),
                             ((h - 6, -h + 6, z1), (h, h - 6, z1 + 2))):
                self.box(mn, mx, "brass")
            self.neon((-h - 3, -h, z1 - 14), (-h, h, z1 - 10))
            self.neon((-h, -h - 3, z1 - 14), (h, -h, z1 - 10))
        blockers.append((0.0, 0.0, base * 1.4142 + 24))
        top = rise * tiers
        # ziggurat stairways: a straight flight of 16u steps (walkable -- no
        # jump needed) up the middle of each face to the summit, white marble
        # with brass nosings. Bots path the tiers via these (playtest tracking:
        # only 2 of 24 bot samples were on the ziggurat before), and 96u tier
        # hops stay the fast route for players.
        nst = int(top // 16)
        run = (base - (base - step * (tiers - 1))) / nst
        for k in range(4):
            for j in range(nst):
                r0 = base - j * run
                z1 = 16.0 * (j + 1)
                if k == 0:
                    mn, mx = (r0 - run, -64, 0), (r0 + 20, 64, z1)
                elif k == 1:
                    mn, mx = (-64, r0 - run, 0), (64, r0 + 20, z1)
                elif k == 2:
                    mn, mx = (-r0 - 20, -64, 0), (-r0 + run, 64, z1)
                else:
                    mn, mx = (-64, -r0 - 20, 0), (64, -r0 + run, z1)
                self.box(mn, mx, "marble_white")
        self.place("item_quad", 0, 0, top + 24)
        self.place("item_armor_combat", 0, -(base - step) - 40, rise * 2 + 24)
        self.place("item_health_large", 0, (base - step) + 40, rise * 2 + 24)

        # summit crown: a black marble ring of four posts with neon caps
        for (x, y) in ((-80, -80), (80, -80), (-80, 80), (80, 80)):
            self.box((x - 16, y - 16, top), (x + 16, y + 16, top + 160), "marble_black")
            self.neon((x - 16, y - 16, top + 160), (x + 16, y + 16, top + 163))

        # ---- wall-run approaches on the four axes ------------------------------
        for k in range(4):
            a = k * math.pi / 2
            ca, sa = math.cos(a), math.sin(a)
            # run from r=920 inward to r=640, walls 192u apart, 320u tall
            for side in (-1, 1):
                off = side * 96.0
                # wall as an axis box in the rotated frame (k multiples of 90)
                r0, r1 = 660.0, 900.0     # 1000 left 76u dead-end pockets at the parapet (bots idled there and burned)
                if k % 2 == 0:
                    x0, x1 = sorted((ca * r0, ca * r1))
                    y0, y1 = sorted((off, off + side * 24))
                else:
                    y0, y1 = sorted((sa * r0, sa * r1))
                    x0, x1 = sorted((off, off + side * 24))
                self.box((x0, y0, 48), (x1, y1, 368), "granite_grey")
            # launch block at the inner end: 96u up, reaches tier 2 (192) with an air jump
            lr = 612.0
            # offset 140u to the side so it doesn't block the stairway's foot
            lx, ly = ca * lr - sa * 140, sa * lr + ca * 140
            self.box((lx - 48, ly - 48, 0), (lx + 48, ly + 48, 96), "marble_black",
                     faces={"+z": "granite_flamed"})
            for rr in range(680, 901, 80):        # a line of discs along the run
                blockers.append((ca * rr, sa * rr, 150))

        # ---- corner wall-jump shafts, railgun on top ---------------------------
        for (sx, sy) in ((-800, -800), (800, -800), (-800, 800), (800, 800)):
            for side in (-1, 1):
                x0 = sx + side * 80
                x1 = x0 + side * 32
                self.box((min(x0, x1), sy - 96, 0), (max(x0, x1), sy + 96, 256), "marble_black")
            self.box((sx - 112, sy + (96 if sy > 0 else -224), 0),
                     (sx + 112, sy + (224 if sy > 0 else -96), 256), "marble_black",
                     faces={"+z": "granite_flamed"})
            self.neon((sx - 3, sy - 90, 16), (sx + 3, sy + 90, 19))
            # an armor shard rewards the wall-jump; the railgun lives on tier 4
            # (bots chased it here and stalled under the shafts -- they can't
            # wall-jump 256u, though AAS thinks they can)
            self.place("item_armor_shard", sx, sy + (160 if sy > 0 else -160), 280)
            blockers.append((sx, sy, 200))

        # ---- spawns -------------------------------------------------------------
        # corridors own the axes, shafts the diagonals: the clear lanes are at
        # 22.5 + 45k degrees, so spawns sit exactly there
        for k in range(8):
            a = math.radians(22.5 + k * 45)
            r = 860.0
            x, y = r * math.cos(a), r * math.sin(a)
            x, y = avoid_footprints(x, y, blockers, clearance=48.0, ring_r=r)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", -760, 0, 700, angle="0")
        # (r 620 put it INSIDE the south launch block -- "in solid, not reachable")
        self.place("weapon_rocketlauncher", 160, -(base) - 80, 24)
        # weapons + health ON the tiers: bots go where the items are, and the
        # tiers are where the vertical fights should happen (playtest 1: they
        # loitered at the plaza edge instead)
        h1, h2, h3 = base - step * 0.5, base - step * 1.5, base - step * 2.5
        for cls, x, y, z in (("weapon_shotgun", h1, h1, rise * 1),
                             ("weapon_plasmagun", -h1, -h1, rise * 1),
                             ("weapon_lightning", h2, -h2, rise * 2),
                             ("weapon_grenadelauncher", -h2, h2, rise * 2),
                             ("item_health", h3, 0, rise * 3), ("item_health", -h3, 0, rise * 3),
                             ("item_armor_shard", 0, h3, rise * 3), ("item_armor_shard", 0, -h3, rise * 3),
                             ("weapon_railgun", h3 - step, h3 - step, rise * 4)):
            self.place(cls, x, y, z + 24)

        hull = make_skybox(-2560, -2560, -420, 2560, 2560, 2600)
        if self.night:
            for b in hull:
                for f in b.faces:
                    if f.tex.endswith("/sky"):
                        f.tex = "textures/strafe64/sky_ether_night"
        self.solids += hull
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 // GRANITE ziggurat"
                                            + (" (night)" if self.night else "")})
        return self
