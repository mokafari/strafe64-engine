# Engine API — let an LLM drive the engine

A control layer that lets Claude (or any LLM) hook **directly into a running
STRAFE 64 engine** to make adjustments and aid development: read and set cvars,
tune the movement model live, change maps, run any console command, take
screenshots, read bot telemetry, and rebuild the native mod.

Two files:

- **`engine_api.py`** — the Python control library (`EngineSession`). Use it
  from scripts, the dojo harness, or a REPL.
- **`engine_mcp.py`** — a zero-dependency MCP server that exposes the library as
  tools over stdio, so Claude Code hooks straight in.

## How it works — native plumbing, no new C code

The engine already ships everything an external controller needs; the API just
speaks its existing dev channels:

| Direction | Channel | Detail |
|---|---|---|
| **commands in** | `com_pipefile` | The engine creates a FIFO in its home dir and executes any console text written to it, every frame (`Com_ReadFromPipe`). We open the FIFO and write to it. |
| **replies out** | captured stderr | The console prints via `CON_Print` → `fputs(stderr)`, which is unbuffered, so our captured copy of the process output is a real-time transcript. Queries bracket a command with unique `echo` sentinels and scrape the text between them. |
| **telemetry** | JSONL | The modded qagame writes `playtest/<tag>.jsonl` under the session home when `g_playtest 1`. |
| **screenshots** | `screenshotJPEG` | Client mode writes to `<home>/baseoa/screenshots/`. |

### Two tiers of adjustment

- **Live tier (no rebuild)** — any cvar, any console command, map changes,
  screenshots, telemetry, against a running instance. A subset of the movement
  model is cvar-backed and mirrored into the `bg_pmove` physics globals every
  frame (see `g_main.c` cvarTable + `cg_predict.c`), so tuning it changes feel
  *immediately*. Those tunables are listed in `MOVEMENT_CVARS`:

  | cvar | default | range | effect |
  |---|---|---|---|
  | `pm_strafeAccelerate` | 70 | 0–300 | A/D-only air-strafe accel |
  | `pm_airControlAmount` | 150 | 0–400 | W-only velocity-redirect strength |
  | `pm_airaccelerate` | 1 | 0–20 | base air acceleration |
  | `pm_airStopAccelerate` | 3 | 0–20 | counter-strafe stopping accel |
  | `pm_wishSpeedClamp` | 30 | 0–320 | air wish-speed cap (skill ceiling) |
  | `g_gravity` | 1000 | 0–4000 | world gravity (latched — reload map) |
  | `g_knockback` | 1000 | 0–4000 | weapon knockback scale |
  | `g_speed` | 320 | 0–1000 | ground move speed |
  | `timescale` | 1 | 0.05–4 | global time multiplier (slow-mo) |

  **Presets:** `engine_save_preset(name)` snapshots the current movement +
  effect cvars to a `.cfg` under the base game; `engine_load_preset(name)`
  restores it. Checkpoint a good feel, try another, restore or A/B — pairs with
  `engine_compare`/`engine_measure`.

  `engine_source_constants` lists the **full** movement tunable surface (all ~40
  constants in `bg_pmove.c`/`bg_local.h`) with values + file:line, each flagged
  `live` (tune now) or rebuild-only. `engine_set_source_constant(name, value,
  rebuild=true)` edits a rebuild-only constant in place and rebuilds — so the
  whole movement model is tunable through the API: **see → set → rebuild**.

- **Source tier (rebuild)** — the compile-time constants in `bg_pmove.c` /
  `bg_local.h` that aren't cvar-backed (`JUMP_VELOCITY`, wall-jump / slide /
  bhop tuning). Edit the source, then `engine_rebuild` (runs `scripts/build.sh`
  and redeploys the dylibs).

## Library usage

```python
from engine_api import EngineSession

with EngineSession(mode="dedicated", map="surf_64") as eng:
    eng.get_cvar("pm_strafeAccelerate")        # -> "70"
    eng.set_cvar("pm_airControlAmount", 220)   # confirms by reading back
    eng.set_movement("pm_wishSpeedClamp", 50)  # clamped to its range
    eng.console("cvarlist pm_")                 # any command, returns output
    eng.change_map("surf_7")
    eng.telemetry(limit=20)                     # mod JSONL telemetry
```

`mode="dedicated"` is a fast headless server (best for tuning/telemetry loops);
`mode="client"` opens the windowed game (needed for screenshots / visual work).

**Default sandbox.** Omit `map` and the engine boots a known-good playtest
location — `dojo_arena`, a purpose-built bot-dojo arena: enclosed (so framing /
the camera-clamp behave), ships a working `.aas` so bots navigate, uses the
STRAFE shaders, and is open enough to audition models, test effects, drive the
player, and film bots. Pass `map="none"` for the menu, or any map name to
override (`S64_DEFAULT_MAP` changes the default).

Client windows open at a low **640×360** by default (`r_mode -1` +
`r_customwidth/height`) so several can run at once and screenshots stay small and
fast — pass `width`/`height` to change it, or `0`/`0` for the desktop resolution.

## MCP usage (Claude Code)

`../../.mcp.json` registers the server, so it loads automatically when Claude
Code runs in this repo (approve it once when prompted). Tools:

| tool | purpose |
|---|---|
| `engine_help` | re-fetch the grouped tool map + workflow recipes on demand |
| `engine_status` | is a session running, what mode/map (also reports a persistent engine) |
| `engine_launch` | start a session-scoped engine (dies with the session) |
| `engine_open` | start a **persistent**, optionally **named** engine that keeps running across sessions |
| `engine_attach` | reconnect to a persistent engine by name (tools also auto-attach) |
| `engine_close` | quit a persistent engine (by name) |
| `engine_list` | list all persistent engines running across sessions |
| `engine_shutdown` | release the session (persistent engine keeps running) |
| `engine_doctor` | provenance/freshness: is the deployed engine my latest build + right assets? |
| `engine_processes` / `engine_kill_orphans` | list running engines; clean up orphaned session windows |
| `engine_command` | fire a console command (no reply) |
| `engine_console` | run a command, return its console output |
| `engine_get_cvar` / `engine_set_cvar` | read / write any cvar |
| `engine_movement_get` / `engine_movement_set` | snapshot / tune the live movement model |
| `engine_save_preset` / `engine_load_preset` / `engine_list_presets` | checkpoint & restore a tuning 'feel' |
| `engine_map` | change map |
| `engine_look` | one-call orientation: map + who's present + an auto-framed wide screenshot |
| `engine_state` | where everything is (positions, velocity, speed, health, **airborne, walljump/airjump counts**) |
| `engine_map_bounds` | the current map's true geometry extent (mins/maxs/center/size) from the `.bsp` |
| `engine_measure` | sample a subject's speed over time → movement metrics (max/avg/peak) |
| `engine_compare` | sweep a cvar across values → side-by-side collage / max-speed bar chart |
| `engine_frame` | **point & shoot**: auto-compose a shot of a named subject / the action / wide |
| `engine_orbit` | turntable around a subject by name (auto-centered) |
| `engine_screenshot` | capture a frame **and return the image** so Claude can see it |
| `engine_audition_model` | swap to a model, respawn, frame in third person, screenshot |
| `engine_camera` | manually place + aim the free cam (yaw/pitch or look_at a point) |
| `engine_dolly` | move the camera along waypoints → a moving cinematic clip/collage |
| `engine_input` | drive the live player: move / turn / attack / jump / use |
| `engine_spawn` | spawn any entity (items, props, jump pads, lights) into the live map |
| `engine_clear` | remove live-spawned entities (map's own entities untouched) |
| `engine_spectate` | follow a bot/player (chase cam) to film their gameplay |
| `engine_capture_death` | trigger + frame a death/ragdoll/gib/blood/dismember sequence on cue |
| `engine_clip_video` | capture a real-time mp4 of the current view |
| `engine_cinematic` | bullet-time hero shot: slow-mo + orbiting camera → mp4 |
| `engine_record_demo` / `engine_play_demo` / `engine_render_demo` | record/replay a `.dm_71` demo; render it to mp4 |
| `engine_capture_clip` | film the view over time → one collage image (+ optional mp4) |
| `engine_capture_angles` | capture the subject from several orbit angles → one collage |
| `engine_capture_event` | time a shot/clip to an observed console event (after / window) |
| `engine_capture_state` | time a shot/clip to a measured condition (e.g. speed > 400) |
| `engine_give` | cheat: give weapons / items |
| `engine_effects_get` / `engine_effects_set` | snapshot / tune the look palette |
| `engine_telemetry` | read bot playtest JSONL |
| `engine_generate_map` | author a procedural course (strafegen) and load it live |
| `engine_playtest_report` | run a headless bot playtest → fitness report + verdict |
| `engine_selftest` | exercise the whole API end-to-end → pass/fail per capability |
| `engine_reload` | vid_restart / snd_restart so freshly-deployed assets show up |
| `engine_source_constants` | list all source-tier movement constants (live vs rebuild-only) |
| `engine_config_overrides` | show cvars where autoexec/strafe64.cfg overrides the source default |
| `engine_set_source_constant` | edit a rebuild-only constant in place (optionally rebuild) |
| `engine_rebuild` | source tier: rebuild + redeploy the native mod |

A typical tuning loop: `engine_launch` → `engine_movement_get` to see the
current model → `engine_movement_set` to adjust → play / `engine_telemetry` to
judge → repeat. For constants outside the live set: edit `bg_pmove.c`,
`engine_shutdown`, `engine_rebuild`, `engine_launch`.

## Recipes (the server also surfaces these to the LLM on connect)

- **Get oriented:** `engine_open`/`engine_launch` (omit map → sandbox) → `engine_look`.
- **Tune movement feel:** `engine_compare("pm_strafeAccelerate",[70,140,220],mode="movement")`
  → pick a value → `engine_movement_set` → `engine_save_preset`.
- **Film bots:** `engine_launch(mode="client", bots=4)` → `engine_spectate` →
  `engine_clip_video` or `engine_cinematic("action")`.
- **Audit a course:** `engine_generate_map(seed=…)` → `engine_playtest_report`.
- **Dress a scene:** `engine_spawn(classname, keys)` → `engine_frame` → tweak → `engine_clear`.
- **Catch a moment:** `engine_capture_state(field="speed", op=">", value=400)` or
  `engine_capture_event(pattern="was killed")`.
- **Health check:** `engine_selftest`.

Camera/capture tools take a `subject` (a bot/player name, `[x,y,z]`, or `"action"`)
and compute the framing for you — never guess coordinates.

## Auditioning models & effects (the adapt-live loop)

Launch in **client** mode (a real window opens; cheats auto-enabled via
`devmap`) and the screenshot tools return the actual frame, so Claude can see
what it changed and adjust:

1. `engine_launch(mode="client", map="surf_64")`
2. `engine_audition_model(model="gargoyle", weapon=1)` → swaps the model, gives
   a weapon, frames it in third person, and returns a screenshot.
3. `engine_effects_set(name="au_bass", value=0.8, shoot=true)` → warps the
   world via the audio-reactive shaders and screenshots the result.
4. `engine_camera(x, y, z, yaw, noclip=true)` → free-fly to frame a subject.
5. Look at the image, adjust, repeat. For mesh changes, convert with
   `obj2md3.py`, drop the `.md3` under `<oa>/baseoa/models/...`, then audition.

**The look palette** (`engine_effects_get`): audio-reactive shader envelopes
(`au_bass/mid/high/level`), the bullet-time core (`g_timeBind`, `g_timeBindMin`),
view (`cg_fov`, `cg_thirdPerson*`, `cg_drawGun`), particles/decals/trails
(`cg_shadows`, `cg_marks`, `cg_brassTime`, `cg_railTrailTime`), and renderer look
(`r_gamma`, `r_picmip`, `r_overBrightBits`, aniso). Renderer cvars auto
`vid_restart` to apply.

> **Gotcha baked into `audition_model`:** the bullet-time core near-freezes game
> time when the player is still (`g_timeBindMin`), which stretches the respawn
> timer to tens of *real* seconds while dead — the audition would screenshot the
> death scoreboard. `audition_model` sets `g_forceRespawn 1` and `g_timeBind 0`
> so the model loads and frames at normal speed.

## Capturing clips & bot footage

Both capture tools bake frames into a single **collage** (contact-sheet) image
that's returned to Claude — no video player needed — and `capture_clip` can also
emit an `.mp4` (ffmpeg). The HUD is hidden during capture (`clean`, default on).

**Film a bot's gameplay over time:**

1. `engine_launch(mode="client", map="dojo_arena", extra=["+addbot","gargoyle","4","+addbot","merman","4"])`
2. `engine_spectate()` — become a spectator and chase-cam the next bot
   (`team spectator` + `follownext` + third person). Pass `target` to follow a
   specific one; cycle with the same tool.
3. `engine_capture_clip(frames=9, interval=0.5, fps=8)` — snaps a timed sequence
   and returns a labelled grid (`0.0s … 4.0s`) plus an mp4.

**Multi-angle / turntable** (same moment from several angles):

- `engine_capture_angles(angles=[0,90,180,270], range=120)` — orbits the
  third-person camera (`cg_thirdPersonAngle`) around the followed bot (or an
  auditioned model) with time nearly frozen, returning an angle-labelled grid.

Library equivalents: `spectate()`, `clip_collage()`, `angles_collage()`,
`capture_frames(hook=…)` for custom per-frame moves, `collage(paths)` to bake an
arbitrary frame set, and `make_video(paths, fps)`.

For an **mp4** of the live action, `engine_clip_video(seconds)` (lib:
`clip_video`) grabs frames as fast as possible and stitches at the achieved rate
so playback is real-time — reliable footage; spectate a bot or drive the player
first to choose what it films.

For a **bullet-time hero shot**, `engine_cinematic(subject)` (lib:
`cinematic_clip`) ramps time into slow-mo and orbits the camera around the
subject while filming → mp4 — the Matrix-rotate that fits the sword / time-bind
game.

`engine_record_demo` (lib: `clip_demo`/`record_demo`/`stop_demo`) records real
gameplay into a Quake `.dm_71` demo — a shareable, replayable artifact;
`engine_play_demo` replays it and `engine_render_demo` renders it to mp4 via the
engine's framebuffer capture. Note: demos recorded on a *listen server* can be
short (sparse snapshots), so for dependable footage prefer `engine_clip_video`.

## Point & shoot — don't guess coordinates

**Get oriented first.** `engine_look` is the one-call "where am I / what's
happening" — it returns the map, every client's position/speed/health, and an
auto-framed wide screenshot in a single response. Reach for it when you
(re)attach to an engine instead of chaining status/state/screenshot.

The fastest path: **name what you want to see, the tools compute the camera.**

- `engine_state` returns every client/bot's live position + the camera, so the
  model has the spatial info it needs (no guessing where things are).
- `engine_frame(subject)` auto-places + aims the camera and returns the shot.
  `subject` is a bot/player **name or number**, an `[x,y,z]`, or `"action"`
  (the players' centroid — auto-pulls back to fit a spread-out group).
  `mode="wide"` frames the whole scene (pass `radius` to pull back over an empty
  course — there are no players to size the shot from during authoring). The camera is **clamped inside the
  playable volume** (bounded by where the players are) so it never ends up
  outside the geometry shooting into fullbright void.
- `engine_orbit(subject)` is a turntable centered on a named subject's live
  position — angles computed for you.

This sits on top of the manual `engine_camera`/`engine_dolly` (below) for when
you do want explicit control.

**Point & measure.** `engine_state` reports each player's velocity, speed,
health, **airborne flag, and walljump/airjump counts**. `engine_measure(subject,
seconds, field)` samples any of those over a window → `{max, avg, final, peak_at,
series}` — speed for a bhop/slide/surf run, or `air`/`wj`/`dj` for moveset usage.
`engine_capture_state` can likewise trigger on the moveset (e.g. `field="wj"
op=">" value=2` to catch a wall-jumper, or `field="air"` for mid-air). Drive the
player (`engine_input` with `play`) or follow a bot, then measure — tune movement
by the numbers instead of by eye.

`engine_compare(cvar, values, mode="movement")` drives a real **air-strafe + bhop**
run (not a plain forward run) under each value and measures peak speed — so it
actually exercises the speed-building tech and can tune `pm_strafeAccelerate` /
`pm_airControlAmount` / `pm_wishSpeedClamp` / bhop boost. (`engine_input` supports
a `{"strafe": secs}` step for this pattern; note bhop needs *pulsed* jumps —
held jump just runs at the ground cap.) Scripted air-strafe is noisy run-to-run,
so movement mode runs `trials` (default 3) per value and takes the **median** —
the result also reports each trial + spread. Kill leftover engines first
(`engine_kill_orphans`); a stale instance can starve the measurement.

**Sweep & decide.** `engine_compare(cvar, values)` tries several values in one
call. `mode="visual"` frames a subject and screenshots each value → a labelled
side-by-side collage (effects, FOV, look). `mode="movement"` respawns and runs a
fixed input under each value while measuring → a max-speed bar chart + metrics
table (movement cvars). Pick the value you want from the result, no eyeballing
separate runs.

## Camera placement, interacting, and timing shots

**Place + aim a camera anywhere.** `engine_camera` (lib: `setviewpos`/`look_at`)
free-flies the view to a world point and aims it — by `yaw`+`pitch`, or by
`look_at=[x,y,z]` to auto-aim at a subject. (Pitch uses an extended
`setviewpos x y z yaw pitch` baked into qagame — rebuilt via `engine_rebuild`.)
`engine_dolly` interpolates the camera across a path of waypoints to film a
moving cinematic; `[x,y,z,tx,ty,tz]` waypoints make it *track* a target.

**Interact with the world.** `engine_input` (lib: `play_mode`, `move`, `turn`,
`attack`, `jump`, `use_item`) drives the live character — spawn as a player,
run/strafe, turn, swing the sword / fire, trigger jump pads and pickups. It sets
`g_timeBind 0` so control runs at normal speed (not the still-player near-freeze).

**Edit the live world.** `engine_spawn(classname, keys)` (lib: `spawn`) drops any
entity into the running map — `item_health_mega`, `weapon_rocketlauncher`,
`item_quad`, `misc_model` (with `keys={"model": "models/…"}`), `target_push`,
lights, triggers. Defaults to ~96u in front of the view, or pass
`keys={"origin": "x y z"}`. Place items/props/jump-pads and audition them in
place — runtime level dressing. `engine_clear` (lib: `clear_spawns`) removes the
entities you spawned (optionally one classname), leaving the map's own entities
intact — so you can spawn → look → clear → respawn while iterating.

**Time shots/clips to what you want to observe.** `engine_capture_event`
(lib: `wait_event`, `capture_on_event`, `record_window`) watches the live console
for a regex — kills (`was killed`, `fragged`), `entered the game`, race finishes,
telemetry markers, or your own `echo` beacons — and either films the **aftermath**
(`mode="after"`) or keeps a **pre-trigger ring buffer + post frames**
(`mode="window"`) so the clip brackets the moment, not just what came after.

`engine_capture_state` times on **measured state** instead of a log line: it
waits until a subject's field crosses a threshold (`field="speed" op=">"
value=400` to catch a fast run, or `health < 30`), then auto-frames that subject
and films it (lib: `wait_state`, `capture_on_state`).

## Persistent engine (leave it running)

`engine_launch` ties the engine to the MCP process. To keep one **continuously
running** across sessions, use `engine_open` instead — it launches detached (its
own session group) and writes a small state file under
`~/.strafe64-engine/<name>/baseoa/engine_api.session.json` (base overridable with
`S64_LIVE_BASE`). Any later process reattaches by reading that file, reopening
the command FIFO and the console log — the engine itself never restarts.

- `engine_open(mode="client", map="dojo_arena", bots=4)` — start it once.
- Work normally; if the MCP server restarts, the next tool call **auto-attaches**.
- `engine_attach` — explicitly reconnect / confirm.
- `engine_close` — quit it and clear the state file.

### Multiple engines at once (one per session)

Pass `name` to run several engines side by side — each gets its **own home dir
and its own auto-allocated UDP port**, so they never collide:

- Session A: `engine_open(name="alpha", map="surf_64")`
- Session B: `engine_open(name="bravo", map="dojo_arena", bots=4)`
- `engine_list` → every instance across all sessions (name, mode, map, port, pid, alive).
- `engine_attach(name="alpha")` makes a process target that instance; all other
  tools then drive it. `engine_close(name="bravo")` quits a specific one.

Each MCP process tracks one *current* instance (defaults to `"default"`), so a
session opens its engine once and the rest of the tools just work. Ports are
allocated from 27960 upward, skipping ports already claimed by live instances.

Library: `EngineSession(..., name="alpha", detached=True).start()`,
`EngineSession.attach(live_home("alpha"))`, `EngineSession.list_live()`,
`EngineSession.peek()` (status without opening fds), `.close()`, `live_home(name)`.

**Cleanup.** `engine_processes` lists all running engines (persistent / session /
other); `engine_kill_orphans` closes leftover *session* windows whose launcher
exited — it never touches persistent engines, foreign processes, or the current
session. (lib: `list_processes()`, `kill_orphans(keep_pid)`.)

## Authoring content live

`engine_generate_map(seed, kind="course|arena|surf|killbox", difficulty, length)`
runs the procedural generator (`strafegen.py`), deploys the result into the
running instance's game dir, and loads it — **design + play in one call**. The
loose `.bsp`/`.aas` go in `maps/` (found at open time even mid-session); the
`.pk3` (full shaders) is copied for the next fresh launch. Same seed reproduces
the same map.

For art iteration: deploy a model/texture/shader (e.g. `obj2md3.py` to convert a
mesh) into the instance's `baseoa/…`, then `engine_reload` (`vid_restart`) to pick
it up live, and `engine_audition_model` to inspect it.

**Judge it.** `engine_playtest_report(map)` (lib: `playtest_report`) spins a
headless bot server with `g_playtest` telemetry, lets the bots play, and returns
flow, airborne %, avg max speed, best bhop chain, moveset usage, death causes,
and a verdict. Generate → report → tweak the seed/constants → report again. Needs
a map with an `.aas` (bots navigate).

Note: **bots ignore the race start/finish triggers**, so `completion_pct` is ~0
for bot runs and is *not* a fitness signal — the verdict judges the
movement-quality metrics bots actually exercise (flow, stuck, moveset). Bots also
tend to cluster/telefrag near spawn in dedicated runs, which depresses flow; for
clean course-traversal numbers a human run is more representative.

## Measured findings

See [FINDINGS.md](FINDINGS.md) for data gathered with these tools — air-strafe
tuning (the shipped constants are well-tuned), the bhop-ceiling experiment, and
the bullet-time response curve (incl. an `autoexec.cfg` freeze-floor inconsistency
worth a decision).

## "Is this my latest build?" — engine_doctor

The single most common trap (across many sessions) is **silently testing a stale
binary** or the wrong assets. `engine_doctor` (lib: `doctor()`) answers it in one
read-only call: engine binaries, **build-output vs deployed dylib mtimes** (flags
"built but not deployed" = stale, and concurrent-session deploys), whether the
qagame/cgame/ui trio is in sync (they must deploy together), the classic old-path
traps (`~/ioquake3`, `~/openarena-0.8.8`), and what's running. Run it whenever a
change "isn't showing up."

## Health check

`engine_selftest` (lib: `selftest()`) spins its own client engine and exercises
the whole surface — cvars, movement, effects, state, spawn/clear, presets,
framing, screenshot, demo — returning pass/fail per capability. Run it after a
rebuild or whenever something seems off; an all-green result means the engine and
every layer of the tooling are working.

`python3 test_engine_api.py` is the fast, **offline** companion: stdlib-only unit
tests for the pure logic (parsing, geometry, aggregation, collage, config
diagnostics) — no engine needed, catches regressions in the module quickly.

## Requirements

- The engine must be built (`scripts/build.sh` → `engine/build/Release`).
- OpenArena assets at `assets/openarena` (or set `OA=`).
- macOS / Apple Silicon (the deploy step re-signs the dylibs for dlopen).
