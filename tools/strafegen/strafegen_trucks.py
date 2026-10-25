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
    WEIGHTS = (
        {"flatbed": 5, "trailer": 2, "ramp": 2, "walls": 1, "bar": 2, "hurdle": 2},
        {"flatbed": 3, "trailer": 3, "ramp": 2, "walls": 2, "bar": 2, "hurdle": 2},
        {"flatbed": 1, "trailer": 3, "ramp": 2, "walls": 3, "bar": 3, "hurdle": 2},
    )

    def __init__(self, seed, difficulty, fleet=None):
        self.rng = random.Random(seed ^ 0x7C0C)
        self.seed = seed
        self.diff = difficulty
        self.fleet = fleet or (14, 12, 11)[difficulty]   # fewer trucks = wider gaps
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

    def build(self):
        top = self.BASE_TOP
        # start pad: flush with the truck roofs, just short of the recycle line
        self.sections.append(("start pad", {}))
        self.solids.append(make_box((-1024, -256, top - 32), (-512, 256, top),
                                    palette=PAL_START))
        for dy in (-96, 0, 96):
            self.place("info_player_deathmatch", -704, dy, top + 40, angle="0")

        weights = self.WEIGHTS[self.diff]
        kinds, wts = list(weights), list(weights.values())
        counts = {}
        for i in range(self.fleet):
            # guarantee one of each kind early in the fleet, then weighted
            kind = kinds[i] if i < len(kinds) else self.rng.choices(kinds, wts)[0]
            counts[kind] = counts.get(kind, 0) + 1
            L = self.rng.choice((352, 416, 480))
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

        # void kill-pit under the whole road
        self.sections.append(("void pit", {}))
        x0, x1, w = -1280.0, self.ROAD_LEN + 1024.0, 1280.0
        pit = make_box((x0, -w, -768), (x1, w, -512), tex=TEX_TRIGGER,
                       contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((pit, {"classname": "trigger_hurt", "dmg": "10000"}))
        self.solids += make_skybox(x0 - 64, -w - 64, -832, x1 + 64, w + 64, 2048)
        self.entities.insert(0, {
            "classname": "worldspawn",
            "message": f"STRAFE64 trucks {self.seed} (difficulty {self.diff})",
        })
        return self
