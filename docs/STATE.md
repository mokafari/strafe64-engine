# STRAFE 64 — game state

Orientation doc for the self-contained engine tree at `~/strafe64-engine`.
For the prioritized work backlog see [ROADMAP.md](ROADMAP.md); for what to
check by hand see [HUMAN_PLAYTEST.md](HUMAN_PLAYTEST.md); for where maps and the
game are heading see [MAP_DESIGN.md](MAP_DESIGN.md).

_Last updated: 2026-06-15._

## What this tree is

A clean, self-contained build of the **C / ioquake3** line of STRAFE 64:
the engine, the gameplay mod, the course generator, the assets, and the docs
in one place. Extracted from the live `~/ioquake3` tree (canonical source).

> A separate **Godot** reimplementation lives at `~/strafe64` — unrelated to
> this tree. The id GPL dump at `~/Quake-III-Arena` is where this project
> originally grew. See the `strafe64-three-trees` memory.

```
engine/            ioquake3 engine + the STRAFE 64 mod
                   code/game  = qagame.dylib  (rules, movement, race, bots, slowmo)
                   code/cgame = cgame.dylib   (HUD, ghost replay, void, FLOW, PSX)
                   code/q3_ui = ui.dylib      (NERV/MAGI reskinned menu)
tools/strafegen/   procedural course generator (writes IBSP v46 .bsp directly)
                   + prebuilt arm64 bspc, strafe64.cfg / psx.cfg, dojo harness
assets/openarena/  bundled OpenArena 0.8.8 free assets (gitignored, ~455 MB)
docs/              this file, ROADMAP, MOVEMENT, ART_DIRECTION, VISUALS, PLAYTEST…
scripts/           build.sh, run.sh
```

## Build & run (verified 2026-06-15)

```sh
./scripts/build.sh         # cmake + ninja, arm64 Release, QVMs off → green (768/768)
./scripts/run.sh           # windowed client, bundled assets
./scripts/run.sh -p -b 4   # PSX look + 4 bots
./scripts/run.sh -daily    # today's date-seeded surf circuit
./scripts/run.sh -d <map>  # headless dedicated server (load test)
```

**Verified working end to end:** clean build → modded `qagame/cgame/ui.dylib`
deployed into `baseoa/` (re-signed for Apple Silicon dlopen) → boots on the
bundled OpenArena assets → live game with movement/race/ghost mod. The client
loaded `ghosts/strafe64_1337.gho` and spawned a player; dedicated server loads
maps + AAS navmesh. Renderer: OpenGL1 over Metal (Apple M3 Pro). FreeType is
off, so the menu uses bitmap fonts (the one expected yellow warning at boot).

The game runs **native dylibs**, not QVM bytecode (`vm_game/vm_cgame/vm_ui 0`,
requires `sv_pure 0`). All three share networked headers — `run.sh` always
deploys them together; a stale mix makes the client exit on load. See the
`strafe64-dylib-deploy` memory.

## Feature inventory (what's implemented)

- **Movement kit** — Q3 + CPM air control, bhop window, crouch-slide +
  slide-jump, wall-run (`STAT_WALLRUN`, camera lean), double-jump, buyable
  air-jump. Tuned snappier/Titanfall (gravity 1000, accel 14). Constants live
  in `code/game/bg_pmove.c` / `bg_local.h` and are mirrored at the top of
  `tools/strafegen/strafegen.py`.
- **Race layer** — `trigger_race_start` / `trigger_race_finish`; live HUD
  timer; per-lap PERS_SCORE payout; lap teleport-to-start; MISSION REPORT
  death screen with the loadout shop (`buy`). The session loop is
  surf → lap → points → spend → repeat.
- **Ghost system** — records the local player's best run and replays it
  alongside. `cgame/cg_view.c`: `CG_AddGhostModel`, `CG_RaceFrame`,
  `CG_GhostSave/Load`; saved to `ghosts/<map>.gho`. See ghost notes below.
- **Rising void** — kill-plane that climbs over time (`CG_VoidZ`,
  `CG_AddVoidPlane`); worldspawn carries `voidbase/voidrise/voiddelay`.
  Design rule: the void clock must stay REAL so stopping is never safe.
- **Timebind / slow-mo** — SUPERHOT-style world-clock dilation driven by
  movement intent (`G_UpdateTimeBind` in `code/game/g_active.c`). Default
  **off** (`g_timeBind 0`). See slow-mo notes below.
- **FLOW combo** — cgame style multiplier + run score + screen-shake / fov
  punch juice (`CG_UpdateCombo` / `CG_DrawCombo`). A concurrent session has
  historically owned the parallel flow/glitch/speedline layer in these files.
- **PSX preset** — point-sampled lo-fi look (`-p` → `psx.cfg`); affine warp
  defaulted off (Gustav finds it too harsh — keep the PSX vibe subtle).
- **strafegen** — seed-based courses (linear, surf, arena/velodrome, daily),
  procedural detail/sky/void textures, identity shaders, AAS navmesh via the
  bundled `bspc`. `python3 tools/strafegen/strafegen.py --help`.
- **Bots** — bhop / wall / double-jump via `bot_moveset`; can now surf
  (`BotSurfControl`). Dojo harness (`tools/strafegen/dojo.py`, `g_playtest.c`)
  for headless telemetry.
- **LATTICE mode** — last-pilot-alive battle royale where each pilot's
  damaging speed-trail is the third player (`g_lattice 1`). Per-client trail
  ring + contact-chip in `code/game/g_lattice.c`; colour-coded vertical
  light-wall rendering in `code/cgame/cg_lattice.c`; short health, death→
  elimination, reuses the rising void as the collapsing floor. See ROADMAP.

## Key cvars

| cvar | default | what |
|---|---|---|
| `cg_ghost` | 1 | replay the best-run ghost while racing |
| `g_timeBind` | 0 | SUPERHOT world-clock dilation (off by default) |
| `g_timeBindMin/Max` | 0.25 / 1.0 | timescale floor (still) / ceiling (full intent) |
| `g_timeBindRef` | 700 | horizontal speed (u/s) counted as full intent |
| `g_timeBindCurve` | 1.5 | intent→timescale exponent (>1 stays slow longer) |
| `g_timeBindLog` | 1 | logarithmic ("Matrix") slow-mo curve vs linear |
| `g_vectorgun` | 1 | vectorgun weapon mode |
| `g_mutator` | 0 | daily mutator selector (9 = resolve-at-load) |
| `g_lattice` | 0 | LATTICE last-pilot-alive mode (latched; damaging speed-trails) |
| `g_latticeHealth` | 60 | pilot health pool in LATTICE (short by design) |
| `g_latticeDamage` | 9 | chip damage per trail-contact tick |
| `g_latticeRadius` | 40 | proximity (u) to a trail that counts as contact |

## Known sharp edges

- **Concurrent edits:** `~/ioquake3` is edited by other sessions. This tree is
  a point-in-time snapshot. Re-syncing the engine can catch a torn state — if
  you re-pull, always `./scripts/build.sh clean` (rsync preserves mtimes, so
  incremental builds skip changed files). This actually bit the first build.
- **QVMs are disabled** because the QVM libc subset lacks `pow()` (used by the
  timebind log curve). Native dylibs are what ship, so this is fine.
- **Ghost & bots under slow-mo are unfinished** — see ROADMAP "Ghost & slow-mo
  polish".
