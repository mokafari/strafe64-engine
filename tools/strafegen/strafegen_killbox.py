"""strafegen_killbox — the vertical melee Killbox arena (momentum portals)."""
import math
import random

import strafegen_gfx as gfx
import strafegen_config as cfg
from strafegen_geom import *
from strafegen_palettes import *
from strafegen_textures import *


class Killbox:
    """The hack-and-slash killbox — a sealed neon box you fight UP through.

    A dark deck, a perimeter catwalk reached by ramp or jump-pad, four magenta
    wall-jump columns chimneying up the corners, and a central spire crowned
    with the quad. Four wall-centre MOMENTUM PORTALS turn every wall into a
    runway: they're paired teleporters whose destinations carry
    `angles "1000000 0 0"`, which makes TeleportPlayer (g_misc.c) skip its
    velocity reset — so you dive at one wall fast and pop out the far wall still
    flying, and the box never dead-ends. Built for the sword / time-bind game:
    speed is power, verticality is the map, the deck is just where you start.

    Bot-playable: ramps and jump pads give AI the catwalk; the columns and
    portals are the human fast lines (bots ignore the portals, like the
    velodrome's race triggers).
    """

    def __init__(self, seed, difficulty, archetype=None, mods=None):
        self.rng = random.Random(seed ^ 0x4B11B0)
        self.seed = seed
        self.diff = difficulty
        # RECIPE hook: force a centerpiece archetype (spire/spiral/forest/ring/
        # cross/twin) instead of letting the seed pick — lets recipes select and
        # blend deterministically. None = seed-random (original behaviour).
        self.archetype = archetype
        # geometry modifiers (default identity → byte-identical output)
        self.mods = mods or cfg.GenMods()
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    def rim(self, x0, y0, x1, y1, z, pal=PAL_KB_NEON, t=10.0, h=8.0):
        """Outline a top's perimeter with four thin proud neon strips, so every
        ledge and drop reads as a bright edge at speed (N64 clarity)."""
        self.solids.append(make_box((x0, y0, z), (x1, y0 + t, z + h), palette=pal))
        self.solids.append(make_box((x0, y1 - t, z), (x1, y1, z + h), palette=pal))
        self.solids.append(make_box((x0, y0, z), (x0 + t, y1, z + h), palette=pal))
        self.solids.append(make_box((x1 - t, y0, z), (x1, y1, z + h), palette=pal))

    def _hull(self, fallback=TEX_WALL):
        """GFX: route a structural mass to the machined-metal hull material
        (opaque PBR-lite — pure draw swap, collision unchanged). The neon rim
        caps stay as the wall-jump/edge cue. Falls back to the dev grid for
        --no-gfx (where the hull shader isn't shipped)."""
        return gfx.TEX_HULL if cfg.GFX else fallback

    def column(self, cx, cy, half, top, pal=PAL_KB_COLUMN):
        """A wall-jump column / cover pillar with a glowing neon cap. The shaft
        is machined hull metal (GFX); the neon rim cap keeps marking it as a
        wall-jump surface."""
        self.solids.append(make_box((cx - half, cy - half, 0), (cx + half, cy + half, top),
                                    tex=self._hull(), palette=pal))
        self.rim(cx - half, cy - half, cx + half, cy + half, top)

    # --- centerpiece ARCHETYPES ------------------------------------------
    # The seed picks one of these so killboxes differ in SILHOUETTE, not just
    # parameters. Each owns the central structure + its wall-jump geometry +
    # the quad, and records footprints in `blk` so spawns nudge clear of them.

    def _cp_spire(self, rng, ci, blk):
        """Solid stepped pyramid + 4 corner columns + 4 cover pillars (classic)."""
        self.sections.append(("centerpiece", {"kind": "spire"}))
        self.solids.append(make_box((-320, -320, 0), (320, 320, 288),
                                    tex=self._hull(TEX_FLOOR), palette=PAL_KB_WALL))
        self.rim(-320, -320, 320, 320, 288)
        self.solids.append(make_box((-224, -224, 288), (224, 224, 560),
                                    tex=self._hull(TEX_FLOOR), palette=PAL_KB_WALL))
        self.rim(-224, -224, 224, 224, 560)
        self.solids.append(make_box((-128, -128, 560), (128, 128, 700), palette=PAL_KB_NEON))
        self.place("item_quad", 0, 0, 724)
        co = ci - self.col_in
        for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            self.column(sx * co, sy * co, 88, 760); blk.append((sx * co, sy * co, 88))
        # (iter: removed the 4 mid-deck cover pillars — same clearing that helped
        #  the ring; spire was the most cluttered archetype, highest stuck. Corner
        #  columns + the stepped spire still provide cover.)

    def _cp_spiral(self, rng, ci, blk):
        """A helix of ascending slabs winding up to the quad — climb fighting.
        No solid core: the spiral IS the tower, so the volume reads totally
        different from the pyramid."""
        self.sections.append(("centerpiece", {"kind": "spiral tower"}))
        n = rng.randint(9, 12)
        rad = rng.choice((360.0, 440.0))
        a0 = rng.uniform(0, 2 * math.pi)
        turns = rng.choice((1.5, 2.0))
        half = 150.0
        last = (0.0, 0.0, 0.0)
        for k in range(n):
            a = a0 + turns * 2 * math.pi * k / (n - 1)
            px, py = rad * math.cos(a), rad * math.sin(a)
            pz = 120.0 + (900.0 - 120.0) * k / (n - 1)
            self.solids.append(make_box((px - half, py - half, pz),
                                        (px + half, py + half, pz + 24), palette=PAL_LEDGE))
            self.rim(px - half, py - half, px + half, py + half, pz + 24, t=8.0, h=6.0)
            last = (px, py, pz)
        self.place("item_quad", last[0], last[1], last[2] + 44)
        # short ground columns give launch points up into the helix
        for sx, sy in ((1, 1), (-1, -1)):
            self.column(sx * 640, sy * 640, 80, 420); blk.append((sx * 640, sy * 640, 80))

    def _cp_forest(self, rng, ci, blk):
        """A scatter of varied-height wall-jump columns — a pillar forest you
        chimney between, quad on the tallest. No central structure at all."""
        self.sections.append(("centerpiece", {"kind": "pillar forest"}))
        span = ci - 360.0
        placed = []
        tallest = None
        for _ in range(rng.randint(7, 10)):
            for _try in range(40):
                px, py = rng.uniform(-span, span), rng.uniform(-span, span)
                if math.hypot(px, py) < 200:            # keep a central pit open
                    continue
                half = float(rng.choice((72, 88, 104)))
                if all(abs(px - qx) > half + qh + 96 or abs(py - qy) > half + qh + 96
                       for (qx, qy, qh) in placed):
                    break
            else:
                continue
            top = float(rng.randrange(300, 861, 40))
            self.column(px, py, half, top); blk.append((px, py, half))
            placed.append((px, py, half))
            if tallest is None or top > tallest[2]:
                tallest = (px, py, top)
        if tallest:
            self.place("item_quad", tallest[0], tallest[1], tallest[2] + 44)
        else:
            self.place("item_quad", 0, 0, 320)

    def _cp_ring(self, rng, ci, blk):
        """A hollow colonnade ring around a central void with the quad floating
        in its heart — fight around and dive THROUGH the gaps."""
        self.sections.append(("centerpiece", {"kind": "hollow ring"}))
        R = rng.choice((360.0, 430.0))
        h = float(rng.randrange(420, 581, 40))
        th = 72.0
        skip = rng.randint(0, 7)                        # rotate which gaps open
        for k in range(8):
            if (k + skip) % 2 == 0:                     # alternate posts → 4 gaps
                continue
            a = k * 2 * math.pi / 8
            px, py = R * math.cos(a), R * math.sin(a)
            self.solids.append(make_box((px - th, py - th, 0), (px + th, py + th, h),
                                        tex=TEX_WALL, palette=PAL_KB_WALL))
            self.rim(px - th, py - th, px + th, py + th, h)
            blk.append((px, py, th))
        self.place("item_quad", 0, 0, h * 0.5)          # floats in the ring's heart
        # (iter: removed the 2 outer cover columns at (+-820,0) — testing whether
        # clearing free-standing deck obstacles cuts bot stuck; the ring posts +
        # wall-jump columns already provide cover)

    def _cp_cross(self, rng, ci, blk):
        """Two crossing wall slabs (a +) splitting the deck into quadrants, a
        tower at the intersection crowned with the quad. Fight around the arms,
        dive over them — a bladed sightline-breaker, no central pyramid."""
        self.sections.append(("centerpiece", {"kind": "cross"}))
        arm = rng.choice((520.0, 640.0))
        th = 80.0
        h = float(rng.randrange(360, 481, 40))
        self.solids.append(make_box((-arm, -th, 0), (arm, th, h), tex=TEX_WALL, palette=PAL_KB_WALL))
        self.rim(-arm, -th, arm, th, h)
        self.solids.append(make_box((-th, -arm, 0), (th, arm, h), tex=TEX_WALL, palette=PAL_KB_WALL))
        self.rim(-th, -arm, th, arm, h)
        self.solids.append(make_box((-150, -150, h), (150, 150, h + 150), palette=PAL_KB_NEON))
        self.place("item_quad", 0, 0, h + 194)
        blk.append((0, 0, 150))
        for bx, by in ((arm, 0), (-arm, 0), (0, arm), (0, -arm)):
            blk.append((bx, by, th))

    def _cp_twin(self, rng, ci, blk):
        """Two offset tower stacks linked by a high bridge with the quad on it —
        a portal-linked twin-peak feel inside one box. Wall-jump up either tower,
        meet in the middle."""
        self.sections.append(("centerpiece", {"kind": "twin towers"}))
        off = rng.choice((560.0, 680.0))
        axis = rng.random() < 0.5                       # towers N/S or E/W
        half = 200.0
        h1, h2 = 580.0, 460.0
        pts = ([(off, 0, h1), (-off, 0, h2)] if axis
               else [(0, off, h1), (0, -off, h2)])
        for px, py, h in pts:
            self.solids.append(make_box((px - half, py - half, 0), (px + half, py + half, h),
                                        tex=TEX_WALL, palette=PAL_KB_WALL))
            self.rim(px - half, py - half, px + half, py + half, h)
            blk.append((px, py, half))
        bz = min(h1, h2)                                # bridge links the tops
        if axis:
            self.solids.append(make_box((-off, -96, bz), (off, 96, bz + 24), palette=PAL_LEDGE))
        else:
            self.solids.append(make_box((-96, -off, bz), (96, off, bz + 24), palette=PAL_LEDGE))
        self.place("item_quad", 0, 0, bz + 68)
        for s in (1, -1):                               # flanking cover columns
            cx, cy = (0, s * 820) if axis else (s * 820, 0)
            self.column(cx, cy, 80, 440); blk.append((cx, cy, 80))

    def _cp_court(self, rng, ci, blk):
        """COURT — purpose-built from the tuning campaign. A central low DAIS holds
        the quad as a reachable, contested objective (single-jump up, z64 — the
        floating/high quad never got contested), ringed by SIX wall-jump columns
        spaced to the research wall-jump band (~640u arc, varied height) for deck
        cover + verticality WITHOUT the clutter that spiked stuck on the spire. It
        fuses the campaign's winners: forest's fight-around cover, a ring/twin-style
        central focal point, all on the dialed shared shell (central spawns etc.)."""
        self.sections.append(("centerpiece", {"kind": "court"}))
        # central dais: low + bot-reachable (z64 = single-jump ledge) so the quad
        # is actually fought over; two-tone rim reads it as the arena's heart
        dr, dh = 300.0, 64.0
        self.solids.append(make_box((-dr, -dr, 0), (dr, dr, dh), palette=PAL_KB_DECK))
        self.rim(-dr, -dr, dr, dr, dh, t=14.0, h=10.0)
        self.solids.append(make_box((-120, -120, dh), (120, 120, dh + 14),
                                    palette=PAL_KB_NEON))       # quad pad, glows
        self.place("item_quad", 0, 0, dh + 58)
        blk.append((0, 0, dr))
        # ring of 6 wall-jump columns, evenly spaced (~640u arc) + a launch shard
        a0 = rng.uniform(0, 2 * math.pi)
        for k in range(6):
            a = a0 + k * math.pi / 3
            px, py = 640.0 * math.cos(a), 640.0 * math.sin(a)
            self.column(px, py, 84, float(rng.randrange(360, 621, 40)))
            blk.append((px, py, 84))

    def build(self):
        rng = self.rng
        # BIG: sized for 16 players. Everything downstream is relative to W/ci,
        # so portals and wall-jumps stay correct as it scales.
        W = 1408.0           # half-extent of the inner box (2816 wide)
        T = 64.0             # wall thickness
        H = 1600.0           # tall: the play is vertical
        CW = float(rng.randrange(352, 433, 16))      # catwalk height (seeded)
        CWW = 256.0                                   # catwalk depth (inward)
        col_in = float(rng.randrange(280, 361, 20))  # corner-column inset
        ci = W               # inner wall face (interior spans [-W, W])
        self.sections.append(("killbox", {"size": int(2 * W), "height": int(H)}))

        # --- deck + four containment walls, neon baseboard ---------------
        self.solids.append(make_box((-W, -W, -32), (W, W, 0), palette=PAL_KB_DECK))
        self.solids.append(make_box((-W - T, W, 0), (W + T, W + T, H),
                                    tex=TEX_WALL, palette=PAL_KB_WALL))      # N
        self.solids.append(make_box((-W - T, -W - T, 0), (W + T, -W, H),
                                    tex=TEX_WALL, palette=PAL_KB_WALL))      # S
        self.solids.append(make_box((W, -W, 0), (W + T, W, H),
                                    tex=TEX_WALL, palette=PAL_KB_WALL))      # E
        self.solids.append(make_box((-W - T, -W, 0), (-W, W, H),
                                    tex=TEX_WALL, palette=PAL_KB_WALL))      # W
        self.rim(-W, -W, W, W, 0, t=14.0, h=10.0)    # deck boundary line
        # vertical neon strips up the four corners — reads the box volume
        for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            self.solids.append(make_box(
                (sx * ci - (14 if sx > 0 else 0), sy * ci - (14 if sy > 0 else 0), 0),
                (sx * ci + (0 if sx > 0 else 14), sy * ci + (0 if sy > 0 else 14), H - 32),
                palette=PAL_KB_NEON))

        # --- perimeter catwalk (Z tier), every edge neon-rimmed ----------
        self.sections.append(("catwalk", {"z": int(CW)}))
        strips = (((-ci, ci - CWW), (ci, ci)),          # N
                  ((-ci, -ci), (ci, -ci + CWW)),        # S
                  ((ci - CWW, -ci), (ci, ci)),          # E
                  ((-ci, -ci), (-ci + CWW, ci)))        # W
        for (a, b) in strips:
            self.solids.append(make_box((a[0], a[1], CW - 28), (b[0], b[1], CW),
                                        palette=PAL_KB_DECK))
            self.rim(a[0], a[1], b[0], b[1], CW)

        # --- two ramps deck -> catwalk (bot-walkable, ~28 deg). (Tried FOUR for
        #     route redundancy — REVERTED: it spiked sword stuck 3024->8570 and cut
        #     frags; the extra deck ramps clutter the floor and trap bots.) -------
        self.sections.append(("ramps", {"count": 2}))
        RUN = CW * 2.4
        sy0 = -ci + CWW
        self.solids.append(make_box((400, sy0, -16), (640, sy0 + RUN, CW),
                                    palette=PAL_KB_DECK, top_drop={6: CW, 7: CW}))
        self.rim(400, sy0, 640, sy0 + RUN, CW - 2, h=6.0)
        ny1 = ci - CWW
        self.solids.append(make_box((-640, ny1 - RUN, -16), (-400, ny1, CW),
                                    palette=PAL_KB_DECK, top_drop={4: CW, 5: CW}))
        self.rim(-640, ny1 - RUN, -400, ny1, CW - 2, h=6.0)

        # --- centerpiece: the seed picks a structural ARCHETYPE so killboxes
        #     vary in SILHOUETTE (solid spire / winding spiral tower / pillar
        #     forest / hollow ring) instead of being one template reskinned per
        #     seed. Each archetype owns its central structure + wall-jump geometry
        #     + the quad, and fills spawn_blockers so deck spawns nudge clear. ---
        self.col_in = col_in
        spawn_blockers = []
        # "court" is the campaign-distilled archetype: explicit-only (via the
        # archetype hook / --arch), kept OUT of the random rotation so existing
        # seed->archetype mappings stay stable.
        arch = self.archetype or rng.choice(
            ("spire", "spiral", "forest", "ring", "cross", "twin"))
        {"spire": self._cp_spire, "spiral": self._cp_spiral, "court": self._cp_court,
         "forest": self._cp_forest, "ring": self._cp_ring,
         "cross": self._cp_cross, "twin": self._cp_twin}[arch](rng, ci, spawn_blockers)

        # --- AERIAL-BLADE slabs: a ring of floating platforms at mid-height that
        #     add vertical play + cover. KEPT on a 5-min A/B (vectorgun frags +40%,
        #     sword neutral, stuck within noise; the earlier 40s "regression" was
        #     noise). Doesn't yet make SWORD duels airborne (4->7% midair — bots
        #     won't path up without a reachable route); a pad/ramp ONTO a slab is the
        #     follow-up. Spiral already IS a slab helix (skip); else skip any slab
        #     that would clip the centerpiece (footprints in spawn_blockers). ---
        if arch != "spiral":
            self.sections.append(("aerial slabs", {"count": 6}))
            ar, ah, ph = 720.0, 300.0, 120.0
            n_slab = 0
            for k in range(6):
                a = math.radians(30) + k * math.pi / 3
                px, py = ar * math.cos(a), ar * math.sin(a)
                if any(abs(px - bx) < ph + bh and abs(py - by) < ph + bh
                       for (bx, by, bh) in spawn_blockers):
                    continue
                self.solids.append(make_box((px - ph, py - ph, ah),
                                            (px + ph, py + ph, ah + 16), palette=PAL_LEDGE))
                self.rim(px - ph, py - ph, px + ph, py + ph, ah + 16, t=8.0, h=6.0)
                if n_slab % 2 == 0:
                    self.place("item_armor_shard", px, py, ah + 40)
                n_slab += 1

        # --- jump pads (8): a single ring, OFFSET 22.5 deg off the cardinal axes
        #     so a pad never sits under a wall momentum-portal dest (they share the
        #     cardinals), and each throws you TANGENTIALLY (~clockwise) onto the
        #     catwalk. That sets up a CIRCULATION CURRENT — pad-hops chain into a lap
        #     around the ring — instead of every pad dumping you into the middle. --
        self.sections.append(("jump pads", {"count": 8, "flow": "clockwise"}))
        pad_r = ci - CWW - 150                       # 1002: just inboard of catwalk
        for k in range(8):
            a = math.radians(22.5) + k * math.pi / 4
            px, py = pad_r * math.cos(a), pad_r * math.sin(a)
            self.solids.append(make_box((px - 52, py - 52, 0), (px + 52, py + 52, 10),
                                        palette=PAL_PAD))
            spawn_blockers.append((px, py, 52))      # don't spawn on a launch pad
            self.rim(px - 52, py - 52, px + 52, py + 52, 10, t=8.0, h=8.0)
            tb = make_box((px - 52, py - 52, 10), (px + 52, py + 52, 84),
                          tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
            self.triggers.append((tb, {"classname": "trigger_push",
                                       "target": f"kb_pad_{k}"}))
            # tangential launch: aim ~70 deg further around the ring so the toss
            # carries you along the catwalk (clockwise), building the lap
            ta = a + math.radians(70)
            tr = ci - CWW * 0.5
            self.place("target_position", tr * math.cos(ta), tr * math.sin(ta),
                       CW + 200, targetname=f"kb_pad_{k}")

        # --- momentum portals: one centred gate per wall, exit the opposite
        #     wall keeping your velocity (dest angles[0]>999999 -> no reset) -
        self.sections.append(("momentum portals", {"count": 4}))
        band = 256.0
        inset = 120.0
        pz0, pz1 = CW - 40.0, CW + 360.0
        dest_in = ci - 360.0
        for k, (nx, ny) in enumerate(((0, 1), (0, -1), (1, 0), (-1, 0))):
            if ny:                                  # north/south wall
                wy = ny * ci
                lo, hi = sorted((wy, wy - ny * inset))
                tb = make_box((-band, lo, pz0), (band, hi, pz1),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
                dx, dy = 0.0, -ny * dest_in
                fy0, fy1 = sorted((wy, wy - ny * 12))
                for z0, z1 in ((pz0 - 16, pz0), (pz1, pz1 + 16)):
                    self.solids.append(make_box((-band - 16, fy0, z0),
                                                (band + 16, fy1, z1),
                                                tex=TEX_WALL, palette=PAL_KB_NEON))
                for bx in (-band - 16, band):       # side jambs
                    self.solids.append(make_box((bx, fy0, pz0), (bx + 16, fy1, pz1),
                                                tex=TEX_WALL, palette=PAL_KB_NEON))
            else:                                   # east/west wall
                wx = nx * ci
                lo, hi = sorted((wx, wx - nx * inset))
                tb = make_box((lo, -band, pz0), (hi, band, pz1),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
                dx, dy = -nx * dest_in, 0.0
                fx0, fx1 = sorted((wx, wx - nx * 12))
                for z0, z1 in ((pz0 - 16, pz0), (pz1, pz1 + 16)):
                    self.solids.append(make_box((fx0, -band - 16, z0),
                                                (fx1, band + 16, z1),
                                                tex=TEX_WALL, palette=PAL_KB_NEON))
                for by in (-band - 16, band):
                    self.solids.append(make_box((fx0, by, pz0), (fx1, by + 16, pz1),
                                                tex=TEX_WALL, palette=PAL_KB_NEON))
            dname = f"kb_portal_{k}"
            self.triggers.append((tb, {"classname": "trigger_teleport", "target": dname}))
            self.place("misc_teleporter_dest", dx, dy, CW + 120,
                       targetname=dname, angles="1000000 0 0")

        # --- 16 spawns: an outer + inner deck ring + four on the catwalk.
        #     the outer ring is offset 22.5 deg so it sits BETWEEN the jump
        #     pads (spawning on a pad would launch you the instant you appear) --
        for k in range(8):
            a = math.pi / 8 + k * math.pi / 4
            x, y = 760 * math.cos(a), 760 * math.sin(a)   # pulled in (was 880): tighter
            x, y = avoid_footprints(x, y, spawn_blockers, clearance=40.0, ring_r=760)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            x, y = 500 * math.cos(a), 500 * math.sin(a)   # pulled in (was 560)
            # inner ring sits on the 45 deg diagonals — exactly where the corner
            # wall-jump columns are. Nudge clear so nobody spawns inside one.
            x, y = avoid_footprints(x, y, spawn_blockers, clearance=40.0, ring_r=500)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        # (iter: the 4 catwalk spawns moved DOWN to the central deck so all 16 bots
        #  start in the melee zone — densify sword engagement. Cardinals at r640,
        #  clear of the inner diagonal + outer 22.5-offset rings.)
        for k in range(4):
            a = k * math.pi / 2
            x, y = 640 * math.cos(a), 640 * math.sin(a)
            x, y = avoid_footprints(x, y, spawn_blockers, clearance=40.0, ring_r=640)
            yaw = math.degrees(math.atan2(-y, -x)) % 360
            self.place("info_player_deathmatch", x, y, 40, angle=f"{yaw:.0f}")
        self.place("info_player_intermission", 0, 0, H - 240, angle="270")

        # --- item economy: a ROTATION laid on the clockwise pad circulation,
        #     not a uniform scatter. The QUAD crowns the centerpiece (the prize,
        #     climb to it). Two POLES — mega health + heavy armor — sit at OPPOSITE
        #     pad-landing zones on the catwalk loop, so owning the arena means
        #     timing both ends of the lap. A SHARD TRAIL marks the other landings
        #     to bait the circulation, and deck health feeds the floor fight. ---
        self.sections.append(("loadout", {"economy": "rotation"}))

        # CLEARANCE RULE: never drop a pickup on/next to a player spawn (you'd
        # spawn standing on an item, or grab a free pole on respawn). Collect the
        # spawns at each height band; place_item nudges a pickup off any spawn at
        # its OWN height (a deck spawn doesn't block a catwalk item above it).
        spawn_pts = []
        for e in self.entities:
            if e.get("classname") == "info_player_deathmatch":
                sx, sy, sz = (float(v) for v in e["origin"].split())
                spawn_pts.append((sx, sy, sz))

        def place_item(cls, x, y, z):
            near = [(sx, sy, 96.0) for (sx, sy, sz) in spawn_pts if abs(sz - z) < 120]
            nx, ny = avoid_footprints(x, y, near, clearance=64.0)  # >=160u off a spawn
            self.place(cls, nx, ny, z)

        land_r = ci - CWW * 0.5                       # where the tangential pads land
        for k in range(8):
            ta = math.radians(22.5) + k * math.pi / 4 + math.radians(70)
            lx, ly = land_r * math.cos(ta), land_r * math.sin(ta)
            if k == 0:
                place_item("item_health_mega", lx, ly, CW + 40)     # pole A
            elif k == 4:
                place_item("item_armor_body", lx, ly, CW + 40)      # pole B (opposite)
            elif k % 2 == 1:
                place_item("item_armor_shard", lx, ly, CW + 40)     # trail
            else:
                place_item("item_health", lx, ly, CW + 40)          # trail
        # deck health on the diagonals (between centerpiece and walls): the floor
        # fight's sustain, off the cardinal portal axes and inboard of the columns
        for k in range(4):
            a = math.pi / 4 + k * math.pi / 2
            place_item("item_health_large", 760 * math.cos(a), 760 * math.sin(a), 40)
        # vectorgun/kit modes: a rail + rockets reachable on opposite axes (sword
        # mode ignores them); ammo just outboard. Sparser than the old 4-gun ring.
        place_item("weapon_railgun", 600, 0, 56)
        place_item("ammo_slugs", 664, 0, 40)
        place_item("weapon_rocketlauncher", -600, 0, 56)
        place_item("ammo_rockets", -664, 0, 40)

        # --- enclosure + worldspawn (ceiling on the wall top, no void) ----
        m = 64.0
        self.solids += make_skybox(-W - T - m, -W - T - m, -32 - m,
                                   W + T + m, W + T + m, H)
        self.entities.insert(0, {
            "classname": "worldspawn",
            "message": f"STRAFE64 killbox {self.seed} (difficulty {self.diff})",
            "music": pick_music(MUSIC_BREAKCORE, self.seed),
        })
        return self


