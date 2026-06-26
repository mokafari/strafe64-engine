"""strafegen_course — the linear movement-run Course builder + dojo recipes."""
import math
import random

import strafegen_config as cfg
from strafegen_geom import *
from strafegen_palettes import *
from strafegen_textures import *
from strafegen_physics import *

# ---- tower deck spacing / void pacing ----
# tower decks: vertical spacing between chained section decks, and how
# long the void should take to swallow one deck (the pace pressure)
DECK_RISE = 1024.0
VOID_SECONDS_PER_DECK = (90.0, 70.0, 55.0)   # by difficulty

# Walljump-hall width as a fraction of the computed walljump reach, indexed by
# difficulty. Harder difficulties sit closer to the crossable limit.
HALL_REACH_SCALES = (0.88, 0.94, 0.99)
VOID_FLAT_RATE        = (8.0, 12.0, 16.0)    # ups/s for length-1 courses
VOID_DELAY            = (20.0, 15.0, 12.0)   # grace seconds


# ======================================================================
# course generation
# ======================================================================
# dojo recipes: fixed section sequences that each isolate one archetype of
# the gameplay we want, for the bot-playtest dojo. Straight (no turns) so the
# metric reflects the mechanic, not navigation noise.
DOJO_RECIPES = {
    "speed":  ["sec_gaps", "sec_gaps", "sec_gaps"],          # strafe-accel lane
    "flow":   ["sec_bhop", "sec_slide", "sec_walls",
               "sec_bhop", "sec_slide", "sec_walls"],         # chain everything
    "ztrick": ["sec_gaps", "sec_bhop", "sec_gaps", "sec_bhop"],  # gaps + bhop
    # (was gaps+double-jump-tower; bots stall climbing the tower — iter12)
    # "arena" is built from the Arena class, not a Course recipe
    # isolation dojos for the new clustertruck/trackmania sections — each
    # repeats one archetype so bot telemetry attributes cleanly to it
    "slalom":  ["sec_slalom", "sec_slalom", "sec_slalom"],
    "hurdles": ["sec_hurdles", "sec_hurdles", "sec_hurdles"],
    "hazard":  ["sec_hazard", "sec_hazard", "sec_hazard"],
    "movers":  ["sec_movers", "sec_movers", "sec_movers"],
    "fork":    ["sec_fork", "sec_fork"],
    # a full representative mix, bait-driven so a lone bot actually runs it
    # end-to-end — the integration test for the "fun course" recipe (tower
    # omitted: bots are known to stall climbing it, see ztrick note)
    # a full representative mix in the fun arc (speed -> flow -> spice),
    # bait-driven so a lone bot runs it end-to-end — the integration test for
    # the recipe build() lays down (tower omitted: bots stall climbing it)
    "showcase": ["sec_gaps", "sec_bhop", "sec_fork", "sec_slalom", "sec_slide",
                 "sec_hurdles", "sec_walls", "sec_movers", "sec_hazard"],
}


class Course:
    def __init__(self, seed, difficulty, length, void=True,
                 voidrise=None, voiddelay=None, recipe=None, combat=False,
                 item_trail=False, mods=None):
        self.rng = random.Random(seed)
        self.seed = seed
        self.diff = difficulty
        self.mods = mods or cfg.GenMods()   # geometry modifiers (identity default)
        self.recipe = recipe        # dojo section sequence, or None for normal
        self.combat = combat        # True = lace the arc with slice-gate enemies
                                    # (the COMBAT recipe); False = pure movement
        self.item_trail = item_trail    # True = breadcrumb pickups so goal-seeking
                                    # bots track the path (bot test bed); off by default
        # gap scale: easy courses shrink jumps, hard ones stretch them
        self.scale = (0.85, 1.0, 1.12)[difficulty]
        self.length = length
        self.void = void
        self.voidrise = voidrise        # ups/s override, None = derive
        self.voiddelay = voiddelay      # seconds override, None = derive
        self.solids = []        # world brushes
        self.triggers = []      # (brush, entity-dict) -> nodraw submodels
        self.movers = []        # (brush, entity-dict) -> *drawn* submodels (func_bobbing)
        self.entities = []
        self.sections = []      # manifest for the report
        self.checkpoints = []   # respawn points, one per section (see below)
        # cursor: position is the center of the *front edge* of the last
        # platform top; dir is the axis-aligned travel direction
        self.pos = [0.0, 0.0, 0.0]
        self.dir = (0, 1)
        self.lowest = 0.0
        # CARRIED SPEED: one accumulating momentum value threaded through every
        # section, so the course is built for a run that gets FASTER the further
        # it goes (gaps/spacing scale to it) instead of each section cold-starting
        # at run speed and capping low — the bhop chain compounds end-to-end.
        self.speed = RUN_SPEED
        self.SPEED_MAX = 720.0   # ceiling the generator sizes gaps for

    # ---- cursor helpers ------------------------------------------------
    def right(self):
        return (self.dir[1], -self.dir[0])

    def advance(self, dist, side=0.0, dz=0.0):
        d, r = self.dir, self.right()
        self.pos[0] += d[0] * dist + r[0] * side
        self.pos[1] += d[1] * dist + r[1] * side
        self.pos[2] += dz
        self.lowest = min(self.lowest, self.pos[2])

    def yaw(self):
        return {(0, 1): 90, (1, 0): 0, (-1, 0): 180, (0, -1): 270}[self.dir]

    def turn(self, leftright):
        d = self.dir
        self.dir = (-d[1] * leftright, d[0] * leftright)

    def world_rect(self, fwd_len, half_w):
        """Axis box footprint for a platform starting at the cursor."""
        d, r = self.dir, self.right()
        x0, y0 = self.pos[0], self.pos[1]
        xs = [x0 + r[0] * half_w, x0 - r[0] * half_w,
              x0 + d[0] * fwd_len + r[0] * half_w,
              x0 + d[0] * fwd_len - r[0] * half_w]
        ys = [y0 + r[1] * half_w, y0 - r[1] * half_w,
              y0 + d[1] * fwd_len + r[1] * half_w,
              y0 + d[1] * fwd_len - r[1] * half_w]
        return (min(xs), min(ys)), (max(xs), max(ys))

    def platform(self, fwd_len, half_w, palette, thick=24.0, tex=TEX_FLOOR):
        (x0, y0), (x1, y1) = self.world_rect(fwd_len, half_w)
        top = self.pos[2]
        b = make_box((x0, y0, top - thick), (x1, y1, top),
                     tex=tex, palette=palette)
        self.solids.append(b)
        self.advance(fwd_len)
        return b

    def gap(self, dist, assumed_speed, dz=0.0):
        """Move the cursor across a jump gap, asserting solvability."""
        limit = SAFETY * jump_range(assumed_speed)
        if dz < 0:
            # falling extends airtime: t = (vz + sqrt(vz^2 + 2g·h)) / g
            t = (JUMP_VELOCITY + math.sqrt(JUMP_VELOCITY ** 2
                                           + 2 * GRAVITY * -dz)) / GRAVITY
            limit = SAFETY * assumed_speed * t
        assert dist <= limit, (
            f"unreachable gap {dist:.0f} > {limit:.0f} "
            f"(speed {assumed_speed:.0f}, dz {dz:.0f})")
        self.advance(dist, dz=dz)

    def place_drone(self, fwd=0.0, side=0.0, up=56.0, health=1, wait=3):
        """Drop a slice_drone on the line — a momentum gate you cut THROUGH.
        It's CONTENTS_CORPSE: the sword cleaves it but a running player passes
        clean through, so an un-sliced gate never breaks the line (you just
        miss the speed feed). Placed relative to the cursor's heading; faces
        back at the incoming runner so it reads as a thing to cut."""
        d, r = self.dir, self.right()
        x = self.pos[0] + d[0] * fwd + r[0] * side
        y = self.pos[1] + d[1] * fwd + r[1] * side
        z = self.pos[2] + up
        self.entities.append({
            "classname": "slice_drone",
            "origin": f"{x:g} {y:g} {z:g}",
            "angle": str((self.yaw() + 180) % 360),
            "health": str(health),
            "wait": str(wait),
        })

    def enemy_phrase(self, n=3, step=72.0, spread=64.0, up=64.0):
        """A short 2-3 enemy 'phrase': a cluster of slice-gates fanned across the
        lane and marching down it, so the player cuts the whole group in ONE
        flowing pass (DOOM/Neon White pulse — cluster then rest, never carpet).
        Gates are CONTENTS_CORPSE so a missed one never blocks the line; you just
        forgo the speed feed. Caps at 3 so it never forces a stop."""
        n = max(1, min(n, 3))
        for k in range(n):
            side = (k - (n - 1) / 2.0) * spread     # fan the cluster across the lane
            self.place_drone(fwd=40.0 + k * step, side=side, up=up)
        self.sections.append(("enemy phrase", {"count": n}))

    def wall_seg(self, p, q, away_from, top, thick=28.0, height=320.0,
                 palette=PAL_WALLS):
        """A thin angled WALL from world point p to q, extruded on the side
        AWAY from `away_from` so its playable face points back toward the line.
        make_prism takes an arbitrary footprint, so this builds walls at any
        angle even though the section cursor only ever runs on the 4-dir grid —
        the trick that lets a corner be two 45-degree bounce surfaces instead of
        one speed-dumping square turn."""
        ux, uy = q[0] - p[0], q[1] - p[1]
        L = math.hypot(ux, uy) or 1.0
        nx, ny = -uy / L, ux / L                       # a unit perpendicular
        mx, my = (p[0] + q[0]) / 2.0, (p[1] + q[1]) / 2.0
        if (mx - away_from[0]) * nx + (my - away_from[1]) * ny < 0:
            nx, ny = -nx, -ny                          # point the solid outward
        foot = [(p[0], p[1]), (q[0], q[1]),
                (q[0] + nx * thick, q[1] + ny * thick),
                (p[0] + nx * thick, p[1] + ny * thick)]
        self.solids.append(make_prism(
            foot, top - 24, [top + height] * 4,
            tex=TEX_WALL, palette=palette))

    # ---- sections ------------------------------------------------------
    def sec_start(self):
        self.sections.append(("start", {}))
        d = self.dir
        cx, cy = self.pos[0] + d[0] * 192, self.pos[1] + d[1] * 192
        top = self.pos[2]
        (sx0, sy0), (sx1, sy1) = self.world_rect(448, 224)
        self.platform(448, 224, PAL_START, thick=32)
        # the race clock restamps while you stand here and starts ticking
        # the moment you leave the pad
        tb = make_box((sx0, sy0, top), (sx1, sy1, top + 128),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_race_start"}))
        for i, off in enumerate((-120, -40, 40, 120)):
            r = self.right()
            self.entities.append({
                "classname": "info_player_deathmatch",
                "origin": f"{cx + r[0] * off - d[0] * 96:g} "
                          f"{cy + r[1] * off - d[1] * 96:g} {top + 40:g}",
                "angle": str(self.yaw()),
            })
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": "start_dest",
            "origin": f"{cx:g} {cy:g} {top + 40:g}",
            "angle": str(self.yaw()),
        })
        self.entities.append({
            "classname": "info_player_intermission",
            "origin": f"{cx:g} {cy:g} {top + 360:g}",
            "angle": str(self.yaw()),
        })

    def sec_gaps(self, n=None):
        n = n or self.mods.count(self.rng.randint(5, 7), lo=1)
        self.sections.append(("strafe gaps", {"count": n}))
        speed = max(self.speed, RUN_SPEED + 20.0)  # carry the run's momentum in
        for i in range(n):
            # gaps stay clearable from a cold entry (don't REQUIRE carried speed,
            # or a slowed run dead-ends here); a fast run clears them trivially
            g = min((150 + 14 * i) * self.scale,
                    SAFETY * jump_range(speed) - 8)
            self.gap(g, speed, dz=-8)
            side = self.rng.uniform(-90, 90) * self.scale
            self.advance(0, side=side)
            size = max(64.0, (96 - 4 * i) * (1.1 - 0.1 * self.diff))
            self.platform(size * 2, size, PAL_GAPS)
            if self.combat and i % 2 == 1:   # a gate over every other landing:
                self.place_drone(fwd=-size, up=72.0)   # cut it as you fly the pad
            speed = min(speed + 18, self.SPEED_MAX)  # chained hops build speed
        self.speed = speed  # carry it out to the next section

    def sec_bhop(self, n=None):
        n = n or self.mods.count(self.rng.randint(8, 11), lo=1)
        self.sections.append(("bhop lane", {"count": n}))
        for i in range(n):
            # gaps stay sized for RUN_SPEED so even a dead chain clears them,
            # but a clean chain physically COMPOUNDS speed — carried out below
            g = min((100 + 4 * i) * self.scale, SAFETY * jump_range(RUN_SPEED))
            self.gap(g, RUN_SPEED)  # solvable even with a dead chain
            self.advance(0, side=self.rng.uniform(-40, 40))
            self.platform(112, 60, PAL_BHOP, thick=16)
        self.speed = min(self.speed + n * 9.0, self.SPEED_MAX)  # the chain builds speed

    def sec_slide(self):
        self.sections.append(("slide ramp", {}))
        self.platform(256, 128, PAL_SLIDE)
        # ramp: top surface drops `h` over `run` — crouch at the crest,
        # slide friction (pm_slideFrictionScale) keeps nearly all of it
        run, h = 512.0, 160.0
        (x0, y0), (x1, y1) = self.world_rect(run, 128)
        top = self.pos[2]
        d = self.dir
        far = {  # top corner ids at the far end of the travel axis
            (0, 1): (6, 7), (0, -1): (4, 5),
            (1, 0): (5, 7), (-1, 0): (4, 6),
        }[d]
        self.solids.append(make_box(
            (x0, y0, top - h - 32), (x1, y1, top),
            tex=TEX_FLOOR, palette=PAL_SLIDE,
            top_drop={far[0]: h, far[1]: h}))
        self.advance(run, dz=-h)
        # slide-jump across the big gap; ~70% of the potential energy of
        # the drop survives friction, then pm_slideJumpBoost kicks
        v_exit = math.sqrt(self.speed ** 2 + 1.4 * GRAVITY * h) * SLIDE_JUMP
        g = min(260 * self.scale, SAFETY * jump_range(v_exit))
        self.gap(g, v_exit, dz=-48)
        self.platform(288, 128, PAL_SLIDE)
        self.speed = min(v_exit, self.SPEED_MAX)  # the slide GENERATES speed — carry it

    def sec_walls(self):
        self.sections.append(("walljump hall", {}))
        self.platform(192, 96, PAL_WALLS)
        # hall width derives from wall-jump reach so it is always crossable
        # by wall-jumps alone (wall-running, added to the moveset, only makes
        # it easier). Harder difficulties sit closer to the limit.
        kick_t = 2.0 * WALLJUMP_VZ / GRAVITY
        reach = SAFETY * (jump_range(330) + WALLJUMP_MAX * 330 * kick_t)
        hall = int(reach * HALL_REACH_SCALES[self.diff]) // 8 * 8
        assert hall <= reach, f"hall {hall} > walljump reach {reach:.0f}"
        (x0, y0), (x1, y1) = self.world_rect(hall, 144)
        base = self.pos[2]
        d, r = self.dir, self.right()
        # two flanking walls, no floor between them; inner faces sit at
        # the hall edge (offset 160 - half thickness 16 = 144)
        half_len = (y1 - y0) / 2 if d[0] == 0 else (x1 - x0) / 2
        for side in (-1, 1):
            cxa = (x0 + x1) / 2 + r[0] * side * 160
            cya = (y0 + y1) / 2 + r[1] * side * 160
            wall_mins = (cxa - (16 if d[0] == 0 else half_len),
                         cya - (half_len if d[0] == 0 else 16),
                         base - 64)
            wall_maxs = (cxa + (16 if d[0] == 0 else half_len),
                         cya + (half_len if d[0] == 0 else 16),
                         base + 288)
            self.solids.append(make_box(
                wall_mins, wall_maxs, tex=TEX_WALL, palette=PAL_WALLS))
        if self.diff < 2:
            mid_save = (self.pos[0], self.pos[1], self.pos[2])
            self.advance(hall / 2 - 56)
            self.platform(112, 64, PAL_WALLS, thick=16)
            self.pos = [mid_save[0], mid_save[1], mid_save[2]]
        self.advance(hall, dz=32)
        self.platform(224, 96, PAL_WALLS)

    def sec_tower(self, n=4):
        self.sections.append(("double-jump tower", {"steps": n}))
        rise = 68.0  # > LEDGE_SJ (63.6) so it forces the double jump,
        assert LEDGE_SJ < rise <= LEDGE_DJ - 12
        for _ in range(n):
            self.advance(0, dz=rise)
            self.platform(176, 112, PAL_TOWER, thick=rise + 24)

    def sec_slalom(self, n=None):
        """Wide deck studded with staggered pillars — weave through at speed.
        Pure run (no jumps), so it always solves; the line you pick through
        the columns is the skill, trackmania-style."""
        n = n or self.mods.count(self.rng.randint(5, 8), lo=1)
        self.sections.append(("slalom", {"pillars": n}))
        half_w = 240.0 * (1.0 + 0.05 * self.diff)
        seg = 256.0
        length = seg * (n + 1)
        (x0, y0), (x1, y1) = self.world_rect(length, half_w)
        top = self.pos[2]
        self.solids.append(make_box((x0, y0, top - 24), (x1, y1, top),
                                    tex=TEX_FLOOR, palette=PAL_GAPS))
        d, r = self.dir, self.right()
        ph = 34.0          # slimmer pillars: a clear racing line always stays
        for i in range(n):
            f = seg * (i + 1)
            # alternate sides; harder difficulties push pillars toward the
            # centre line so the gaps to thread are tighter
            side = (1 if i % 2 == 0 else -1) * self.rng.uniform(
                95, 175 - 30 * self.diff)
            cx = self.pos[0] + d[0] * f + r[0] * side
            cy = self.pos[1] + d[1] * f + r[1] * side
            self.solids.append(make_box(
                (cx - ph, cy - ph, top), (cx + ph, cy + ph, top + 200),
                tex=TEX_WALL, palette=PAL_PILLAR))
        self.advance(length)

    def sec_hurdles(self, n=None):
        """A wide lane crossed by low walls, each with an alternating-side gap.
        Two ways through every hurdle: vault the wall straight (hold the line)
        or cut to the open gap (weave). The gap also gives bot nav a walkable
        route, so the section flows for AI and humans alike."""
        n = n or self.mods.count(self.rng.randint(4, 6), lo=1)
        self.sections.append(("hurdles", {"count": n}))
        half_w = 200.0
        seg = 220.0
        gap_w = 150.0
        length = seg * (n + 1)
        (x0, y0), (x1, y1) = self.world_rect(length, half_w)
        top = self.pos[2]
        self.solids.append(make_box((x0, y0, top - 24), (x1, y1, top),
                                    tex=TEX_FLOOR, palette=PAL_BHOP))
        d, r = self.dir, self.right()
        hh = 32.0 + 6.0 * self.diff          # < JUMP_APEX (45.6u), clearable
        assert hh < JUMP_APEX
        for i in range(n):
            f = seg * (i + 1)
            cx = self.pos[0] + d[0] * f
            cy = self.pos[1] + d[1] * f
            gc = (1 if i % 2 == 0 else -1) * (half_w * 0.5)   # gap side alternates
            g0, g1 = gc - gap_w / 2, gc + gap_w / 2
            for a, b in ((-half_w, g0), (g1, half_w)):        # wall on each side of gap
                if b - a < 24:
                    continue
                p1x, p1y = cx + r[0] * a + d[0] * -14, cy + r[1] * a + d[1] * -14
                p2x, p2y = cx + r[0] * b + d[0] * 14, cy + r[1] * b + d[1] * 14
                self.solids.append(make_box(
                    (min(p1x, p2x), min(p1y, p2y), top),
                    (max(p1x, p2x), max(p1y, p2y), top + hh),
                    tex=TEX_WALL, palette=PAL_WALLS))
        self.advance(length)
        self.platform(160, half_w, PAL_BHOP)

    def sec_hazard(self, n=None):
        """"The floor is lava" — wide pads separated by red lethal-looking gaps.
        A missed leap is NOT an instakill (a trigger_hurt just respawns a runner
        at the start and, for a bot, loops it there forever); instead the fall
        drops into the course's own void/rescue, the same hostile floor the
        whole game is built on — you lose the line, eat the reset, and the
        rising void gains on you. Gaps are comfortable and slightly downhill so
        a clean run flows; the danger reads through the red pit and lip stripes."""
        n = n or self.mods.count(self.rng.randint(3, 5), lo=1)
        self.sections.append(("hazard pits", {"pits": n}))
        half_w = 168.0
        speed = max(self.speed, RUN_SPEED + 60.0)  # carry momentum in
        self.platform(224, half_w, PAL_BHOP)       # long take-off pad: room to line up
        for i in range(n):
            # comfortable gaps, slightly downhill (like sec_gaps, which bots
            # clear): the drop buys airtime so a clean jump lands easily
            dz = -28.0
            pit = min((130 + 6 * i) * self.scale,
                      SAFETY * jump_range(speed) - 48)
            (px0, py0), (px1, py1) = self.world_rect(pit, half_w)
            d = self.dir
            zt = self.pos[2]
            # red rails lining the pit's lateral edges: reads as a lava trench
            # from a run. They sit just below the lip (no jump obstruction) and
            # leave the floor open to the void (a fall is caught by the course
            # rescue — no instakill that would loop a bot at the start).
            for s in (0, 1):
                if d[0] == 0:                # travel ±y: rails at x = px0 / px1
                    wx = px0 if s == 0 else px1 - 24
                    rmins, rmaxs = (wx, py0, zt - 64), (wx + 24, py1, zt - 8)
                else:                        # travel ±x: rails at y = py0 / py1
                    wy = py0 if s == 0 else py1 - 24
                    rmins, rmaxs = (px0, wy, zt - 64), (px1, wy + 24, zt - 8)
                self.solids.append(make_box(rmins, rmaxs, tex=TEX_WALL, palette=PAL_DANGER))
            # red warning stripe across the take-off lip — danger reads at speed
            lx = self.pos[0] - d[0] * 20
            ly = self.pos[1] - d[1] * 20
            z = self.pos[2]
            if d[0] == 0:                    # travelling ±y: stripe spans x
                smins, smaxs = (lx - half_w, ly - 12, z - 2), (lx + half_w, ly + 12, z + 3)
            else:                            # travelling ±x: stripe spans y
                smins, smaxs = (lx - 12, ly - half_w, z - 2), (lx + 12, ly + half_w, z + 3)
            self.solids.append(make_box(smins, smaxs, tex=TEX_FLOOR, palette=PAL_DANGER))
            self.gap(pit, speed, dz=dz)
            self.platform(224, half_w, PAL_BHOP)
            speed = min(speed + 8, self.SPEED_MAX)
        self.speed = speed  # carry it out

    def sec_movers(self, n=None):
        """Clustertruck moving platforms. The through-line is static stepping
        stones (a guaranteed jump path — bots can't path onto a func_bobbing,
        and a trap would break every mixed course), and beside each stone a
        bobbing platform sits as the stylish human line. No item bait on the
        bobbers: an item there only drags bot nav toward a platform it can't
        stand on. Vertical bob and lateral slide alternate; amplitudes stay
        small so the hop on is always there."""
        n = n or self.rng.randint(3, 4)
        self.sections.append(("moving platforms", {"count": n}))
        half_w = 144.0
        stone_half = 96.0
        amp = 40.0
        speed = max(self.speed, RUN_SPEED + 20.0)  # carry momentum in
        self.platform(176, half_w, PAL_PILLAR)
        d, r = self.dir, self.right()
        for i in range(n):
            g = min(196.0 * self.scale, SAFETY * jump_range(speed) - 8)
            self.gap(g, speed)
            # static stepping stone on the main line (bot + human safe)
            self.platform(stone_half * 2, stone_half, PAL_PILLAR)
            # bobbing platform set far off the alternating side — vertical bob
            # only, so it never swings into the static line and crushes a runner
            # standing there (a lateral slider at 230u did exactly that). Far
            # enough out that bot nav never strays toward it.
            side = 1 if i % 2 == 0 else -1
            bcx = self.pos[0] - d[0] * stone_half + r[0] * side * 360
            bcy = self.pos[1] - d[1] * stone_half + r[1] * side * 360
            top = self.pos[2]
            b = make_box((bcx - 80, bcy - 80, top - 24), (bcx + 80, bcy + 80, top),
                         tex=TEX_FLOOR, palette=PAL_PAD)
            cycle = self.rng.uniform(2.4, 3.4)
            phase = round(self.rng.random(), 3)
            self.movers.append((b, {
                "classname": "func_bobbing",
                "height": f"{amp:g}", "speed": f"{cycle:g}",
                "spawnflags": "0", "phase": f"{phase:g}"}))
        self.platform(192, half_w, PAL_PILLAR)

    def sec_fork(self):
        """Trackmania-style split: the path forks into a narrow risky/fast
        lane (speed shards as a reward) and a wide safe lane, then both
        rejoin on a shared merge pad. Both lanes are asserted crossable."""
        self.sections.append(("fork", {}))
        self.platform(192, 220, PAL_GATE)        # fork pad: lanes diverge here
        base = list(self.pos)
        base_dir = self.dir
        d, r = self.dir, self.right()
        off = 280.0                              # lane centre-to-centre
        K, L, G = 4, 176.0, 170.0
        span = K * (G + L)
        speed = max(self.speed, RUN_SPEED + 50.0)  # carry momentum into the lanes

        def lane(sign, pal, lane_half, reward):
            self.pos = [base[0] + r[0] * sign * off,
                        base[1] + r[1] * sign * off, base[2]]
            self.dir = base_dir
            for _ in range(K):
                self.gap(G, speed)
                self.advance(0, side=self.rng.uniform(-24, 24))
                self.platform(L, lane_half, pal)
                if reward:
                    self.entities.append({
                        "classname": "item_armor_shard",
                        "origin": f"{self.pos[0] - d[0] * L / 2:g} "
                                  f"{self.pos[1] - d[1] * L / 2:g} "
                                  f"{self.pos[2] + 40:g}"})

        lane(+1, PAL_SLIDE, 72.0, True)          # right: narrow, fast, rewarded
        lane(-1, PAL_BHOP, 120.0, False)         # left: wide, safe
        # merge pad: spans both lanes (overlaps their ends) and rejoins the path
        self.pos = [base[0] + d[0] * (span - 40), base[1] + d[1] * (span - 40), base[2]]
        self.dir = base_dir
        merge_half = off + 120.0 + 64.0
        self.platform(220, merge_half, PAL_GATE)

    def sec_finish(self):
        self.sections.append(("finish gate", {}))
        self.platform(160, 160, PAL_FINISH, thick=32)
        d, r = self.dir, self.right()
        top = self.pos[2]
        # gate columns + crossbar
        for side in (-1, 1):
            cx = self.pos[0] + r[0] * side * 128
            cy = self.pos[1] + r[1] * side * 128
            self.solids.append(make_box(
                (cx - 24, cy - 24, top - 32), (cx + 24, cy + 24, top + 192),
                tex=TEX_WALL, palette=PAL_FINISH))
        (gx0, gy0), (gx1, gy1) = self.world_rect(48, 128)
        self.solids.append(make_box(
            (gx0, gy0, top + 192), (gx1, gy1, top + 224),
            tex=TEX_WALL, palette=PAL_FINISH))
        # the finish line and the lap teleporter share the arch
        fb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {
            "classname": "trigger_teleport",
            "target": "start_dest",
        }))
        self.platform(224, 160, PAL_FINISH, thick=32)
        if self.item_trail:
            # far high-value anchor: bots set a long-term goal at the END and
            # navigate the whole course toward it, sweeping the breadcrumbs en route
            self.entities.append({
                "classname": "item_health_mega",
                "origin": f"{self.pos[0]:g} {self.pos[1]:g} {self.pos[2] + 40:g}",
            })

    def sec_lift(self, deck):
        """Deck-to-deck lift: an arch that teleports a full DECK_RISE up.

        Towers climb away from the rising void — every deck you clear is
        a deck the void gets to keep.
        """
        self.sections.append(("lift gate", {"to_deck": deck + 2}))
        self.platform(160, 160, PAL_TOWER, thick=32)
        d, r = self.dir, self.right()
        top = self.pos[2]
        for side in (-1, 1):
            cx = self.pos[0] + r[0] * side * 128
            cy = self.pos[1] + r[1] * side * 128
            self.solids.append(make_box(
                (cx - 24, cy - 24, top - 32), (cx + 24, cy + 24, top + 192),
                tex=TEX_WALL, palette=PAL_TOWER))
        (gx0, gy0), (gx1, gy1) = self.world_rect(48, 128)
        self.solids.append(make_box(
            (gx0, gy0, top + 192), (gx1, gy1, top + 224),
            tex=TEX_WALL, palette=PAL_TOWER))
        tb = make_box((gx0, gy0, top), (gx1, gy1, top + 192),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {
            "classname": "trigger_teleport",
            "target": f"deck{deck}_dest",
        }))
        # the next deck starts directly past and above the gate
        self.advance(96, dz=DECK_RISE)
        dcx = self.pos[0] + d[0] * 192
        dcy = self.pos[1] + d[1] * 192
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": f"deck{deck}_dest",
            "origin": f"{dcx:g} {dcy:g} {self.pos[2] + 40:g}",
            "angle": str(self.yaw()),
        })
        self.platform(448, 224, PAL_START, thick=32)

    def maybe_turn(self):
        if self.rng.random() < 0.45:
            lr = self.rng.choice((-1, 1))
            self.gap(140 * self.scale, max(self.speed, RUN_SPEED + 30), dz=-8)
            d = self.dir
            half = 176.0                 # wider pad: room to carry a fast line
            cx = self.pos[0] + d[0] * half
            cy = self.pos[1] + d[1] * half
            top = self.pos[2]
            r = self.right()
            f = d
            o = lr                       # outer side of the turn (see turn(lr))
            # FLAT corner pad. The velodrome floor-bank read as "banked the wrong
            # way" and consistently cost flow (43% single-wall -> 35% banked ->
            # 22-27% on fresh seeds), so the carve now comes purely from bouncing
            # the two 45-degree walls — no tilted floor to fight.
            self.solids.append(make_box(
                (cx - half, cy - half, top - 24),
                (cx + half, cy + half, top),
                tex=TEX_FLOOR, palette=PAL_PLAIN))
            # TWO 45-DEGREE BOUNCE WALLS forming an outer chevron: ricochet off
            # the first (deflect ~45) then the second (complete the 90) — two
            # shallow revectors that each preserve air-strafe speed. Faces point
            # back at the line; the inner/exit quadrant stays open for bots.
            far  = (cx + f[0] * half, cy + f[1] * half)          # far-centre
            apex = (cx + r[0] * o * half, cy + r[1] * o * half)  # outer-mid
            back = (cx - f[0] * half, cy - f[1] * half)          # back-centre
            self.wall_seg(far,  apex, (cx, cy), top, height=320.0)
            self.wall_seg(apex, back, (cx, cy), top, height=320.0)
            self.pos[0], self.pos[1] = cx, cy
            self.turn(lr)
            # APEX GATE (combat recipe): a slice_drone right where you carve off
            # the wall — the kill lands at the revector apex and FEEDS the exit
            # (speed kick + refreshed air/bhop), so routing through the corner is
            # the fast line. enemy_offset ≈ corner − 0.7·jump per the design doc.
            if self.combat:
                self.place_drone(fwd=40.0, up=64.0)
            self.advance(half, dz=-16)   # exit a touch downhill: the turn donates speed
            self.sections.append(("bounce corner", {"dir": "left" if lr > 0 else "right"}))

    def build(self):
        self.sec_start()
        self.set_checkpoint()
        # dojo: a fixed straight recipe isolating one archetype
        if self.recipe:
            self.waypoints = []
            for secname in self.recipe:
                getattr(self, secname)()
                self.waypoints.append(tuple(self.pos))   # path point per section
                self.set_checkpoint()
            self.sec_finish()
            self.waypoints.append(tuple(self.pos))   # bait the finish line so
                                                     # bots chase across it -> completion
            self.build_checkpoint_catches()
            self.add_void_and_sky()
            self._dojo_items()      # bait bots down the course so they run it
            self.entities.insert(0, self._dojo_worldspawn())
            return self
        # THE RECIPE — the fun arc, validated by the bot dojo (every tier flows
        # at 50-58% with ~1200-2100ms stuck, the shipped-archetype band):
        #   1. OPENERS build speed (gaps, bhop) — both, every deck
        #   2. a FORK gives the trackmania route choice — the centrepiece
        #   3. FLOW sections keep the chain alive (slide, walls, slalom) — 2 of 3
        #   4. SPICE is the technical/lethal climax once you're fast
        #      (hurdles, movers, hazard) — 2 of 3
        #   5. the dj TOWER climbs out (toward the lift / finish)
        # tiers stay in arc order; the picks within shuffle for variety. ~8
        # sections/deck (was 5) — bigger, branchier, paced.
        openers = [self.sec_gaps, self.sec_bhop]
        flow = [self.sec_slide, self.sec_walls, self.sec_slalom]
        spice = [self.sec_hurdles, self.sec_movers, self.sec_hazard]
        # length > 1 builds a tower: each deck runs the arc, ending in a lift up
        # COMBAT runs WAY longer: repeat the flow+spice "movement" several times
        # (each its own teach→test cycle with enemy phrases) before the tower, so
        # a combat lap is one long sustained line, not a quick sprint.
        movements = 5 if self.combat else 1
        for deck in range(self.length):
            o = openers[:]
            self.rng.shuffle(o)
            mid = []
            for _ in range(movements):
                mid += list(self.rng.sample(flow, 2)) + list(self.rng.sample(spice, 2))
            deck_secs = o + [self.sec_fork] + mid + [self.sec_tower]
            # section enable/disable: drop any whose key (method suffix, e.g.
            # "hazard","movers","fork") isn't in the allowed set. Filtering AFTER
            # the shuffle/sample keeps the RNG stream — and thus identity — intact
            # when no filter is set. The cursor just advances over fewer sections.
            if self.mods.sections is not None:
                deck_secs = [s for s in deck_secs
                             if self.mods.enabled(s.__name__[4:])]
            for sec in deck_secs:
                sec()
                self.maybe_turn()
                # COMBAT recipe: the spice sections are the intensity beats, so
                # drop a 2-3 enemy phrase there (the "test"); openers + fork +
                # flow stay enemy-free as the rest beats that build/keep speed.
                if self.combat and sec in spice:
                    self.enemy_phrase()
                self.set_checkpoint()
            if deck < self.length - 1:
                self.sec_lift(deck)
                self.set_checkpoint()
        self.sec_finish()
        self.sections.append(("checkpoints", {"count": len(self.checkpoints)}))
        self.build_checkpoint_catches()
        self.add_void_and_sky()
        worldspawn = {
            "classname": "worldspawn",
            "message": f"STRAFE64 run {self.seed} "
                       f"(difficulty {self.diff}, length {self.length})",
            "music": pick_music(
                MUSIC_AMBIENT if not self.void else MUSIC_JUNGLE, self.seed),
        }
        if self.void:
            if self.voidrise is not None:
                rate = self.voidrise
            elif self.length > 1:
                rate = DECK_RISE / VOID_SECONDS_PER_DECK[self.diff]
            else:
                rate = VOID_FLAT_RATE[self.diff]
            delay = (self.voiddelay if self.voiddelay is not None
                     else VOID_DELAY[self.diff])
            worldspawn["voidbase"] = f"{self.void_floor:g}"
            worldspawn["voidrise"] = f"{rate:g}"
            worldspawn["voiddelay"] = f"{delay:g}"
        self.entities.insert(0, worldspawn)
        return self

    def _dojo_place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def _dojo_items(self):
        # DM bots roam toward items/enemies, not race finishes — so a bare
        # course gives them no reason to move. Drop a pickup on each section's
        # platform: the bot AI paths to the next item and runs the course as a
        # side effect. Weapons/armor are the strongest bot magnets.
        bait = ["weapon_railgun", "item_armor_combat", "item_health_large",
                "item_armor_body", "item_health"]
        for i, (x, y, z) in enumerate(self.waypoints):
            self._dojo_place(bait[i % len(bait)], x, y, z + 24)

    def _dojo_worldspawn(self):
        ws = {
            "classname": "worldspawn",
            "message": "STRAFE64 dojo: " + "/".join(self.recipe),
        }
        if self.void:
            rate = (self.voidrise if self.voidrise is not None
                    else VOID_FLAT_RATE[self.diff])
            delay = (self.voiddelay if self.voiddelay is not None
                     else VOID_DELAY[self.diff])
            ws["voidbase"] = f"{self.void_floor:g}"
            ws["voidrise"] = f"{rate:g}"
            ws["voiddelay"] = f"{delay:g}"
        return ws

    # ---- checkpoints ---------------------------------------------------
    def set_checkpoint(self):
        """Drop a respawn point + visible pad at the current cursor.

        Falls over the geometry built *after* this call are caught and
        teleported back here (build_checkpoint_catches), so a missed jump
        costs the player the current section, not the whole run. The void
        still rises, so checkpoints forgive execution but never the clock.
        """
        d = self.dir
        name = f"cp{len(self.checkpoints)}"
        # a safe standing spot pulled back from the platform's front edge
        bx = self.pos[0] - d[0] * 64
        by = self.pos[1] - d[1] * 64
        bz = self.pos[2]
        self.entities.append({
            "classname": "misc_teleporter_dest",
            "targetname": name,
            "origin": f"{bx:g} {by:g} {bz + 48:g}",
            "angle": str(self.yaw()),
        })
        # glowing checkpoint pad, flush on the platform
        self.solids.append(make_box(
            (bx - 48, by - 48, bz - 2), (bx + 48, by + 48, bz + 4),
            tex=TEX_FLOOR, palette=PAL_CHECK))
        if self.item_trail:
            # breadcrumb pickup: goal-seeking AAS bots mill at spawn with nothing
            # to chase, so a trail of respawning items pulls them down the course
            # (a bot test bed where they actually run the path). On a solid pad,
            # so always reachable.
            self.entities.append({
                "classname": "item_armor_shard",
                "origin": f"{bx:g} {by:g} {bz + 40:g}",
            })
        # idx marks where this section's geometry begins (the pad above is
        # tiny and belongs to the previous segment)
        self.checkpoints.append({"name": name, "idx": len(self.solids)})

    def build_checkpoint_catches(self):
        """One rescue slab per section, under its footprint, → its entrance.

        Sized to the bounding box of the solids built between consecutive
        checkpoints; a short drop below the section's lowest floor so a
        fall reads as a quick reset rather than a long plummet.
        """
        cps = self.checkpoints
        for i, cp in enumerate(cps):
            lo = cp["idx"]
            hi = cps[i + 1]["idx"] if i + 1 < len(cps) else len(self.solids)
            seg = self.solids[lo:hi]
            if not seg:
                continue
            xs = [b.mins[0] for b in seg] + [b.maxs[0] for b in seg]
            ys = [b.mins[1] for b in seg] + [b.maxs[1] for b in seg]
            zmin = min(b.mins[2] for b in seg)
            m = 96.0
            cb = make_box(
                (min(xs) - m, min(ys) - m, zmin - 360),
                (max(xs) + m, max(ys) + m, zmin - 200),
                tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
            self.triggers.append((cb, {
                "classname": "trigger_teleport",
                "spawnflags": "2",          # RESCUE: set the faller down, don't fling
                "target": cp["name"],
            }))

    def add_void_and_sky(self):
        xs = [b.mins[0] for b in self.solids] + [b.maxs[0] for b in self.solids]
        ys = [b.mins[1] for b in self.solids] + [b.maxs[1] for b in self.solids]
        zs = [b.mins[2] for b in self.solids] + [b.maxs[2] for b in self.solids]
        m = 768.0
        x0, x1 = min(xs) - m, max(xs) + m
        y0, y1 = min(ys) - m, max(ys) + m
        z0, z1 = min(zs) - 896.0, max(zs) + 1024.0
        # the rising kill plane starts at the bottom of the pit, below the
        # rescue teleporter at z0+256 — early falls are forgiven, then the
        # void overtakes the rescue plane and falls become fatal
        self.void_floor = z0 + 64.0
        # falling into the void teleports you home — keeps the run flowing
        vb = make_box((x0 + 8, y0 + 8, z0 + 256), (x1 - 8, y1 - 8, z0 + 320),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((vb, {
            "classname": "trigger_teleport",
            "spawnflags": "2",              # RESCUE: set the faller down, don't fling
            "target": "start_dest",
        }))
        self.solids += make_skybox(x0, y0, z0, x1, y1, z1)


