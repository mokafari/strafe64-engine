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

## What you can do

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
