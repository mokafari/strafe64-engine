"""Station roof: descending canopy into a sword encounter and two exits."""
from strafegen_materials import tex
from strafegen_geom import make_box, make_skybox
from strafegen_marble import Marble
from strafegen_palettes import CONTENTS_TRIGGER, TEX_TRIGGER


class Station(Marble):
    def build(self):
        self.box((-2600, -500, -180), (2600, 500, 0), "marble_tiles")
        self.box((-2470, -260, 0), (-1900, 260, 240), "slab")
        self.solids.append(make_box((-1900, -260, 0), (-550, 260, 240),
                                    tex=tex("glass"), top_drop={5: 144, 7: 144}))
        self.box((-550, -260, 0), (300, 260, 96), "polished")
        # Rail continues at canopy height. Missing it lands on the lower deck.
        self.box((300, 116, 0), (1940, 166, 96), "brass")
        self.box((1940, 72, 0), (2440, 240, 96), "slab")
        for x in range(-2300, 2401, 540):
            self.box((x, -395, 0), (x + 20, -371, 340), "board")
            self.box((x, -395, 336), (x + 20, -250, 350), "brass")
        for y in (-410, 400):
            self.box((-2500, y, -64), (2500, y+10, 32), "board")
        for x in (-2500, 2480):
            self.box((x, -400, 0), (x+20, 400, 80), "board")
        self.place("info_player_deathmatch", -2220, 0, 280, angle=0, spawnflags=1, nobots=1)
        for x, y in ((-440, 70), (120, -50)):
            self.place("info_player_deathmatch", x, y, 136, angle=180, nohumans=1)
        self.place("info_player_intermission", 500, -350, 290, angle=160)
        self.place("item_armor_combat", 120, 0, 128)
        self.place("item_health_large", 1930, -120, 32)
        self.place("item_armor_body", 2080, 142, 128)
        self.place("misc_teleporter_dest", -2210, 0, 282, angle=0, targetname="restart")
        def gate(x, width, height, kind, **keys):
            brush = make_box((x, -width, 0), (x+32, width, height),
                             tex=TEX_TRIGGER, contents=CONTENTS_TRIGGER, draw=set())
            self.triggers.append((brush, dict(classname=kind, **keys)))
        gate(-2100, 260, 440, "trigger_race_start")
        gate(2200, 500, 400, "trigger_race_finish")
        gate(2310, 500, 400, "trigger_teleport", target="restart")
        for y in (-360, 340):
            self.box((2200, y, 0), (2220, y+20, 260), "brass")
        self.neon((2200, -360, 260), (2220, 360, 270))
        self.solids += make_skybox(-2600, -500, -180, 2600, 500, 840)
        self.entities.insert(0, {"classname":"worldspawn", "message":"Station Roof"})
        return self
