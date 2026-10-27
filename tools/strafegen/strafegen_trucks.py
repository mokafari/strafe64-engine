"""strafegen_trucks — the Clustertruck treadmill (endless, ramping)."""
import random

from strafegen_geom import *
from strafegen_palettes import *
from strafegen_textures import *


class Trucks:
    """Endless Clustertruck run on a static BSP.

    A fleet of func_truck movers drives down the road toward the start pad
    (-x) and the game recycles each one to the far end once it passes killx
    (g_mover.c SP_func_truck), so the stream never ends. The runner hops
    onto the oncoming fleet and runs upstream across the roofs; anything that
    drops off a truck falls into the void kill-pit. The game owns the run:
    speed, lateral spread and enemy riders all ramp per STAGE (g_truckRamp
    seconds each), and a death resets the whole fleet to stage 1.

    Every truck is its own multi-brush inline submodel parked at the origin
    and built from an ARCHETYPE that asks for one part of the moveset —
    mantle/double-jump onto a box trailer, launch off a ramp, wall-run a floor
    gap, slide under a bar, hop a hurdle. Each truck also tells the game
    where an enemy can stand on it ("ridex"/"ridez").
    """

    ROAD_LEN = 6144.0
    KILL_X = -256.0
    BASE_TOP = 112.0        # flatbed roof; start pad is flush with it

    # archetype weights per difficulty (easy leans on flatbeds)
    # The daily-course sections ported onto trailers (slalom, hurdles, tower,
    # fork, tunnel, bhop pads) sit beside the original six; "long" kinds are
    # built on 800-1000u trailers so a bhopping runner gets a real stretch.
    WEIGHTS = (
        {"flatbed": 3, "trailer": 2, "ramp": 2, "walls": 1, "bar": 1, "hurdle": 1,
         "slalom": 2, "hurdles": 2, "tower": 1, "fork": 1, "tunnel": 1, "bhop": 1},
        {"flatbed": 1, "trailer": 2, "ramp": 2, "walls": 2, "bar": 1, "hurdle": 1,
         "slalom": 2, "hurdles": 2, "tower": 2, "fork": 2, "tunnel": 2, "bhop": 2},
        {"flatbed": 1, "trailer": 2, "ramp": 2, "walls": 3, "bar": 1, "hurdle": 1,
         "slalom": 2, "hurdles": 2, "tower": 2, "fork": 2, "tunnel": 2, "bhop": 3},
    )
    LONG = {"slalom", "hurdles", "fork", "tunnel", "bhop"}

    def __init__(self, seed, difficulty, fleet=None):
        self.rng = random.Random(seed ^ 0x7C0C)
        self.seed = seed
        self.diff = difficulty
        self.fleet = fleet or (16, 15, 14)[difficulty]   # fewer trucks = wider gaps
        self.speed = (260, 320, 380)[difficulty]
        self.solids = []
        self.triggers = []
        self.movers = []
        self.entities = []
        self.sections = []

    def place(self, classname, x, y, z, **kw):
        e = {"classname": classname, "origin": f"{x:g} {y:g} {z:g}"}
        e.update({k: str(v) for k, v in kw.items()})
        self.entities.append(e)

    # --- truck archetypes ------------------------------------------------
    # Local coords: x runs along the road, the truck DRIVES toward -x, so the
    # runner boards at the -x nose and runs out the +x tail. Each returns
    # (parts, ride_x, ride_z): the brushes and where a rider stands.

    def _deck(self, L, W, top, pal=PAL_PILLAR):
        return make_box((-L / 2, -W, 0), (L / 2, W, top), palette=pal)

    def truck_flatbed(self, L, W):
        top = self.BASE_TOP - self.rng.choice((0, 16, 32))
        return [self._deck(L, W, top, PAL_PAD)], L / 4, top

    def truck_trailer(self, L, W):
        """Low cab at the nose, tall box behind it: board the cab, then
        double-jump / mantle onto the box roof."""
        cab = 112.0
        box = cab + self.rng.choice((56, 72, 80))   # <= 88u double-jump ledge
        split = -L / 2 + 112
        return [make_box((-L / 2, -W, 0), (split, W, cab), palette=PAL_PILLAR),
                make_box((split, -W, 0), (L / 2, W, box), palette=PAL_WALLS)], L / 4, box

    def truck_ramp(self, L, W):
        """Flatbed whose back half is a kicker wedge rising toward the tail —
        run it at speed and launch to the next truck."""
        top = self.BASE_TOP
        rise = self.rng.choice((64, 96))
        mid = 0.0
        deck = make_box((-L / 2, -W, 0), (mid, W, top), palette=PAL_PILLAR)
        wedge = make_box((mid, -W, 0), (L / 2, W, top + rise), palette=PAL_GATE,
                         top_drop={4: rise, 6: rise})
        return [deck, wedge], -L / 4, top

    def truck_walls(self, L, W):
        """Two tall side walls with a gap in the floor between short end
        decks: wall-run or wall-jump across, or drop into the void."""
        top = self.BASE_TOP
        wall_h = 160.0
        gap = self.rng.choice((128, 160, 192))
        end = (L - gap) / 2
        t = 16.0
        Wx = W + 32
        return [make_box((-L / 2, -W, 0), (-L / 2 + end, W, top), palette=PAL_PILLAR),
                make_box((L / 2 - end, -W, 0), (L / 2, W, top), palette=PAL_PILLAR),
                make_box((-L / 2, -Wx, 0), (L / 2, -Wx + t, top + wall_h), palette=PAL_WALLS),
                make_box((-L / 2, Wx - t, 0), (L / 2, Wx, top + wall_h), palette=PAL_WALLS),
                ], L / 2 - end / 2, top

    def truck_bar(self, L, W):
        """Flatbed with a low crossbar: standing hull hits it, a slide (40u
        crouch hull) passes under."""
        top = self.BASE_TOP
        bx = self.rng.uniform(-L / 6, L / 6)
        bar_lo, bar_hi = top + 46, top + 62
        return [self._deck(L, W, top),
                make_box((bx - 12, -W, bar_lo), (bx + 12, W, bar_hi), palette=PAL_DANGER),
                make_box((bx - 12, -W - 16, top), (bx + 12, -W, bar_hi), palette=PAL_DANGER),
                make_box((bx - 12, W, top), (bx + 12, W + 16, bar_hi), palette=PAL_DANGER),
                ], L / 2 - 64, top

    def truck_hurdle(self, L, W):
        """Flatbed with a waist-high block to bhop over without losing speed."""
        top = self.BASE_TOP
        h = self.rng.choice((40, 56))
        return [self._deck(L, W, top),
                make_box((-24, -W, top), (24, W, top + h), palette=PAL_DANGER),
                ], -L / 4, top

    # --- daily-course sections on wheels ---------------------------------

    def truck_slalom(self, L, W):
        """sec_slalom: long deck studded with staggered tall pillars — weave
        at full speed, or wall-kick off one to change line."""
        top = self.BASE_TOP
        parts = [self._deck(L, W, top)]
        n = int((L - 160) // 176)
        for k in range(n):
            px = -L / 2 + 160 + k * 176
            py = (W * 0.45) * (1 if k % 2 else -1)
            parts.append(make_box((px - 20, py - 20, top), (px + 20, py + 20, top + 176),
                                  palette=PAL_WALLS))
        return parts, L / 2 - 80, top

    def truck_hurdles(self, L, W):
        """sec_hurdles: a run of low walls spaced to one bhop each, every wall
        with an alternating-side gap — vault the line or cut the gap."""
        top = self.BASE_TOP
        parts = [self._deck(L, W, top)]
        n = int((L - 160) // 224)
        gap = W * 0.8
        for k in range(n):
            hx = -L / 2 + 192 + k * 224
            side = 1 if k % 2 else -1
            # wall spans the deck except a gap at the alternating edge
            y0, y1 = (-W, W - gap) if side > 0 else (-W + gap, W)
            parts.append(make_box((hx - 12, y0, top), (hx + 12, y1, top + 44),
                                  palette=PAL_DANGER))
        return parts, L / 2 - 80, top

    def truck_tower(self, L, W):
        """sec_tower: double-jump steps (68u, just past a single-jump ledge)
        climbing toward the tail, then the drop onto the next truck."""
        top = self.BASE_TOP
        steps = self.rng.choice((2, 3))
        parts = [self._deck(L, W, top)]
        run = L / (steps + 1)
        for k in range(1, steps + 1):
            x0 = -L / 2 + k * run
            parts.append(make_box((x0, -W, top), (L / 2, W, top + 68 * k),
                                  palette=PAL_TOWER if k % 2 else PAL_PILLAR))
        return parts, L / 2 - run / 2, top + 68 * steps

    def truck_fork(self, L, W):
        """sec_fork: a wide trailer split by a tall centre wall — the left lane
        is a slide-bar tunnel, the right lane a hurdle pair. Pick a side early."""
        top = self.BASE_TOP
        W = W + 96
        parts = [self._deck(L, W, top),
                 make_box((-L / 2 + 96, -12, top), (L / 2, 12, top + 160), palette=PAL_WALLS)]
        for bx in (-L / 6, L / 6):
            parts.append(make_box((bx - 12, -W, top + 46), (bx + 12, -12, top + 62),
                                  palette=PAL_DANGER))
            parts.append(make_box((bx - 12 + L / 12, 12, top), (bx + 12 + L / 12, W, top + 40),
                                  palette=PAL_DANGER))
        return parts, L / 2 - 64, top

    def truck_tunnel(self, L, W):
        """sec_slide: a covered stretch with a 46u ceiling — standing hull
        clips it, a slide carries you through the whole length."""
        top = self.BASE_TOP
        t0, t1 = -L / 4, L / 4
        return [self._deck(L, W, top),
                make_box((t0, -W, top + 46), (t1, W, top + 78), palette=PAL_WALLS),
                make_box((t0, -W - 16, top), (t1, -W, top + 78), palette=PAL_WALLS),
                make_box((t0, W, top), (t1, W + 16, top + 78), palette=PAL_WALLS),
                ], L / 2 - 64, top

    def truck_bhop(self, L, W):
        """sec_bhop: a skeletal flatbed of small pads over open air — chain
        hops pad to pad, a miss drops you onto the kill-road."""
        top = self.BASE_TOP
        pad, n = 112.0, self.rng.choice((4, 5))
        gap = (L - n * pad) / (n - 1)
        parts = []
        for k in range(n):
            x0 = -L / 2 + k * (pad + gap)
            jog = self.rng.uniform(-W * 0.3, W * 0.3)
            parts.append(make_box((x0, jog - 56, top - 16), (x0 + pad, jog + 56, top),
                                  palette=PAL_BHOP))
        return parts, L / 2 - pad / 2, top

    def build(self):
        top = self.BASE_TOP
        # start pad: flush with the truck roofs, just short of the recycle line.
        # A func_static MOVER, not world: the game's seamless wrap shifts it
        # back with the road so it never reappears beside the runner, and a
        # new run brings it home (g_mover.c Truck_Wrap / G_TruckRunReset).
        self.sections.append(("start pad", {}))
        self.movers.append((make_box((-1024, -256, top - 32), (-512, 256, top),
                                     palette=PAL_START),
                            {"classname": "func_static"}))
        for dy in (-96, 0, 96):
            self.place("info_player_deathmatch", -704, dy, top + 40, angle="0")

        weights = self.WEIGHTS[self.diff]
        kinds, wts = list(weights), list(weights.values())
        counts = {}
        for i in range(self.fleet):
            # guarantee one of each kind early in the fleet, then weighted
            kind = kinds[i] if i < len(kinds) else self.rng.choices(kinds, wts)[0]
            counts[kind] = counts.get(kind, 0) + 1
            L = (self.rng.choice((864, 960, 1056)) if kind in self.LONG
                 else self.rng.choice((480, 576, 672)))
            W = self.rng.choice((64, 80, 96))
            parts, rx, rz = getattr(self, f"truck_{kind}")(L, W)
            self.movers.append((parts, {
                "classname": "func_truck",
                "speed": f"{self.speed:g}",
                "spawnx": f"{self.ROAD_LEN:g}", "killx": f"{self.KILL_X:g}",
                "lane": "320", "index": str(i), "count": str(self.fleet),
                "ridex": f"{rx:g}", "ridez": f"{rz:g}",
                "kind": kind}))
        self.sections.append(("truck fleet", dict(count=self.fleet,
                                                  speed=self.speed, **counts)))

        # the highway: a lethal road under the fleet (Clustertruck's "the
        # ground kills you"). Uniform along x so the game's wrap can't be seen;
        # dashed lane lines give the eye a speed reference. A thin trigger
        # skin on the asphalt kills on touch.
        self.sections.append(("kill road", {}))
        x0, x1, w = -1280.0, 4 * self.ROAD_LEN, 1280.0  # room for the queue ahead after a wrap
        self.solids.append(make_box((x0, -w, -64), (x1, w, 0), palette=PAL_WALLS))
        for ly in (-480, -160, 160, 480):
            x = x0
            while x < x1:
                self.solids.append(make_box((x, ly - 6, 0), (x + 160, ly + 6, 1),
                                            palette=PAL_GATE))
                x += 480
        kill = make_box((x0, -w, 1), (x1, w, 24), tex=TEX_TRIGGER,
                        contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((kill, {"classname": "trigger_hurt", "dmg": "10000"}))
        self.solids += make_skybox(x0 - 64, -w - 64, -832, x1 + 64, w + 64, 2048)
        self.entities.insert(0, {
            "classname": "worldspawn",
            "message": f"STRAFE64 trucks {self.seed} (difficulty {self.diff})",
        })
        return self
