# MapForge — browser map forge for STRAFE 64

Generate, **visualise in 3D**, edit, and export strafegen courses from the
browser. A local Python server drives the existing generator live; the page
renders the real geometry in an orbitable 3D scene (and a 2D plan/elevation),
lets you move/add/delete entities and box brushes, and exports a playable
`.bsp` / Radiant `.map` / `.pk3`.

```sh
cd tools/strafegen
python3 mapforge/server.py --port 8765 --open
# open http://127.0.0.1:8765
```

No pip dependencies (stdlib `http.server`). three.js (r160) is **vendored**
under `static/vendor/` so the tool runs fully offline — no CDN.

Two modes, switched at the top of the left panel:

- **Generate** — procedurally generate a whole map and edit its brushes/entities
  (below).
- **Compose** — kit-bash a map from section **parts**, CAD-style: drag sections
  in the viewport and snap their entry/exit **connection dots** together (below).

## Compose mode (section kit-bash)

Build a course by hand from reusable section parts — start pad, strafe gaps,
bhop lane, fork, slalom, slide ramp, walljump hall, hurdles, movers, hazard
pits, double-jump tower, finish gate — **and geometric primitives**: straight
floor, ramp up/down, stairs, **turn left/right** (reorient the route 90°), and an
open arena pad. The sections all run +Y; the turn primitives let a layout route
in any direction and the ramps/stairs change elevation, so you can shape an
arbitrary map, not just a straight chain.

- **🎲 auto-layout** — one seeded click procedurally assembles a full connected
  course from the kit (start → openers → turns → flow → spice → climb → finish),
  winding through space via turn primitives. Deterministic per seed; edit it
  afterward like any composed layout.
- **Add** a section from the library — it auto-chains onto the last open exit.
- **Drag** a section in the 3D view to move it; as a connection dot nears a
  compatible free dot on another section (red **entry** ↔ green **exit**), it
  **snaps** so the two mate seamlessly (position + orientation solved for you,
  Fusion-style). Rotate in 90° steps and raise/lower in Z from the inspector.
- Each join is a real seam: snapped sections butt together into a continuous,
  playable run. The connection readout tracks open connectors and whether a
  start + finish are present.
- **Freeform box brushes** — drop arbitrary boxes (walls, platforms, cover)
  anywhere with `+ add box brush`, edit their AABB + role, drag them in the view;
  they bake into the export alongside the sections. A map can be sections, free
  brushes, or any mix — a real "build any geometry" editor.
- **Export** bakes the placed parts into one sealed map — geometry transformed
  and merged, a sky enclosure + rising-void rescue added automatically, race
  triggers carried from the start/finish parts — then writes `.bsp` / `.map` /
  `.pk3` (validated with `check_bsp`).

Realtime: the browser holds the only state and transforms/snaps parts locally
(the parts catalog is fetched once); the server only re-composes authoritatively
on export. Snapping/transform math is mirrored in `app.js` and `scene.py`.

## Import / decompile any map

In Generate mode, the **Import / decompile** picker lists every `.bsp` / `.pk3`
under `generated/` (and any dirs in `$MAPFORGE_MAPS`). Load one and MapForge
**decompiles its geometry** — `../bsp_import.py` reads the drawvert / drawindex /
surface lumps of any IBSP v46 map (planar + triangle-soup surfaces, plus a
control-grid approximation of patches) back into the scene model, preserving the
baked vertex colours — and renders it in the 3D view. Works on stock Q3,
OpenArena, or strafegen output: a way to study shipped level design in the same
viewer. Imported maps are view-only — but **✎ edit imported map in Compose**
traces the decompiled geometry into editable freeform boxes (surfaces thickened
to solids, original colours preserved), so you can remix *any* map — move/add/
delete brushes and re-export it.

The **📊 learn** button decompiles every available map and aggregates a design
digest — platform footprints, wall heights, vertical layering (decks), map
volume, item/spawn mix, jump-pad arcs — into percentile distributions
(`bsp_learn.py`). The learned median dimensions then calibrate new freeform box
brushes in Compose, closing the loop: decompile shipped maps → learn their
proportions → author with them.

**🎯 calibrate generator to maps** goes further: it scales the *procedural*
generator's `hscale`/`vscale` to match the corpus's learned platform/height
medians (clamped to the safe 0.5–2.0 range) and regenerates — so the procedural
output takes on the proportions of the maps you decompiled (`scene.calibrate`,
measured against strafegen's own baseline).

```sh
python3 bsp_import.py path/to/map.bsp            # geometry digest
python3 bsp_import.py pack.pk3 --map q3dm6 --json scene.json
python3 bsp_learn.py /path/to/baseoa --json learned.json   # learn from a corpus
MAPFORGE_MAPS=/path/to/baseoa python3 mapforge/server.py   # expose a map dir
```

## What you can do (Generate mode)

- **Generate** any kind — course, combat course, velodrome arena, killbox,
  surf line/turn, lattice arena, combat pit, and every bot-dojo recipe — with
  the full parameter space: seed (+ random / daily), difficulty, length,
  killbox archetype, theme (default / concrete), and the vscale / hscale /
  density / section-toggle modifiers. Builds happen in-memory; no engine or
  `bspc` needed to preview.
- **Visualise** in an orbitable 3D viewport (drag to orbit, scroll to zoom):
  brushes coloured by role, entity markers, pad/portal flow arrows, the world
  enclosure as a faint wireframe (toggle it on to see the shell). Toggle layers
  (geometry / entities / flows / triggers / enclosure / edges) or flip to a 2D
  plan + elevation that mirrors `mapview.py`.
- **Edit**: click a brush or entity to select it. Move entities, retype their
  origin, or delete them; nudge/resize axis-box brushes via numeric fields, add
  new box brushes or entities from the palette (click to place in either view).
  Non-box brushes (ramps/prisms) are read-only but can be deleted. The edit
  count is shown live; **reset all edits** reverts to the pristine generation.
- **Export** `.bsp` (validated with `check_bsp`), `.map` (Radiant source), and
  `.pk3` (bundles the identity shader + procedural textures; packs an `.aas`
  navmesh for bots when `bspc` is present). Files land in
  `tools/strafegen/generated/`. Copy the `.pk3` into `baseq3/` (or the `.bsp`
  into `baseq3/maps/`) and `\map <name>`.

## How it fits together

- `static/` — the frontend (`index.html`, `app.js`, `style.css`) + vendored
  three.js. `app.js` holds the only authoritative scene: it keeps the pristine
  build plus a local edit list, applies edits client-side for instant feedback,
  and sends the structured edit ops to the server only on generate/export.
- `server.py` — stateless backend. `/api/meta` (kinds, params, legend),
  `/api/generate` and `/api/export` are pure functions of `(kind, params,
  edits)`; a global lock serialises builds (the generator's theme is module
  state and `BspWriter.write()` mutates the course, so a course is built fresh
  per request and never written twice).
- `../scene.py` — the shared serializer: `build_kind()` builds any kind exactly
  as `strafegen.generate()` would (greeble + scale), `serialize()` turns a
  course into the scene JSON (brushes → faces, entities, triggers, flows,
  bounds; world-boundary shell brushes are tagged `sky/enclosure`),
  `apply_edits()` applies the JSON edit ops with the safety guards
  (min-extent, worldspawn/last-spawn protection, teleporter `angles`,
  `validate_spawns`).

## Edit op reference (the JSON the frontend sends)

```
{op:"ent.move",   id, origin:[x,y,z]}
{op:"ent.setkey", id, key, value}
{op:"ent.add",    classname, origin:[x,y,z], keys?:{}}
{op:"ent.delete", id}
{op:"brush.move",   id, delta:[dx,dy,dz]}    # axis boxes only
{op:"brush.resize", id, mins:[..], maxs:[..]} # axis boxes only
{op:"brush.add",    mins:[..], maxs:[..], role}
{op:"brush.delete", id}
```

`id` is the position in the freshly-built course's `entities` / `solids` list
(deterministic per seed, so stable across the stateless rebuild).
