"""strafegen_arena — the velodrome Arena + the dojo Pit + the LatticeArena."""
import math
import random

import strafegen_gfx as gfx
import strafegen_config as cfg
from strafegen_geom import *
from strafegen_palettes import *
from strafegen_textures import *
from strafegen_physics import *


class Arena:
    """Deathmatch bowl: a 16-segment banked velodrome rings an arena
    floor with a tiered center tower, pillars, jump pads and items.
    The bank is walkable (~30 deg < MIN_WALK_NORMAL limit) so bots can
    use it too; humans ride it at speed like a wall-of-death."""

    SEGS = 16

    def __init__(self, seed, difficulty):
        self.rng = random.Random(seed ^ 0x0A12E7A)
        self.seed = seed
        self.diff = difficulty
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def _pt(self, r, i):
        a = 2.0 * math.pi * i / self.SEGS
        return (r * math.cos(a), r * math.sin(a))

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def build(self):
        rng = self.rng
        R1 = 1280.0 + 64 * self.diff + rng.randrange(0, 257, 64)
        BANK_W = 384.0
        BANK_H = float(rng.randrange(192, 257, 16))  # 26-34 deg: ridable
        WALL_T = 64.0                   # AND bot-walkable (< 45.5 deg)
        WALL_H = BANK_H + 384.0
        R2 = R1 + BANK_W
        center = rng.choice(("tower", "crater"))
        self.sections.append(("arena floor", {"radius": int(R1)}))
        self.sections.append(("velodrome", {
            "segments": self.SEGS, "bank": f"{BANK_H:g}z/{BANK_W:g}u"}))

        def bank_z(r):
            return BANK_H * (r - R1) / BANK_W

        # floor: one convex prism
        foot = [self._pt(R1 + 8, i) for i in range(self.SEGS)]
        self.solids.append(make_prism(foot, -48, [0.0] * self.SEGS,
                                      palette=PAL_FLOORA))
        # banked ring + containment wall, segment by segment
        for i in range(self.SEGS):
            a0, a1 = self._pt(R1, i), self._pt(R1, i + 1)
            b0, b1 = self._pt(R2, i), self._pt(R2, i + 1)
            self.solids.append(make_prism(
                [a0, a1, b1, b0], -48, [0.0, 0.0, BANK_H, BANK_H],
                palette=PAL_BANK))   # deep-blue standard Source dev surface
            w0, w1 = self._pt(R2 + WALL_T, i), self._pt(R2 + WALL_T, i + 1)
            self.solids.append(make_prism(
                [b0, b1, w1, w0], -48, [WALL_H] * 4,
                tex=TEX_WALL, palette=PAL_WALLA))

        # boost gates: trigger_push bands on the bank that throw riders a
        # segment forward along the lane — the loop is the fast way around
        gates = 3 + self.diff
        self.sections.append(("boost gates", {"count": gates}))
        seg_a = 2.0 * math.pi / self.SEGS
        g0 = rng.randrange(self.SEGS)
        Ra, Rb = R1 + 64.0, R2 - 64.0
        Rm = R1 + 0.45 * BANK_W
        for g in range(gates):
            seg = (g0 + g * self.SEGS // gates) % self.SEGS
            th = (seg + 0.5) * seg_a

            def lane_pt(r, dth):
                return (r * math.cos(th + dth), r * math.sin(th + dth))

            hw = seg_a * 0.18   # half the band's angular width
            za, zb = bank_z(Ra), bank_z(Rb)
            foot_g = [lane_pt(Ra, -hw), lane_pt(Ra, hw),
                      lane_pt(Rb, hw), lane_pt(Rb, -hw)]
            self.solids.append(make_prism(     # band sits 8u proud
                foot_g, -48, [za + 8, za + 8, zb + 8, zb + 8],
                palette=PAL_GATE))
            tb = make_prism(
                [lane_pt(Ra, -hw * 1.6), lane_pt(Ra, hw * 1.6),
                 lane_pt(Rb, hw * 1.6), lane_pt(Rb, -hw * 1.6)],
                za - 8, [zb + 128] * 4,
                tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER)
            self.triggers.append((tb, {
                "classname": "trigger_push",
                "target": f"gate_{g}",
            }))
            # apex one segment counter-clockwise along the lane
            tx, ty = lane_pt(Rm, 1.2 * seg_a)
            self.place("target_position", tx, ty, bank_z(Rm) + 224,
                       targetname=f"gate_{g}")
        # armor-shard speed line rewards staying in the loop
        for i in range(0, self.SEGS, 2):
            a = (i + 0.5) * seg_a
            self.place("item_armor_shard", Rm * math.cos(a),
                       Rm * math.sin(a), bank_z(Rm) + 24)

        # pure arena combat: NO race layer here. The velodrome lap timer and
        # ghost (trigger_race_start / trigger_race_finish + painted stripes)
        # are intentionally omitted — this is a deathmatch pit, not a time
        # trial. The bank is still ridden for speed, just untimed.

        # center structure, seeded: tiered tower or a quad crater.
        # rises are tuned to the movement mod: 56 single-jumpable,
        # 74 double-jump-only (LEDGE_SJ 63.6 < 74 < LEDGE_DJ 92.4)
        t1, t3 = 56.0, 176.0
        rim = 74.0
        assert LEDGE_SJ < rim < LEDGE_DJ
        # GFX: dress the centerpiece + pillars in the machined-metal recipes
        # (vectorgun/sword arena reads as an industrial neon pit). hull/chrome
        # are opaque & solid, so this is a pure draw-shader swap — collision
        # unchanged. Falls back to the dev palette when --no-gfx.
        HULL   = gfx.TEX_HULL   if cfg.GFX else None
        CHROME = gfx.TEX_CHROME if cfg.GFX else None
        if center == "tower":
            self.sections.append(("center tower", {"tiers": 3}))
            self.solids.append(make_box((-256, -256, 0), (256, 256, t1),
                                        tex=HULL or TEX_FLOOR, palette=PAL_CENTER))
            self.solids.append(make_box((-160, -160, t1), (160, 160, 120),
                                        tex=HULL or TEX_FLOOR, palette=PAL_CENTER))
            self.solids.append(make_box((-96, -96, 120), (96, 96, t3),
                                        tex=CHROME or TEX_FLOOR, palette=PAL_CENTER))
            self.place("item_quad", 0, 0, t3 + 24)
            self.place("item_health_large", 208, 208, t1 + 24)
            self.place("item_health_large", -208, -208, t1 + 24)
            apex_z = t3 + 256
        else:
            # crater: damage floor inside double-jump-only walls; the
            # quad sits on a safe pedestal — dive in, pay, climb out
            self.sections.append(("center crater", {"rim": int(rim)}))
            for mins, maxs in (((-288, 176), (288, 288)),
                               ((-288, -288), (288, -176)),
                               ((176, -176), (288, 176)),
                               ((-288, -176), (-176, 176))):
                self.solids.append(make_box(
                    (mins[0], mins[1], 0), (maxs[0], maxs[1], rim),
                    tex=HULL or TEX_FLOOR, palette=PAL_CENTER))
            self.solids.append(make_box((-176, -176, 0), (176, 176, 8),
                                        palette=PAL_DANGER))   # keep red hazard cue
            self.solids.append(make_box((-64, -64, 8), (64, 64, 32),
                                        tex=CHROME or TEX_FLOOR, palette=PAL_LEDGE))
            self.place("item_quad", 0, 0, 56)
            for mins, maxs in (((-176, 80), (176, 176)),
                               ((-176, -176), (176, -80)),
                               ((80, -80), (176, 80)),
                               ((-176, -80), (-80, 80))):
                hb = make_box((mins[0], mins[1], 8), (maxs[0], maxs[1], 72),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER,
                              draw=set())
                self.triggers.append((hb, {
                    "classname": "trigger_hurt",
                    "dmg": "10", "spawnflags": "16",   # SLOW: 1 tick/sec
                }))
            self.place("item_health_large", 232, 232, rim + 24)
            self.place("item_health_large", -232, -232, rim + 24)
            apex_z = rim + 288
        # ramps up to the center, east and west; base sits 16u into the
        # floor so the outer face keeps area when the top edge drops to 0
        self.solids.append(make_box((256, -96, -16), (512, 96, t1),
                                    palette=PAL_CENTER,
                                    top_drop={5: t1, 7: t1}))
        self.solids.append(make_box((-512, -96, -16), (-256, 96, t1),
                                    palette=PAL_CENTER,
                                    top_drop={4: t1, 6: t1}))
        # kicker wedges north and south: hit them at speed (36.9 deg,
        # still bot-walkable) and ground speed becomes a launch arc
        self.sections.append(("kickers", {"count": 2}))
        self.solids.append(make_box((-96, 384, -16), (96, 512, 96),
                                    palette=PAL_GATE,
                                    top_drop={6: 96, 7: 96}))
        self.solids.append(make_box((-96, -512, -16), (96, -384, 96),
                                    palette=PAL_GATE,
                                    top_drop={4: 96, 5: 96}))

        # pillars for cover and walljumps: seeded count, size, layout
        n_pil = rng.randint(3, 6)
        self.sections.append(("pillars", {"count": n_pil}))
        pr = 0.55 * R1
        a_off = rng.uniform(0, 2 * math.pi)
        spawn_blockers = []     # footprints spawns must NOT land inside
        for k in range(n_pil):
            a = a_off + k * 2 * math.pi / n_pil
            px, py = pr * math.cos(a), pr * math.sin(a)
            half = rng.choice((48, 64, 80))
            tall = rng.randrange(256, 385, 32)
            self.solids.append(make_box((px - half, py - half, 0),
                                        (px + half, py + half, tall),
                                        tex=HULL or TEX_WALL, palette=PAL_PILLAR))
            spawn_blockers.append((px, py, half))

        # jump pads arcing onto the center
        self.sections.append(("jump pads", {"count": 4}))
        pad_r = 0.72 * R1
        for k in range(4):
            a = k * math.pi / 2
            px, py = pad_r * math.cos(a), pad_r * math.sin(a)
            self.solids.append(make_box((px - 48, py - 48, 0),
                                        (px + 48, py + 48, 10),
                                        palette=PAL_PAD))
            spawn_blockers.append((px, py, 48))     # don't spawn on a launch pad
            tb = make_box((px - 48, py - 48, 10), (px + 48, py + 48, 74),
                          tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER,
                          draw=set())
            self.triggers.append((tb, {
                "classname": "trigger_push",
                "target": f"padtarget_{k}",
            }))
            self.place("target_position", px * 0.30, py * 0.30, apex_z,
                       targetname=f"padtarget_{k}")

        # aerial platforms: floating stepping-stones for bhop chains, air
        # revectoring and aerial sword kills. A weaving spiral between the
        # pillars and the center — alternating radius (in/out) AND height
        # (up/down) forces you to air-strafe a new direction from slab to slab
        # instead of a flat hop. Launch off a jump pad / kicker, then chain.
        n_plat = 8
        self.sections.append(("sky platforms", {"count": n_plat}))
        plat_r = 0.46 * R1
        pa_off = rng.uniform(0, 2 * math.pi)
        for k in range(n_plat):
            a = pa_off + k * 2 * math.pi / n_plat
            rad = plat_r * (0.82 if k % 2 else 1.12)            # weave in / out
            px, py = rad * math.cos(a), rad * math.sin(a)
            pz = 150.0 + 20.0 * k + (104.0 if k % 2 else 0.0)   # spiral + zigzag
            ph = rng.choice((80, 96, 112))
            self.solids.append(make_box((px - ph, py - ph, pz),
                                        (px + ph, py + ph, pz + 16),
                                        palette=PAL_LEDGE))
            if k % 2 == 0:    # a shard on every other slab baits the chain
                self.place("item_armor_shard", px, py, pz + 40)

        # prize ledges at the top of the bank: ride the velodrome to shop.
        # seeded axis: north/south some seeds, east/west others. SWORD ARENA:
        # both ledges are survival rewards (heavy armor + mega), no guns.
        self.sections.append(("bank ledges", {"items": "armor, mega"}))
        swap = rng.random() < 0.5
        for s, item in ((1, "item_armor_body"), (-1, "item_health_mega")):
            lo, hi = sorted((s * (R2 - 192), s * (R2 - 16)))
            if swap:
                mins, maxs = (lo, -112), (hi, 112)
                ix, iy = s * (R2 - 104), 0
            else:
                mins, maxs = (-112, lo), (112, hi)
                ix, iy = 0, s * (R2 - 104)
            self.solids.append(make_box(
                (mins[0], mins[1], BANK_H - 128),
                (maxs[0], maxs[1], BANK_H), palette=PAL_LEDGE))
            self.place(item, ix, iy, BANK_H + 24)

        # floor items — SWORD ARENA: no guns or ammo, only survival pickups, so
        # the fight stays a pure blade duel. Health/armor reward working the
        # velodrome and the platforms instead of camping a weapon spawn.
        self.sections.append(("items", {}))

        def ring(classname, r, k8, z=24.0):
            a = k8 * math.pi / 4
            self.place(classname, r * math.cos(a), r * math.sin(a), z)

        ring("item_health_large", 0.80 * R1, 0)
        ring("item_health_large", 0.80 * R1, 4)
        ring("item_armor_combat", 0.80 * R1, 2)
        ring("item_armor_combat", 0.80 * R1, 6)
        ring("item_health_large", 0.45 * R1, 1)
        ring("item_health_large", 0.45 * R1, 5)
        ring("item_armor_combat", 0.80 * R1, 3)
        ring("item_armor_combat", 0.80 * R1, 7)
        for k in range(4):
            ring("item_health", 0.30 * R1, 2 * k + 1)
            ring("item_armor_shard", 0.62 * R1, 2 * k)

        # spawns face the tower. NUDGE each off any pillar/pad footprint first —
        # the spawn ring (0.60 R1) overlaps the pillar ring (0.55 R1 +/- up to 80),
        # so an angle-aligned spawn would land INSIDE a pillar and eject the player
        # at huge speed ("stuck in pillar"). avoid_footprints rotates it clear.
        spawn_r = 0.60 * R1
        for k in range(8):
            a = math.pi / 8 + k * math.pi / 4
            x, y = spawn_r * math.cos(a), spawn_r * math.sin(a)
            x, y = avoid_footprints(x, y, spawn_blockers, clearance=40.0, ring_r=spawn_r)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, -0.7 * R1, 560, angle="90")

        # enclosure: the ceiling sits exactly on the wall top, so nothing
        # escapes over the rim and no void trigger is needed
        m = 64.0
        self.solids += make_skybox(
            -R2 - WALL_T - m, -R2 - WALL_T - m, -48 - m,
            R2 + WALL_T + m, R2 + WALL_T + m, WALL_H)
        self.entities.insert(0, {
            "classname": "worldspawn",
            "message": f"STRAFE64 arena {self.seed} "
                       f"({center}, difficulty {self.diff})",
            "music": pick_music(MUSIC_BREAKCORE, self.seed),
        })
        return self




class Pit:
    """A small sealed combat arena for the dojo ARENA archetype — close spawns,
    cover pillars, weapons in reach. The velodrome was too spread for bots to
    ever meet (10 bots, 0 frags); this packs them in so they actually fight."""

    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []      # manifest (for the generate() report)

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def build(self):
        W, H, T = 448.0, 448.0, 16.0       # tighter pit -> more encounters
        self.sections.append(("combat pit", {"size": int(2 * W)}))
        # sealed room: floor, ceiling, four flush walls (no overlaps -> bspc-safe)
        self.solids.append(make_box((-W, -W, -T), (W, W, 0), palette=PAL_FLOORA))
        self.solids.append(make_box((-W, -W, H), (W, W, H + T),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W - T, -W, 0), (-W, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((W, -W, 0), (W + T, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, -W - T, 0), (W, -W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, W, 0), (W, W + T, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        # cover pillars
        for px, py in ((-220, -220), (220, -220), (-220, 220), (220, 220)):
            self.solids.append(make_box((px - 48, py - 48, 0), (px + 48, py + 48, 200),
                                        tex=TEX_WALL, palette=PAL_PILLAR))
        # eight close spawns in a tight ring, facing center
        for k in range(8):
            a = k * 2 * math.pi / 8
            x, y = 280 * math.cos(a), 280 * math.sin(a)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, 0, H - 96, angle="0")
        # weapons + armor: the reason to fight
        for cls, x, y in (("weapon_railgun", -340, 0), ("weapon_rocketlauncher", 340, 0),
                          ("weapon_shotgun", 0, -340), ("item_armor_combat", 0, 340),
                          ("item_health_large", 0, 0), ("item_quad", 0, 120)):
            self.place(cls, x, y, 24)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 combat pit"})
        return self




class LatticeArena:
    """A 16-pilot LATTICE heat arena. Unlike the tight combat Pit (8 close
    spawns → 16 bots telefrag-cascade, ~6 elims/window of pure noise), this is a
    big OPEN sealed box with a SPREAD grid of 24 spawns (300u apart, well past the
    telefrag bbox) so a full field seats with no spawn kills, plus room to carve
    long trail-walls a rival actually crosses (the radius sweep showed rival kills
    rise with carve room). Flat, neutral floor, pit-free, sealed → AAS-friendly."""

    def __init__(self, seed=0, weapons=True):
        self.rng = random.Random(seed or 64)
        self.weapons = weapons          # weapons-light variant: the trail is the
                                        # SOLE decider (purer lattice TTK signal)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def build(self):
        W, H, T = 768.0, 512.0, 16.0       # big + open: room to lay long walls
        self.sections.append(("lattice arena",
                              {"size": int(2 * W), "spawns": 24,
                               "weapons": "full" if self.weapons else "light"}))
        # sealed room: floor, ceiling, four flush walls (flush -> bspc-safe)
        self.solids.append(make_box((-W, -W, -T), (W, W, 0), palette=PAL_FLOORA))
        self.solids.append(make_box((-W, -W, H), (W, W, H + T),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W - T, -W, 0), (-W, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((W, -W, 0), (W + T, W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, -W - T, 0), (W, -W, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        self.solids.append(make_box((-W, W, 0), (W, W + T, H),
                                    tex=TEX_WALL, palette=PAL_WALLA))
        # CORNER PILLARS: 4 fat pillars in the corners (centre ±500, half 100 ->
        # outer face 168u off each ±768 wall: a comfortable wall-kick channel, not
        # the cramped 56u chute of the last pass). Cover + a kick surface.
        PIL, PILH = 100.0, 320.0
        pillars = [(-500, -500), (500, -500), (-500, 500), (500, 500)]
        for px, py in pillars:
            self.solids.append(make_box((px - PIL, py - PIL, 0),
                                        (px + PIL, py + PIL, PILH),
                                        tex=TEX_WALL, palette=PAL_PILLAR))

        def slab(x, y, z, half):
            self.solids.append(make_box((x - half, y - half, z),
                                        (x + half, y + half, z + 16),
                                        palette=PAL_LEDGE))
        # PLATFORMS INTERLEAVED with the pillars: the corners hold pillars, so the
        # platforms sit on the CARDINALS between them — going round the perimeter
        # you alternate pillar / platform / pillar / platform. The outer cardinals
        # hug the side walls (wall-kick straight up the flat wall to mount), then a
        # bhop chain steps inward (mid stones -> centre perch). Smaller than before
        # so the floor lanes stay open for long trail-walls.
        for cx, cy in ((640, 0), (-640, 0), (0, 640), (0, -640)):
            slab(cx, cy, 152, 80)                # outer cardinal (wall-kick mount)
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            slab(sx * 250, sy * 250, 208, 60)    # inner diagonal bhop stone
        slab(0, 0, 250, 88)                       # centre perch (chain summit)

        # 24 SPREAD spawns: 5x5 grid (step 300) minus the centre cell, each facing
        # the middle. 300u spacing >> telefrag bbox so a 16-bot fill never doubles.
        # The 4 extreme-corner cells land inside the corner pillars, so nudge any
        # blocked spawn straight in toward the centre until the player box clears
        # (inward stays in bounds — unlike the ring-rotate nudge, which can push a
        # near-wall grid point through a wall). validate_spawns() is the backstop.
        blockers = [(px, py, PIL) for px, py in pillars]
        step = 300.0
        for gx in (-2, -1, 0, 1, 2):
            for gy in (-2, -1, 0, 1, 2):
                if gx == 0 and gy == 0:
                    continue            # leave the centre clear (24 spawns)
                x, y = gx * step, gy * step
                s = 1.0
                while s > 0.0 and any(
                        abs(x * s - bx) < bh + 40.0 and abs(y * s - by) < bh + 40.0
                        for bx, by, bh in blockers):
                    s -= 0.05           # walk the cell inward toward the origin
                x, y = x * s, y * s
                yaw = math.degrees(math.atan2(-y, -x)) % 360 if (x or y) else 0
                self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, 0, H - 96, angle="0")
        if self.weapons:
            # a few weapons so combat is an OPTION (kills + lattice both end a heat),
            # but spread thin so the lattice stays the dominant threat
            # guns on the outer cardinals (clear of the corner pillars); the
            # armours sit on the x-axis inner ring, clear of both the pillars
            # (now at ±500 in the corners) and the inward-nudged corner spawns.
            for cls, x, y in (("weapon_railgun", -680, 0), ("weapon_rocketlauncher", 680, 0),
                              ("weapon_shotgun", 0, -680), ("weapon_lightning", 0, 680),
                              ("item_armor_combat", -450, 0), ("item_armor_combat", 450, 0),
                              ("item_health_large", 0, 0)):
                self.place(cls, x, y, 24)
        # weapons-light: NO guns at all → the trail is the only weapon. (Bots keep
        # the spawn machinegun but with no ammo pickups it's a non-factor.)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 lattice arena"})
        return self


