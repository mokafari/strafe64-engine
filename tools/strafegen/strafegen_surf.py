"""strafegen_surf — the core-loop SurfLine + the banked SurfTurn test piece."""
import math
import random

from strafegen_geom import *
from strafegen_palettes import *
from strafegen_textures import *

class SurfLine:
    """The ★ core-loop seed: a CS-surf-style line you flow through. Built from
    steep banked ramps (make_prism tops tilted past the walkable angle, so you
    don't stand — you surf), strung surf -> transition -> surf, ending in a
    finish that teleports back to the start (lap +1). v0 is a hand-laid two-ramp
    proof: it generates valid geometry and wires the full loop (race clock +
    MISSION REPORT fire on finish, teleport resets you). Ramp angles and the
    momentum hand-off between ramps are human-tuned from here — bots can't surf
    (AAS doesn't model air-strafe-on-ramps), so this is playtest-validated, not
    dojo-gated."""

    def __init__(self, seed=0, ramps=None):
        self.rng = random.Random(seed)
        self.ramps = ramps          # None = seed-driven 3-5; else a fixed count
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def surf_ramp(self, x0, x1, y0, y1, z_entry, fwd_drop, roll_drop, bank,
                  pal=PAL_SLIDE):
        """One surf ramp. The top is a single plane, steeply banked across Y
        (roll = roll_drop over the width, set past ~45 deg so it's non-walkable
        and you surf) and gently descending along X (fwd_drop over the length,
        for forward progress). bank=+1 tilts down toward +Y (you drift +Y),
        bank=-1 toward -Y. z_entry is the top height at the high entry edge.
        Returns (z_min, z_max) of the ramp top."""
        W, L = (y1 - y0), (x1 - x0)
        B = fwd_drop / L                 # +X lowers z (forward descent)
        C = (roll_drop / W) * bank       # +Y lowers z when bank=+1 (the roll)
        y_high = y0 if bank > 0 else y1  # the high edge the player enters on
        A = z_entry + B * x0 + C * y_high
        pz = lambda x, y: A - B * x - C * y
        foot = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
        top_zs = [pz(x, y) for (x, y) in foot]
        zb = min(top_zs) - 128.0
        self.solids.append(make_prism(foot, zb, top_zs, tex=TEX_FLOOR, palette=pal))
        return min(top_zs), max(top_zs)

    def pad(self, x0, x1, y0, y1, ztop, pal, thick=32.0):
        self.solids.append(make_box((x0, y0, ztop - thick), (x1, y1, ztop),
                                     tex=TEX_FLOOR, palette=pal))

    def build(self):
        rng = self.rng
        n = self.ramps if self.ramps else rng.randint(3, 5)
        self.sections.append(("surf line", {"ramps": n}))

        # one shared Y-corridor for the whole line. Alternating the bank each
        # ramp makes the player zig-zag inside it: a +1 ramp's LOW exit edge
        # (y1) is exactly a -1 ramp's HIGH entry edge (y1), so consecutive ramps
        # meet flush on the player's path with no realignment — that's what lets
        # an arbitrary seed-driven chain stay continuous.
        W = 512.0
        y0, y1 = -W / 2.0, W / 2.0

        # --- start pad (high) -------------------------------------------------
        sx0, sx1, stop = -384.0, -64.0, 1024.0
        cx = (sx0 + sx1) / 2.0
        self.pad(sx0, sx1, -176, 176, stop, PAL_START)
        # several spread spawns: a single spawn telefrags a field of bots on
        # respawn (and is cramped for humans racing together)
        for sy in (-120, -40, 40, 120):
            self.entities.append({"classname": "info_player_deathmatch",
                                  "origin": f"{cx:g} {sy:g} {stop + 40:g}", "angle": "0"})
        self.entities.append({"classname": "misc_teleporter_dest",
                              "targetname": "start_dest",
                              "origin": f"{cx:g} 0 {stop + 40:g}", "angle": "0"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"{cx:g} 0 {stop + 360:g}", "angle": "0"})
        self.triggers.append((make_box((sx0, -176, stop), (sx1, 176, stop + 256),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set()),
                              {"classname": "trigger_race_start"}))

        # --- procedural ramp chain -------------------------------------------
        x = 0.0
        entry_z = stop
        lo_z = stop
        for i in range(n):
            bank = +1 if (i % 2 == 0) else -1
            L = float(rng.randint(1280, 1792))
            angle = rng.uniform(49.0, 55.0)        # surfable band (non-walkable)
            roll_drop = round(W * math.tan(math.radians(angle)))
            fwd_drop = float(rng.randint(200, 320))
            x_start = x
            r_lo, _ = self.surf_ramp(x, x + L, y0, y1, entry_z,
                                     fwd_drop=fwd_drop, roll_drop=roll_drop, bank=bank)
            # bot bait: items down the ramp midline (y=0). AAS can't path a surf
            # face, so bots won't ride it for its own sake — but they'll chase a
            # pickup onto it, where BotSurfControl takes over. Item z follows the
            # ramp plane at y=0 (== entry_z - roll_drop/2 - fwd_drop*frac), lifted.
            for frac in (0.25, 0.5, 0.75):
                iz = entry_z - roll_drop / 2.0 - fwd_drop * frac + 56.0
                self.entities.append({
                    "classname": "item_armor_shard",
                    "origin": f"{x_start + frac * L:g} 0 {iz:g}"})
            lo_z = r_lo
            x += L
            entry_z = r_lo               # next ramp enters at this ramp's low corner
            if i < n - 1:                # transition pad, flush at the low edge
                gap = 384.0
                self.pad(x, x + gap, y0, y1, r_lo, PAL_PLAIN)
                x += gap

        # --- finish pad (low) + lap teleport back to start -------------------
        # the trigger is a TALL full-corridor wall (lo_z-64 .. +768): a surfer
        # arrives here airborne at 600+ ups, well above a short floor trigger —
        # a 256-tall box let fast runs (bot AND human) fly clean over the finish.
        fx0, fx1 = x, x + 448.0
        self.pad(fx0, fx1, y0, y1, lo_z, PAL_FINISH)
        # trigger reaches modestly back over the last ramp tail + is tall (below):
        # a fast surfer arrives airborne and a touch short of the pad. (Bots lose
        # the line earlier on the later ramps and fall well short — full bot lap
        # completion is an open surf-control-robustness problem, not a trigger one.)
        tx0 = fx0 - 256.0
        fb = make_box((tx0, y0, lo_z - 64), (fx1, y1, lo_z + 768),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((tx0, y0, lo_z - 64), (fx1, y1, lo_z + 768),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_teleport",
                                   "spawnflags": "2",   # RESCUE: set the faller down
                                   "target": "start_dest"}))

        # --- sky box enclosing the whole line --------------------------------
        for b in make_skybox(sx0 - 128, y0 - 128, lo_z - 256,
                             fx1 + 128, y1 + 128, stop + 320):
            self.solids.append(b)

        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 surf line"})
        return self


class SurfTurn:
    """Banked surf TURN test piece — the last surf-depth primitive. A steep
    banked 180deg arc (inner edge low, outer high, like a velodrome corner but
    >45deg so you surf it): enter at one end, lean into the bank, carry speed
    around the curve, exit travelling the opposite way. This is the candidate
    geometry for closed surf circuits with corners; FEEL is human-validated (bots
    can't surf an arc), so v0 just proves it generates + loads. If it rides well,
    fold `arc()` into SurfLine for true 2D circuits."""

    def __init__(self, seed=0):
        self.rng = random.Random(seed)
        self.solids = []
        self.triggers = []
        self.entities = []
        self.sections = []

    def pad(self, x0, x1, y0, y1, ztop, pal, thick=32.0):
        self.solids.append(make_box((x0, y0, ztop - thick), (x1, y1, ztop),
                                     tex=TEX_FLOOR, palette=pal))

    def build(self):
        Ri, W = 512.0, 320.0            # inner radius, lane width
        Ro = Ri + W
        H = 420.0                       # bank height over W -> atan(420/320)=53deg, surfable
        SEGS = 14
        th0, th1 = math.pi / 2, 3 * math.pi / 2   # 90 -> 270 deg = 180deg U, bulging -X
        self.sections.append(("banked surf turn",
                              {"deg": 180, "bank": f"{int(H)}z/{int(W)}u~53deg"}))

        def arc(r, th):
            return (r * math.cos(th), r * math.sin(th))

        # banked arc, segment by segment (inner low z=0, outer high z=H)
        for i in range(SEGS):
            ta = th0 + (th1 - th0) * i / SEGS
            tb = th0 + (th1 - th0) * (i + 1) / SEGS
            ia, ib = arc(Ri, ta), arc(Ri, tb)
            oa, ob = arc(Ro, ta), arc(Ro, tb)
            self.solids.append(make_prism([ia, ib, ob, oa], -160.0,
                                          [0.0, 0.0, H, H], palette=PAL_SLIDE))

        # entry pad at the th0 mouth (y=+, x=0), exit pad at the th1 mouth (y=-)
        self.pad(-64, 64, Ri, Ro, 0.0, PAL_START)
        cx = 0.0
        self.entities.append({"classname": "info_player_deathmatch",
                              "origin": f"{cx:g} {Ri + W / 2:g} 48", "angle": "180"})
        self.entities.append({"classname": "misc_teleporter_dest",
                              "targetname": "start_dest",
                              "origin": f"{cx:g} {Ri + W / 2:g} 48", "angle": "180"})
        self.entities.append({"classname": "info_player_intermission",
                              "origin": f"0 0 {H + 200:g}", "angle": "0"})
        self.triggers.append((make_box((-96, Ri, 0), (96, Ro, 128),
                              tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set()),
                              {"classname": "trigger_race_start"}))

        self.pad(-64, 64, -Ro, -Ri, 0.0, PAL_FINISH)
        fb = make_box((-96, -Ro, 0), (96, -Ri, 256),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((fb, {"classname": "trigger_race_finish"}))
        tb = make_box((-96, -Ro, 0), (96, -Ri, 256),
                      tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
        self.triggers.append((tb, {"classname": "trigger_teleport",
                                   "spawnflags": "2",   # RESCUE: set the faller down
                                   "target": "start_dest"}))

        for b in make_skybox(-Ro - 192, -Ro - 192, -288, Ro + 192, Ro + 192, H + 384):
            self.solids.append(b)
        self.entities.insert(0, {"classname": "worldspawn",
                                 "message": "STRAFE64 banked surf turn"})
        return self


