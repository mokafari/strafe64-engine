#!/usr/bin/env python3
"""
strafegen — procedural movement-run map generator for Quake III Arena.

Writes playable IBSP v46 .bsp files directly (no q3map compile pass), tuned
to the movement mod in code/game/bg_pmove.c: bunny-hop chains, double jump,
crouch slide, CPM air control and walljumps.

    python3 strafegen.py 1337                 # generate generated/strafe64_1337.bsp
    python3 strafegen.py 1337 --difficulty 2  # harder gaps, fewer rest pads
    python3 strafegen.py 1337 --pk3           # also pack a .pk3 with an .arena file
    python3 strafegen.py 1337 --map           # also emit a Radiant-editable .map
    python3 strafegen.py --daily --pk3        # today's tower: same course worldwide
    python3 strafegen.py --check FILE.bsp     # validate any generated bsp
    python3 strafegen.py --selftest           # generate+validate a seed matrix

Courses carry race triggers (trigger_race_start / trigger_race_finish)
for the mod's run timer + ghosts, and worldspawn void keys (voidbase /
voidrise / voiddelay) for the rising kill plane. length > 1 builds a
tower: decks of sections chained by lift-gate teleporters, climbing
away from the void.

Drop the .bsp into baseq3/maps/ (or the .pk3 into baseq3/) and run:
    \\map strafe64_1337
"""

import argparse
import os

# Modular strafegen: this file is the FACADE + CLI. The generator was split into
# focused strafegen_* modules; we re-export their public API so external importers
# (mapview.py, shaderlib/*.py) keep doing `import strafegen` / `from strafegen
# import ...` unchanged. The heavy lifting lives in those modules; this file just
# re-exports + drives the CLI.
import strafegen_gfx as gfx
import strafegen_config as cfg
from strafegen_physics import *
from strafegen_palettes import *
from strafegen_tga import _tga32, _clamp8
from strafegen_shaders import *
from strafegen_textures import *
from strafegen_textures import _build_synthsky
from strafegen_geom import *
from strafegen_bsp import *
from strafegen_course import *
from strafegen_arena import *
from strafegen_killbox import *
from strafegen_surf import *
from strafegen_pack import *

# GFX is the live toggle in strafegen_config (cfg.GFX); --no-gfx clears it. This
# module-level alias is a read-only convenience for back-compat importers.
GFX = cfg.GFX


def generate(seed, difficulty, length, out_dir, want_map, want_pk3,
             arena=False, name=None, void=True, voidrise=None,
             voiddelay=None, dojo=None, surf=False, killbox=False,
             latticearena=False, combat=False, archetype=None,
             item_trail=False, standalone=False, backup=False,
             bake=False, bake_bounce=3,
             vscale=1.0, hscale=1.0, density=1.0, sections=None,
             theme="default", greeble_density=1.0, godrays=True):
    os.makedirs(out_dir, exist_ok=True)
    cfg.THEME = theme           # read by BspWriter to remap bulk faces to concrete
    mods = cfg.GenMods(vscale=vscale, hscale=hscale, density=density,
                       sections=sections)
    if latticearena:
        lite = latticearena == "lite"
        base = "lattice_arena_lite" if lite else "lattice_arena"
        name = name or (f"{base}_{seed}" if seed else base)
        course = LatticeArena(seed, weapons=not lite).build()  # 16-pilot heat arena
    elif killbox:
        if not name:
            base = f"strafe64kb_{archetype}" if archetype else "strafe64kb"
            name = f"{base}_{seed}" + ("" if difficulty == 1 else f"_d{difficulty}")
        course = Killbox(seed, difficulty, archetype=archetype, mods=mods).build()
    elif surf == "turn":
        name = name or (f"surfturn_{seed}" if seed else "surfturn_64")
        course = SurfTurn(seed).build()  # banked surf-turn test (human feel-validated)
    elif surf:
        name = name or (f"surf_{seed}" if seed else "surf_64")
        course = SurfLine(seed).build()  # ★ core-loop surf line (human-validated)
    elif dojo == "arena":
        name = name or "dojo_arena"
        course = Pit(seed).build()      # tight combat pit (was the velodrome)
    elif dojo:
        name = name or f"dojo_{dojo}"
        course = Course(seed, difficulty, 1, void=void, voidrise=voidrise,
                        voiddelay=voiddelay, recipe=DOJO_RECIPES[dojo],
                        item_trail=item_trail, mods=mods).build()
    elif arena:
        name = name or (f"strafe64dm_{seed}"
                        + ("" if difficulty == 1 else f"_d{difficulty}"))
        course = Arena(seed, difficulty).build()
    else:
        tag = "cb" if combat else ""
        name = name or (f"strafe64{tag}_{seed}"
                        + ("" if difficulty == 1 else f"_d{difficulty}")
                        + ("" if length == 1 else f"_x{length}"))
        course = Course(seed, difficulty, length, void=void,
                        voidrise=voidrise, voiddelay=voiddelay,
                        combat=combat, item_trail=item_trail, mods=mods).build()
    # CONCRETE THEME: erode the big masses into lun3dm5-style cube clusters
    # (decorative, gameplay-safe — see greeble_course). The face material remap
    # itself is applied at BSP-emit via cfg.THEME; this adds the silhouette.
    if theme == "concrete" and greeble_density > 0:
        n = greeble_course(course, seed=getattr(course, "seed", 0),
                           density=greeble_density)
        print(f"    concrete theme: +{n} greeble cubes (density {greeble_density:g})")
    # VOLUMETRIC SUN-SHAFTS: the arena-class maps have a real sky-sun, so pour
    # visible light into the air — leaning additive god-ray columns + a corona,
    # aligned to the same bearing as the cast shadows. Non-colliding decor; rides
    # the same vertex-lit BSP (no bake needed). --no-gfx / --no-godrays skip it.
    if godrays and cfg.GFX and (arena or killbox or latticearena):
        import strafegen_volumetric as vol
        n = vol.add_godrays(course, seed=getattr(course, "seed", seed or 0))
        print(f"    volumetric: +{n} sun-shaft brushes (god-rays + corona)")
    # universal post-build resize (identity at 1,1,1 -> byte-unchanged). Runs
    # before bake/write so both paths see the resized geometry.
    scale_course(course, mods.hscale, mods.hscale, mods.vscale)
    # universal guard: no map ships with a player spawn inside solid geometry
    # (the "stuck in pillar" ejection). Checks the FINAL scaled geometry for
    # every kind, before bake/write — fails the build loudly if violated.
    validate_spawns(course)
    # LAYER 3: q3map2 baked lighting (sun shadows + AO + bounce) with the
    # vertex-color identity preserved. Short-circuits the direct-BSP path — the
    # bake compiles geometry from the .map and re-injects the palette. Ships a
    # standalone pk3 (baked BSP + AAS + lightmapped shader + textures).
    if bake:
        import strafegen_bake as bk
        st = bk.build_baked_pk3(name, course, out_dir, bounce=bake_bounce)
        print(f"{name}: BAKED via q3map2 — suns={st['suns']}, "
              f"palette {st['matched']}/{st['surfaces']} surfaces, aas={st['aas']}")
        for sec, info in course.sections:
            extra = (" " + " ".join(f"{k}={v}" for k, v in info.items())) if info else ""
            print(f"    {sec}{extra}")
        print(f"    + {os.path.basename(st['pk3'])} (standalone baked pk3)")
        print(f"    play: copy into baseoa and run \\map {name}")
        return st["pk3"]

    bsp_path = os.path.join(out_dir, f"{name}.bsp")
    if backup:
        # opt-in: stash any prior artifacts for this name before we overwrite
        for ext in (".bsp", ".aas", ".map", ".pk3"):
            dst = backup_path(os.path.join(out_dir, name + ext))
            if dst:
                print(f"    backed up {name}{ext} -> {os.path.relpath(dst)}")
    stats = BspWriter(course).write(bsp_path)
    check_bsp(bsp_path)
    print(f"{name}: {stats['brushes']} brushes, {stats['surfaces']} surfaces, "
          f"{stats['leafs']} leafs, {stats['bytes'] / 1024:.0f} KB")
    for sec, info in course.sections:
        extra = (" " + " ".join(f"{k}={v}" for k, v in info.items())) if info else ""
        print(f"    {sec}{extra}")
    aas_path = compile_aas(bsp_path)
    if aas_path:
        print(f"    + {os.path.basename(aas_path)} (bots enabled)")
    else:
        print("    (no bspc found -> no .aas, bots can't navigate)")
    if want_map:
        write_map(course, os.path.join(out_dir, f"{name}.map"))
        print(f"    + {name}.map (Radiant source)")
    if want_pk3:
        pk3 = write_pk3(bsp_path, name, out_dir, aas_path,
                        gfx_on=cfg.GFX, standalone=standalone)
        print(f"    + {os.path.basename(pk3)}"
              + (" (standalone: shader+textures bundled)" if standalone else ""))
        if not standalone:
            shared = write_shared_assets(out_dir, cfg.GFX)
            print(f"    + {os.path.basename(shared)} (shared shader+textures — "
                  f"deploy alongside every map pk3)")
    print(f"    play: copy into baseq3/maps and run \\map {name}")
    return bsp_path



def selftest():
    import tempfile
    ok = 0
    with tempfile.TemporaryDirectory() as td:
        for seed in (1, 2, 3, 7, 42, 1337):
            for diff in (0, 1, 2):
                course = Course(seed, diff, 1).build()
                p = os.path.join(td, f"t_{seed}_{diff}.bsp")
                BspWriter(course).write(p)
                check_bsp(p)
                ok += 1
        for seed in (99,):
            course = Course(seed, 1, 3).build()  # tower: 3 decks, 2 lifts
            lifts = sum(1 for s, _ in course.sections if s == "lift gate")
            assert lifts == 2, f"tower expected 2 lift gates, got {lifts}"
            classes = [e["classname"] for _, e in course.triggers]
            assert classes.count("trigger_race_start") == 1
            assert classes.count("trigger_race_finish") == 1
            ws = course.entities[0]
            assert float(ws["voidrise"]) > 0 and float(ws["voidbase"]) < 0
            p = os.path.join(td, "t_long.bsp")
            BspWriter(course).write(p)
            check_bsp(p)
            ok += 1
        # void can be disabled for free practice
        course = Course(5, 1, 1, void=False).build()
        assert "voidrise" not in course.entities[0]
        ok += 0
        for seed in (1, 7, 1337):
            for diff in (0, 2):
                arena = Arena(seed, diff).build()
                p = os.path.join(td, f"a_{seed}_{diff}.bsp")
                BspWriter(arena).write(p)
                check_bsp(p)
                ok += 1
        # killbox: vertical melee arena with momentum portals
        for seed in (1, 42, 1337):
            kb = Killbox(seed, 1).build()
            classes = [e["classname"] for _, e in kb.triggers]
            assert classes.count("trigger_teleport") == 4, "killbox wants 4 portals"
            dests = [e for e in kb.entities
                     if e.get("classname") == "misc_teleporter_dest"]
            assert all(float(d["angles"].split()[0]) > 999999 for d in dests), \
                "killbox portals must preserve momentum (angles[0] > 999999)"
            p = os.path.join(td, f"kb_{seed}.bsp")
            BspWriter(kb).write(p)
            check_bsp(p)
            ok += 1
        # volumetric god-rays: non-colliding DECOR brushes that still DRAW. Place
        # them on an arena and assert the writer keeps them translucent (so they
        # never clip a player) yet emits drawn surfaces (so the beam is visible).
        import strafegen_volumetric as vol
        ar = Arena(1337, 1).build()
        n = vol.add_godrays(ar, seed=1337, count=5)
        assert n >= 5, "add_godrays placed nothing"
        decor = [b for b in ar.solids if b.contents == CONTENTS_DECOR]
        assert len(decor) == n, f"expected {n} decor brushes, got {len(decor)}"
        assert all(b.contents != CONTENTS_SOLID for b in decor), \
            "god-ray brushes must be non-solid (players pass through)"
        p = os.path.join(td, "gr.bsp")
        BspWriter(ar).write(p)
        check_bsp(p)
        ok += 1
    print(f"selftest: {ok} maps generated and validated, all invariants hold")
    print(f"physics: jump range @320 = {jump_range(320):.0f}u, "
          f"single-jump ledge <= {LEDGE_SJ:.0f}u, "
          f"double-jump ledge <= {LEDGE_DJ:.0f}u")



def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("seed", nargs="?", type=int, help="course seed")
    ap.add_argument("--dojo", choices=("speed", "flow", "ztrick", "arena", "all",
                                       "slalom", "hurdles", "hazard", "movers",
                                       "fork", "showcase"),
                    help="generate a bot-dojo scenario (or 'all' four)")
    ap.add_argument("--arena", action="store_true",
                    help="deathmatch arena with a velodrome ring instead "
                         "of a linear run")
    ap.add_argument("--killbox", action="store_true",
                    help="futuristic vertical melee arena: wall-jump columns, "
                         "a central spire and momentum portals for the "
                         "hack-and-slash / time-bind game")
    ap.add_argument("--arch", default=None,
                    choices=("spire", "spiral", "forest", "ring", "cross",
                             "twin", "court"),
                    help="force the killbox centerpiece archetype (else seed-random)")
    ap.add_argument("--combat", action="store_true",
                    help="combat-flow course: the speed->flow->spice arc laced "
                         "with slice-gate enemies (apex gates + 2-3 enemy "
                         "phrases at the spice beats); openers/fork/flow stay "
                         "enemy-free as rest. Dev look, same as the base course")
    ap.add_argument("--surf", action="store_true",
                    help="generate the ★ core-loop surf line (steep banked "
                         "ramps you air-strafe along; finish laps back to start)")
    ap.add_argument("--surfturn", action="store_true",
                    help="generate a banked surf-TURN test piece (180deg steep "
                         "banked arc — candidate corner geometry, feel-test by hand)")
    ap.add_argument("--latticearena", action="store_true",
                    help="generate the 16-pilot LATTICE heat arena (big open "
                         "sealed box, 24 SPREAD spawns so a full field has no "
                         "telefrag cascade, room to carve long trail-walls)")
    ap.add_argument("--noweapons", action="store_true",
                    help="with --latticearena: weapons-LIGHT variant (no guns → "
                         "the trail is the sole decider, purer lattice TTK)")
    ap.add_argument("--difficulty", type=int, default=1, choices=(0, 1, 2))
    ap.add_argument("--length", type=int, default=1,
                    help="course length multiplier (1 = ~6 sections)")
    ap.add_argument("--out", default="generated", help="output directory")
    ap.add_argument("--map", action="store_true",
                    help="also write a Radiant .map source")
    ap.add_argument("--pk3", action="store_true",
                    help="also pack a .pk3 with an .arena file")
    ap.add_argument("--check", metavar="FILE", help="validate a .bsp and exit")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--daily", action="store_true",
                    help="today's course (seed = UTC date): same tower "
                         "worldwide, share your time")
    ap.add_argument("--no-void", action="store_true",
                    help="omit the rising void (free practice)")
    ap.add_argument("--voidrise", type=float, default=None,
                    help="override void rise rate in ups/s")
    ap.add_argument("--voiddelay", type=float, default=None,
                    help="override void grace period in seconds")
    ap.add_argument("--no-gfx", action="store_true",
                    help="omit the graphics-recipe shaders (sun/shadows, PBR "
                         "hull, chrome, plasma, beam) — vanilla identity look")
    ap.add_argument("--no-godrays", action="store_true",
                    help="skip the volumetric sun-shafts/corona on arena-class "
                         "maps (they're on by default when gfx is on)")
    ap.add_argument("--standalone", action="store_true",
                    help="bundle the shader + textures INTO the map pk3 (portable "
                         "single file). Default is LEAN: shared assets ship once "
                         "in zzz_strafe64_assets.pk3 (deduped, no FS collision)")
    ap.add_argument("--backup", action="store_true",
                    help="move any existing .bsp/.aas/.map/.pk3 for this name into "
                         "backups/maps-<date>/ before overwriting (default: "
                         "overwrite in place)")
    ap.add_argument("--bake", action="store_true",
                    help="LAYER 3: bake real lightmaps (sun shadows + AO + "
                         "radiosity bounce) via q3map2, preserving the vertex-"
                         "color identity. Ships a standalone pk3. Needs q3map2 "
                         "(tools/strafegen/build-q3map2.sh)")
    ap.add_argument("--bake-bounce", type=int, default=3,
                    help="q3map2 radiosity bounces for --bake (default 3)")
    ap.add_argument("--item-trail", action="store_true",
                    help="breadcrumb pickups + a finish anchor so goal-seeking "
                         "bots run the whole path (a bot movement test bed)")
    ap.add_argument("--name", default=None,
                    help="force the output basename (else derived from kind+seed); "
                         "used by the in-game FORGE bridge so the engine knows "
                         "exactly which map to load")
    ap.add_argument("--emit-shared", metavar="DIR", default=None,
                    help="write ONLY the shared zzz_strafe64_shader.pk3 into DIR "
                         "and exit (no map). The in-game FORGE uses this to "
                         "guarantee the identity shader pak is present so its "
                         "loose .bsp maps render with real shaders, not grey")
    # ---- art theme ----
    ap.add_argument("--theme", default="default", choices=("default", "concrete"),
                    help="art theme. 'default' = Source dev look (orange floors/grey "
                         "walls); 'concrete' = lun3dm5 brutalist pale concrete — one "
                         "material + a cube-greeble pass that erodes masses into the "
                         "stacked-cube silhouette. Accents stay vivid in both")
    ap.add_argument("--greeble-density", type=float, default=1.0,
                    help="with --theme concrete: cube-erosion density (1.0 default, "
                         "0 = concrete material only / no added cubes, 1.8 = heavier)")
    # ---- geometry modifiers (Stage B) ----
    ap.add_argument("--gen-vscale", type=float, default=1.0,
                    help="vertical size multiplier (heights, deck rise, bank). "
                         "1.0 = unchanged. Best on arenas; scales linear-course "
                         "gaps past the fixed jump (harder), which is expected")
    ap.add_argument("--gen-hscale", type=float, default=1.0,
                    help="horizontal size/spacing multiplier (widths, radii, "
                         "gaps). 1.0 = unchanged")
    ap.add_argument("--gen-density", type=float, default=1.0,
                    help="placement-count multiplier (gaps/bhop/slalom/hurdles/"
                         "hazard counts). 1.0 = unchanged, 0.4 sparse, 1.8 dense")
    ap.add_argument("--gen-sections", default=None,
                    help="comma list of course section keys to INCLUDE (else all): "
                         "gaps,bhop,slide,walls,slalom,hurdles,movers,hazard,fork,tower")
    args = ap.parse_args()
    _genmods = dict(
        vscale=args.gen_vscale, hscale=args.gen_hscale, density=args.gen_density,
        sections=(args.gen_sections.split(",") if args.gen_sections else None),
        theme=args.theme, greeble_density=args.greeble_density)

    global GFX
    if args.no_gfx:
        cfg.GFX = False
        GFX = False

    if args.check:
        stats = check_bsp(args.check)
        print(f"{args.check}: OK {stats}")
        return
    if args.selftest:
        selftest()
        return
    if args.emit_shared:
        os.makedirs(args.emit_shared, exist_ok=True)
        p = write_shared_assets(args.emit_shared, cfg.GFX)
        print(f"shared assets -> {p}")
        return
    if args.surf or args.surfturn:
        # --daily --surf = the daily-speedrun core loop: one date-seeded surf
        # circuit, same worldwide, fresh each day (unifies the daily identity
        # with the ★ surf core loop instead of the tower course).
        dname = None
        if args.daily:
            import datetime
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d")
            seed = int(stamp)
            dname = f"surf_daily_{stamp}"
        else:
            seed = args.seed if args.seed is not None else 64
        generate(seed, args.difficulty, 1, args.out, args.map, args.pk3,
                 surf=("turn" if args.surfturn else True),
                 name=args.name or dname,
                 standalone=args.standalone, backup=args.backup, **_genmods)
        return
    if args.dojo:
        seed = args.seed if args.seed is not None else 64
        targets = (["speed", "flow", "ztrick", "arena"]
                   if args.dojo == "all" else [args.dojo])
        for d in targets:
            # the arena dojo reuses a velodrome seed bspc is known to digest
            # ("Tried parent" aborts on some seeds); movement dojos use 64
            s = 1337 if d == "arena" and args.seed is None else seed
            generate(s, args.difficulty, 1, args.out, args.map, args.pk3,
                     dojo=d, void=not args.no_void,
                     voidrise=args.voidrise, voiddelay=args.voiddelay,
                     item_trail=args.item_trail, name=args.name,
                     standalone=args.standalone, backup=args.backup, **_genmods)
        return
    if args.latticearena:
        seed = args.seed if args.seed is not None else 64
        generate(seed, args.difficulty, 1, args.out, args.map, args.pk3,
                 latticearena="lite" if args.noweapons else True,
                 name=args.name, godrays=not args.no_godrays,
                 standalone=args.standalone, backup=args.backup, **_genmods)
        return
    name = None
    length = args.length
    if args.daily:
        if args.seed is not None:
            ap.error("--daily picks the seed from the date; drop the seed")
        import datetime
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d")
        args.seed = int(stamp)
        name = f"strafe64_daily_{stamp}"
        if length == 1:
            length = 3      # the daily is a tower
    if args.seed is None:
        ap.error("a seed is required (or use --daily / --check / --selftest)")
    generate(args.seed, args.difficulty, length, args.out,
             args.map, args.pk3, arena=args.arena, killbox=args.killbox,
             combat=args.combat, archetype=args.arch,
             name=args.name or name, void=not args.no_void,
             voidrise=args.voidrise, voiddelay=args.voiddelay,
             item_trail=args.item_trail, godrays=not args.no_godrays,
             standalone=args.standalone, backup=args.backup,
             bake=args.bake, bake_bounce=args.bake_bounce, **_genmods)


if __name__ == "__main__":
    main()
