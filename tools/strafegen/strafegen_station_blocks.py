"""Station blockout: fixed gallery, clerestory and concourse, one service lift."""
from strafegen_geom import make_box, make_skybox
from strafegen_marble import Marble
from strafegen_materials import tex
from strafegen_palettes import CONTENTS_TRIGGER, TEX_TRIGGER


class StationBlocks(Marble):
    def build(self):
        self.movers = []
        bays = 7
        for bay in range(bays):
            x = bay * 520
            for lane, (y, z, width, material) in enumerate((
                (0, 104, 105, "polished"), (260, 148, 85, "glass"),
                (-260, 68, 119, "marble_tiles"),
            )):
                lift = bay == 3 and lane == 1
                half = self.rng.randint(204, 224)
                deck = make_box((-half, -width, -18), (half, width, 0),
                                tex=tex("brass" if lift else material))
                self.movers.append((deck, dict(
                    classname="func_bobbing" if lift else "func_static",
                    origin=f"{x} {y} {z}", targetname="station_route_deck",
                    count=str(lane), speed="3.6", height="32", phase="0")))
                if not lift:
                    self.box((x-48, y-44, -96), (x+48, y+44, z-18), "board")
            # Repeated structural bays and a covered lower maintenance gallery.
            self.box((x-22, -455, -96), (x+22, -424, 310), "slab")
            self.box((x-218, -455, 310), (x+218, -170, 326), "panel")
            self.box((x-20, 420, -96), (x+20, 449, 390), "board")
            self.neon((x-20, 415, 210), (x+20, 420, 340))
        self.box((-330,-480,-128),(3500,480,-96),"granite_flamed")
        self.box((3330,-455,-96),(3500,455,70),"slab")
        for y in (-420,420):
            self.box((3360,y-12,70),(3384,y+12,320),"brass")
        self.neon((3360,-432,320),(3384,432,330))
        self.place("info_player_deathmatch", -120, 0, 148, angle=0, spawnflags=1, nobots=1)
        self.place("info_player_deathmatch", 1560, 0, 148, angle=180, nohumans=1)
        self.place("info_player_intermission", 500, -400, 280, angle=25)
        self.place("item_armor_body", 2080,260,184)
        self.place("item_health_large", 1550,-260,100)
        self.place("misc_teleporter_dest",-120,0,148,targetname="blocks_return",angle=0)
        for x, kind in ((80,"trigger_race_start"),(3350,"trigger_race_finish"),(3420,"trigger_teleport")):
            b = make_box((x,-470,-96),(x+28,470,380),tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER,draw=set())
            e = dict(classname=kind)
            if kind == "trigger_teleport":
                e["target"] = "blocks_return"
            self.triggers.append((b,e))
        # Full-width return for a runner who drops to the foundation.
        self.triggers.append((make_box((-320,-475,-95),(3330,475,-45),tex=TEX_TRIGGER,
                                      contents=CONTENTS_TRIGGER,draw=set()),
                              dict(classname="trigger_teleport",target="blocks_return")))
        self.solids += make_skybox(-350,-500,-180,3550,500,650)
        self.entities.insert(0, dict(classname="worldspawn",message="Station // Service Galleries"))
        return self
