"""strafegen_showcase — one hand-composed map that shows off the ethereal look.

Open to the sky (like lun3dm5) so the baked sun rakes across everything:
a troweled-slab plaza around a sunken court, monolith towers in precast
panels with weathered crowns and block cladding, a board-formed cantilever
bridge on piers with stairs down to the plaza, a steel-grate catwalk with
gunmetal rails, a neon gate, neon reveals under every overhang, scale-giving
plinths, and debris cubes floating in the sky. Every surface wears a
material from strafegen_materials at real-world scale, so no two adjacent
elements read the same.

    python3 strafegen.py --showcase --bake --pk3      # -> strafe64_showcase
"""
import math
import random

from strafegen_geom import make_box, make_skybox
from strafegen_palettes import TEX_WALL, PAL_NEON_TRIM
import strafegen_materials as M

PAL_MAT = (232, 232, 236)        # near-white: the material photo carries tone
PAL_MAT_DARK = (200, 202, 210)   # a touch darker for weathered masses


class Showcase:
    def __init__(self, seed=64):
        self.seed = seed
        self.rng = random.Random(seed)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []
        self.clad_tex = M.tex("board")     # greeble_brutal cladding: light, fine grain
        # open air: far more sky light reaches every surface than in a killbox
        self.bake_scale = 1.0
        self.bake_ambient = ("8", "0.78 0.80 0.86")

    # ---- helpers ---------------------------------------------------------
    def box(self, mins, maxs, mat, pal=PAL_MAT, clad=False, faces=None):
        """Axis box in one material; `faces` overrides per face key
        (e.g. {"+z": "grate"}). clad=False keeps greeble off it."""
        ft = {k: M.tex(v) for k, v in (faces or {}).items()}
        b = make_box(mins, maxs, tex=M.tex(mat), palette=pal, face_tex=ft or None)
        b.no_clad = not clad
        self.solids.append(b)
        return b

    def neon(self, mins, maxs):
        b = make_box(mins, maxs, tex=TEX_WALL, palette=PAL_NEON_TRIM)
        b.no_clad = True
        self.solids.append(b)

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    # ---- the map ---------------------------------------------------------
    def build(self):
        P, C, D = 1280.0, 384.0, 96.0      # plaza half-size, court half-size, court depth
        self.sections.append(("showcase", {"plaza": int(2 * P)}))

        # plaza ring around the sunken court (slab), court floor below
        self.box((-P, -P, -64), (P, -C, 0), "slab")
        self.box((-P, C, -64), (P, P, 0), "slab")
        self.box((-P, -C, -64), (-C, C, 0), "slab")
        self.box((C, -C, -64), (P, C, 0), "slab")
        self.box((-C, -C, -D - 64), (C, C, -D), "slab")
        # court walls (board-formed) with a neon reveal just under the plaza lip
        for (mn, mx) in (((-C, -C, -D), (C, -C + 16, -8)), ((-C, C - 16, -D), (C, C, -8)),
                         ((-C, -C + 16, -D), (-C + 16, C - 16, -8)),
                         ((C - 16, -C + 16, -D), (C, C - 16, -8))):
            self.box(mn, mx, "board")
        self.neon((-C + 16, -C + 16, -14), (C - 16, -C + 19, -10))
        self.neon((-C + 16, C - 19, -14), (C - 16, C - 16, -10))
        # steps down into the court on the south side
        for i in range(3):
            z = -32.0 * (i + 1)
            self.box((-128, -C + 16 + 40 * i, z - 8), (128, -C + 56 + 40 * i, z + 32 - 8 * 0), "slab")

        # plinths in the court for scale (and cover)
        for x, y, w, d in ((-200, 120, 192, 64), (180, -80, 64, 192), (0, 230, 128, 128)):
            self.box((x - w / 2, y - d / 2, -D), (x + w / 2, y + d / 2, -D + 40), "panel")

        # monolith towers: precast body, weathered overhanging crown, clad band
        towers = ((-900, 700, 256, 320, 896), (820, 820, 320, 256, 640),
                  (-1040, -220, 192, 448, 512), (980, -360, 256, 256, 1024),
                  (-420, -980, 384, 160, 384))
        for (x, y, w, d, h) in towers:
            self.box((x - w / 2, y - d / 2, 0), (x + w / 2, y + d / 2, h), "panel",
                     clad=True)
            self.box((x - w / 2 - 16, y - d / 2 - 16, h), (x + w / 2 + 16, y + d / 2 + 16, h + 48),
                     "weather")
            self.neon((x - w / 2 - 3, y - d / 2 + 24, h - 24), (x - w / 2, y + d / 2 - 24, h - 20))

        # the island reads as one thick floating slab: weathered fascia skirt
        # around the plaza edge + a heavy underside mass
        for (mn, mx) in (((-P - 32, -P - 32, -320), (P + 32, -P, -16)),
                         ((-P - 32, P, -320), (P + 32, P + 32, -16)),
                         ((-P - 32, -P, -320), (-P, P, -16)),
                         ((P, -P, -320), (P + 32, P, -16))):
            self.box(mn, mx, "weather")
        self.box((-P, -P, -320), (P, P, -176), "weather")

        # colonnade: a rhythm of tall board columns under a precast beam --
        # the low sun throws a striped shadow field across the plaza
        cx = -700.0
        for k in range(8):
            y = -520.0 + k * 112.0
            self.box((cx - 16, y - 16, 0), (cx + 16, y + 16, 384), "board")
        self.box((cx - 40, -560, 384), (cx + 40, 320, 432), "panel")
        self.neon((cx + 40, -540, 386), (cx + 43, 300, 390))

        # raised terrace with stairs down to the plaza and a neon lip
        tx0, tx1, ty0, ty1, tz = 344.0, 856.0, -1110.0, -790.0, 112.0
        self.box((tx0, ty0, 0), (tx1, ty1, tz), "board", faces={"+z": "slab"})
        self.neon((tx0, ty1, tz - 12), (tx1, ty1 + 3, tz - 8))
        for i in range(7):
            top = tz - (i + 1) * tz / 7.0
            self.box((480, ty1 + i * 28, 0), (720, ty1 + (i + 1) * 28, max(top, 8.0)),
                     "slab")
        self.box((tx0 + 32, ty0 + 32, tz), (tx0 + 160, ty0 + 160, tz + 40), "panel")

        # long monolith wall with vertical window slots, each lit neon inside
        wy0, wy1, wh = 1000.0, 1048.0, 448.0
        x = -560.0
        for k in range(6):
            seg = 128.0 if k % 2 == 0 else 160.0
            self.box((x, wy0, 0), (x + seg, wy1, wh), "panel", clad=False)
            x += seg
            if k < 5:                                   # 24u slot, neon at its back
                self.box((x, wy1 - 8, 0), (x + 24, wy1, wh), "trim")
                self.neon((x + 2, wy1 - 11, 24), (x + 22, wy1 - 8, wh - 24))
                x += 24
        self.box((-576, wy0 - 16, wh), (x + 16, wy1 + 16, wh + 40), "weather")

        # board-formed cantilever bridge over the court on two piers
        bz = 176.0
        self.box((-448, -112, bz), (448, 112, bz + 40), "board",
                 faces={"+z": "slab"})
        for px in (-320, 320):
            self.box((px - 40, -64, -D), (px + 40, 64, bz), "board")
        self.neon((-448, -115, bz + 4), (448, -112, bz + 8))
        self.neon((-448, 112, bz + 4), (448, 115, bz + 8))
        # stairs from the bridge's east end down to the plaza
        steps = 11
        for i in range(steps):
            top = bz + 40 - (i + 1) * (bz + 40) / steps
            x0 = 448 + i * 26
            self.box((x0, -96, 0), (x0 + 26, 96, max(top, 8.0)), "board",
                     faces={"+z": "slab"})

        # steel catwalk along the north side: grate deck, gunmetal edges + rails
        cz, cy = 288.0, 560.0
        self.box((-640, cy - 56, cz - 8), (640, cy + 56, cz), "trim",
                 faces={"+z": "grate", "-z": "grate"})
        for sy in (cy - 56, cy + 52):
            self.box((-640, sy, cz), (640, sy + 4, cz + 44), "trim")
        for px in range(-640, 641, 320):
            self.box((px - 12, cy - 12, 0), (px + 12, cy + 12, cz - 8), "trim")
        self.neon((-640, cy - 59, cz - 6), (640, cy - 56, cz - 3))

        # neon gate on the plaza, framing the view into the court
        gx, gy = 0.0, -760.0
        self.box((gx - 176, gy - 16, 0), (gx - 144, gy + 16, 320), "trim")
        self.box((gx + 144, gy - 16, 0), (gx + 176, gy + 16, 320), "trim")
        self.box((gx - 176, gy - 16, 320), (gx + 176, gy + 16, 352), "trim")
        self.neon((gx - 144, gy - 4, 8), (gx - 140, gy + 4, 316))
        self.neon((gx + 140, gy - 4, 8), (gx + 144, gy + 4, 316))
        self.neon((gx - 140, gy - 4, 312), (gx + 140, gy + 4, 316))

        # debris cubes drifting in the sky beyond the plaza (out of reach)
        for _ in range(34):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(1500, 2200)
            s = self.rng.choice((48, 64, 96, 128, 160))
            x, y = r * math.cos(a), r * math.sin(a)
            z = self.rng.uniform(700, 1700)
            self.box((x - s / 2, y - s / 2, z - s / 2), (x + s / 2, y + s / 2, z + s / 2),
                     "weather", pal=PAL_MAT_DARK)

        # spawns on the plaza facing the court, items for scale
        for k in range(6):
            a = math.pi / 6 + k * math.pi / 3
            x, y = 720 * math.cos(a), 720 * math.sin(a)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", -700, -700, 420, angle="45")
        self.place("item_health_large", 0, 0, -D + 24)
        self.place("item_armor_combat", 0, 0, bz + 64)
        self.place("weapon_railgun", 0, cy, cz + 24)

        # open air: a tall sky hull well clear of everything
        self.solids += make_skybox(-2560, -2560, -320, 2560, 2560, 2400)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 // ETHEREAL showcase"})
        return self
