"""strafegen_marble — a vertical marble & granite temple, built for the moveset.

Sized from the movement budget (gravity 800): jump 270 ups -> ~45u, air jump
+~56u, two walljumps ~+80u, stair-jump ~74u. So:

  * spiral of floating granite platforms inside a black-marble atrium, 96u
    apart in z and <=200u apart in plan: one jump + air jump per step, climbing
    to the rooftop ring at 640u;
  * a wall-run gallery: two long, perfectly FLAT white marble walls 192u apart
    (never clad -- cladding steps break a wall-run), raised so the run carries
    you across a gap;
  * a wall-jump shaft: two black walls 160u apart, 224u to the ledge;
  * four brass-rimmed jump pads throwing you tangentially onto the rooftop
    ring, so pad -> ring -> spiral down is a loop;
  * spawns spread on the plaza and pushed clear of every footprint.

Neon is used sparingly and only where the sun (az ~30deg) can't reach: under
platform lips, inside the shaft, under the ring walkway.

    python3 strafegen.py --marble --bake --pk3      # -> strafe64_marble
"""
import math

from strafegen_geom import make_box, avoid_footprints
from strafegen_palettes import TEX_TRIGGER, CONTENTS_TRIGGER
from strafegen_showcase import Showcase


# vertex tone per material: polished white stone under a hard sun blows out at
# the near-white default, so the light stones sit lower and the dark ones
# a touch higher to keep their veining readable
MAT_PAL = {"marble_white": (168, 168, 172), "marble_tiles": (172, 172, 176),
           "marble_black": (236, 236, 240), "brass": (220, 216, 208)}


class Marble(Showcase):
    def __init__(self, seed=64, night=False):
        super().__init__(seed, night=night)
        # polished white stone: far less light than concrete before it blows out
        self.bake_scale = 0.85 if not night else 2.2

    def box(self, mins, maxs, mat, pal=None, clad=False, faces=None):
        return super().box(mins, maxs, mat, pal=pal or MAT_PAL.get(mat, (232, 232, 236)),
                           clad=clad, faces=faces)

    def pad(self, k, px, py, tx, ty, tz):
        """Brass-rimmed jump pad at (px,py) throwing toward (tx,ty,tz)."""
        self.box((px - 52, py - 52, 0), (px + 52, py + 52, 8), "granite_flamed")
        for (mn, mx) in (((px - 60, py - 60, 0), (px + 60, py - 52, 12)),
                         ((px - 60, py + 52, 0), (px + 60, py + 60, 12)),
                         ((px - 60, py - 52, 0), (px - 52, py + 52, 12)),
                         ((px + 52, py - 52, 0), (px + 60, py + 52, 12))):
            self.box(mn, mx, "brass")
        self.neon((px - 40, py - 40, 8), (px + 40, py + 40, 9), floor=True)
        tb = make_box((px - 52, py - 52, 9), (px + 52, py + 52, 80),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_push", "target": f"mb_pad_{k}"}))
        self.place("target_position", tx, ty, tz, targetname=f"mb_pad_{k}")

    def build(self):
        P = 960.0      # was 1280: too open -- bots skirmished but no fight resolved
        A, AH, T = 512.0, 640.0, 32.0          # atrium half-size, height, wall thickness
        self.sections.append(("marble temple", {"plaza": int(2 * P), "atrium": int(2 * A)}))
        blockers = []

        # ---- plaza: large-format marble tiles, flamed granite border band -----
        self.box((-P, -P, -64), (P, P, 0), "marble_tiles")
        for (mn, mx) in (((-P, -P, 0), (P, -P + 96, 2)), ((-P, P - 96, 0), (P, P, 2)),
                         ((-P, -P + 96, 0), (-P + 96, P - 96, 2)),
                         ((P - 96, -P + 96, 0), (P, P - 96, 2))):
            self.box(mn, mx, "granite_flamed")
        # thick floating base: the white marble skirt stays INSIDE the footprint
        # -- an outward lip made a walkable ledge below the parapet that bots
        # dropped onto and ran along (playtest 2), and off into the void
        for (mn, mx) in (((-P, -P, -360), (P, -P + 32, -64)),
                         ((-P, P - 32, -360), (P, P, -64)),
                         ((-P, -P + 32, -360), (-P + 32, P - 32, -64)),
                         ((P - 32, -P + 32, -360), (P, P - 32, -64))):
            self.box(mn, mx, "marble_white")
        self.neon((-P, -P - 3, -76), (P, -P, -72))
        self.neon((-P, P, -76), (P, P + 3, -72))
        self.neon((-P - 3, -P, -76), (-P, P, -72))
        self.neon((P, -P, -76), (P + 3, P, -72))
        self.box((-P, -P, -360), (P, P, -200), "granite_grey")
        # low parapet (32u: stops walk-offs, still hoppable on purpose) with a
        # brass cap -- the playtest's first death was a bot walking off the edge
        for (mn, mx) in (((-P, -P, 0), (P, -P + 24, 32)), ((-P, P - 24, 0), (P, P, 32)),
                         ((-P, -P + 24, 0), (-P + 24, P - 24, 32)),
                         ((P - 24, -P + 24, 0), (P, P - 24, 32))):
            self.box(mn, mx, "granite_flamed")
            self.box((mn[0], mn[1], 32), (mx[0], mx[1], 36), "brass")
        self.neon((-P - 35, -P - 35, -28), (P + 35, -P - 32, -24))
        self.neon((-P - 35, P + 32, -28), (P + 35, P + 35, -24))
        self.neon((-P - 35, -P - 32, -28), (-P - 32, P + 32, -24))
        self.neon((P + 32, -P - 32, -28), (P + 35, P + 32, -24))

        # ---- atrium: black Marquina outside, white Carrara inside ---------------
        # two-tone: an all-black shaft read as a void; the white inner faces
        # bounce the sky light down onto the spiral. 160u doorway in each wall.
        door = 80.0
        for (axis, s) in ((0, -1), (0, 1), (1, -1), (1, 1)):
            inner = {(0, 1): "-x", (0, -1): "+x", (1, 1): "-y", (1, -1): "+y"}[(axis, s)]
            white = {inner: "marble_white"}
            for half in (-1, 1):
                if axis == 0:          # walls at x = +-A, running in y
                    x0, x1 = (s * A - T, s * A) if s > 0 else (s * A, s * A + T)
                    y0, y1 = (door, A) if half > 0 else (-A, -door)
                    self.box((x0, y0, 0), (x1, y1, AH), "marble_black", faces=white)
                else:                  # walls at y = +-A, running in x
                    y0, y1 = (s * A - T, s * A) if s > 0 else (s * A, s * A + T)
                    x0, x1 = (door, A) if half > 0 else (-A, -door)
                    self.box((x0, y0, 0), (x1, y1, AH), "marble_black", faces=white)
            # lintel over the doorway
            if axis == 0:
                x0, x1 = (s * A - T, s * A) if s > 0 else (s * A, s * A + T)
                self.box((x0, -door, 256), (x1, door, AH), "marble_black", faces=white)
            else:
                y0, y1 = (s * A - T, s * A) if s > 0 else (s * A, s * A + T)
                self.box((-door, y0, 256), (door, y1, AH), "marble_black", faces=white)
            # brass pilasters on the black exterior every 128u (1u proud so a
            # wall-run across the facade doesn't snag), skipping the doorway
            for t in range(int(-A + 64), int(A), 128):
                if abs(t) < door + 16:
                    continue
                if axis == 0:
                    xo = s * A
                    self.box((min(xo, xo + s), t - 5, 0), (max(xo, xo + s), t + 5, AH), "brass")
                else:
                    yo = s * A
                    self.box((t - 5, min(yo, yo + s), 0), (t + 5, max(yo, yo + s), AH), "brass")
        blockers.append((0.0, 0.0, A * 1.4142 + 24))   # square: use its corner radius

        # rooftop ring walkway ON the atrium walls, flush with their outer face:
        # no outward overhang and no rail on the outside -- pad arcs came in
        # under a 48u overhang, bonked, and stuck to the wall face (wall-run)
        rz, rw = AH, 128.0
        for (mn, mx) in (((-A, -A, rz), (A, -A + rw, rz + 24)),
                         ((-A, A - rw, rz), (A, A, rz + 24)),
                         ((-A, -A + rw, rz), (-A + rw, A - rw, rz + 24)),
                         ((A - rw, -A + rw, rz), (A, A - rw, rz + 24))):
            self.box(mn, mx, "granite_grey", faces={"+z": "granite_flamed"})
        # thin brass inlay strip flush along the outer edge (decor, not a rail)
        for (mn, mx) in (((-A, -A, rz + 24), (A, -A + 6, rz + 25)),
                         ((-A, A - 6, rz + 24), (A, A, rz + 25)),
                         ((-A, -A + 6, rz + 24), (-A + 6, A - 6, rz + 25)),
                         ((A - 6, -A + 6, rz + 24), (A, A - 6, rz + 25))):
            self.box(mn, mx, "brass")
        ri = A - rw
        self.neon((-ri, -ri - 3, rz - 4), (ri, -ri, rz - 1))
        self.neon((-ri, ri, rz - 4), (ri, ri + 3, rz - 1))

        # ---- the spiral: granite platforms, 96u per step, lit under the lip ----
        steps = 6
        for i in range(steps):
            a = math.radians(225 + i * 62)
            r = 300.0
            cx, cy, z = r * math.cos(a), r * math.sin(a), 96.0 * (i + 1)
            w = 152.0
            self.box((cx - w / 2, cy - w / 2, z - 24), (cx + w / 2, cy + w / 2, z),
                     "granite_grey", faces={"+z": "marble_white"})
            # a thin lit frame under the lip (a full-size glowing underside read
            # as a glare panel from below)
            e, z0 = 12.0, z - 27
            self.neon((cx - w / 2 + e, cy - w / 2 + e, z0), (cx + w / 2 - e, cy - w / 2 + e + 3, z - 24))
            self.neon((cx - w / 2 + e, cy + w / 2 - e - 3, z0), (cx + w / 2 - e, cy + w / 2 - e, z - 24))
            self.neon((cx - w / 2 + e, cy - w / 2 + e + 3, z0), (cx - w / 2 + e + 3, cy + w / 2 - e - 3, z - 24))
            self.neon((cx + w / 2 - e - 3, cy - w / 2 + e + 3, z0), (cx + w / 2 - e, cy + w / 2 - e - 3, z - 24))
        # top of the spiral steps onto the ring (at 576 -> ring 640 via jump)
        self.place("item_health_large", 0, 0, 24)

        # ---- wall-run gallery: two flat white marble walls, a gap between ------
        gx0, gx1, gy = 620.0, 900.0, 0.0
        for side in (-1, 1):
            y0 = gy + side * 96
            y1 = y0 + side * 32
            self.box((gx0, min(y0, y1), 64), (gx1, max(y0, y1), 448), "marble_white")
        # the run's purpose: launch decks at each end, 64u up, a void between
        self.box((gx0 - 80, gy - 96, 0), (gx0, gy + 96, 64), "granite_grey",
                 faces={"+z": "granite_flamed"})
        self.box((gx1, gy - 96, 0), (gx1 + 36, gy + 96, 192), "granite_grey",
                 faces={"+z": "granite_flamed"})
        self.neon((gx1 - 3, gy - 90, 180), (gx1, gy + 90, 184))
        blockers.append(((gx0 + gx1) / 2, gy, 260))

        # ---- wall-jump shaft: two black walls 160u apart, ledge on top ----------
        sx, sy = -700.0, -700.0
        for side in (-1, 1):
            x0 = sx + side * 80
            x1 = x0 + side * 32
            self.box((min(x0, x1), sy - 96, 0), (max(x0, x1), sy + 96, 224), "marble_black")
            self.neon((min(x0, x1) if side > 0 else max(x0, x1) - 3, sy - 4, 16),
                      ((min(x0, x1) + 3) if side > 0 else max(x0, x1), sy + 4, 208))
        self.box((sx - 112, sy + 96, 0), (sx + 112, sy + 224, 224), "marble_black",
                 faces={"+z": "granite_flamed"})
        self.place("weapon_railgun", sx, sy + 160, 248)
        blockers.append((sx, sy, 200))

        # ---- jump pads: tangential throws onto the rooftop ring ---------------
        pad_r = 860.0     # on the diagonals: 608 per axis, outside the +-512 atrium (700 put them INSIDE it)
        for k in range(4):
            a = math.radians(45 + 90 * k)
            px, py = pad_r * math.cos(a), pad_r * math.sin(a)
            # a trigger_push aims the arc's APEX; you keep travelling past it.
            # Apex 40u outside the (now flush) ring edge and 160u above it: the
            # arc clears the wall top, and the inward drift on the way down lands
            # you ON the ring (sweep-tested by dropping onto a pad and tracing).
            ta = a + math.radians(30)
            ar = A + 40
            self.pad(k, px, py, ar * math.cos(ta), ar * math.sin(ta), AH + 160)
            blockers.append((px, py, 70))

        # ---- floating stair-jump route up the south face to the ring ----------
        # nine granite blocks, +72u each (a stair-jump inside the 400ms rejump
        # window), 120u apart: the skill route to the high road, beside the
        # pads (easy) and the spiral (slow). Neon frame under each lip.
        for i in range(9):
            x = -440.0 + i * 110.0
            z = 72.0 * (i + 1)
            self.box((x - 44, -A - 140, z - 20), (x + 44, -A - 52, z), "granite_grey",
                     faces={"+z": "granite_flamed"})
            self.neon((x - 36, -A - 132, z - 23), (x + 36, -A - 129, z - 20))
            blockers.append((x, -A - 96, 80))       # one per block: a line, not a disc

        # ---- statement pieces: a black monolith, white marble obelisk ----------
        self.box((-880, 560, 0), (-800, 820, 900), "marble_black")
        self.neon((-883, 564, 24), (-880, 816, 28))
        self.box((700, 700, 0), (780, 780, 760), "marble_white")
        self.box((680, 680, 760), (800, 800, 784), "brass")
        blockers += [(-840, 690, 160), (740, 740, 80)]

        # low granite cover on the plaza ring (48u: waist-high, jumpable) so
        # duels between the atrium and the parapet have something to play around
        for (x, y, w, d) in ((-720, 0, 160, 64), (-170, -740, 64, 160), (170, 740, 64, 160),
                             (560, -560, 128, 128), (-400, 760, 160, 64)):
            self.box((x - w / 2, y - d / 2, 0), (x + w / 2, y + d / 2, 48), "granite_grey",
                     faces={"+z": "granite_flamed"})
            blockers.append((x, y, max(w, d) / 2 + 24))

        # ---- spawns: ten, spread, nudged off every footprint ------------------
        for k in range(10):
            a = math.radians(k * 36)      # bearings clear of the south stair route
            r = 860.0 if k % 2 else 770.0
            x, y = r * math.cos(a), r * math.sin(a)
            x, y = avoid_footprints(x, y, blockers, clearance=48.0, ring_r=r)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", -700, 700, 820, angle="315")
        for cls, x, y, z in (("weapon_rocketlauncher", 0, -A - 140, 24),
                             ("item_armor_combat", 0, A + 140, 24),
                             ("item_quad", 0, 0, AH + 48)):
            self.place(cls, x, y, z)

        hull = __import__("strafegen_geom").make_skybox(-2560, -2560, -420, 2560, 2560, 2600)
        if self.night:
            for b in hull:
                for f in b.faces:
                    if f.tex.endswith("/sky"):
                        f.tex = "textures/strafe64/sky_ether_night"
        self.solids += hull
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 // MARBLE temple"
                                            + (" (night)" if self.night else "")})
        return self
