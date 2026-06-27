# FORGE — in-game strafegen

Generate a STRAFE 64 course from the main menu: scrub the parameters, read a
live schematic of the layout, hit **FORGE**, and drop straight into the map you
just authored. No terminal, no MCP, no rebuild — strafegen runs as a child
process of the engine and the fresh `.bsp`/`.aas` are loaded in place.

This is the in-game front door to `tools/strafegen/strafegen.py`, the same
generator the MCP `engine_generate_map` and the `dojo`/`showcase` scripts drive
from outside.

---

## 1. Goal & scope

| | |
|---|---|
| **Who** | A player at the title screen who wants a fresh course on demand. |
| **What** | Pick *kind / seed / difficulty / length / void*, preview the layout, generate, play. |
| **Where** | New `FORGE` entry on the STRAFE 64 main menu → `ui_generate.c`. |
| **How** | The menu emits a `forge` console command; the engine forks `python3 strafegen.py …`, drops the map into the live game dir, then `devmap`s it. |

Non-goals for v1: editing brush geometry by hand, saving a named library of
forged maps, multiplayer-synced generation, Windows process spawning (stubbed).

---

## 2. Architecture

```
 ui_generate.c  ──(trap_Cmd_ExecuteText)──▶  "forge course 1337 1 1"
        │                                          │
   live schematic                          common.c : Com_Forge_f
   (drawn in C, no gen)                            │
                                    Sys_RunProcess(argv)  ── fork/exec ─▶ python3 strafegen.py
                                                   │                         --name forge_course_1337
                                                   │                         --out <home>/baseoa/maps
                                            (blocks ~5–30 s)                  (writes .bsp + .aas)
                                                   │
                                       Cbuf: "devmap forge_course_1337"
                                                   │
                                            engine loads the loose .bsp/.aas
```

Three layers, each independently testable:

1. **strafegen `--name`** — caller dictates the output basename so the engine
   knows exactly what to `devmap` (no stdout parsing, no name re-derivation).
2. **`Sys_RunProcess` + `forge` command** — the native bridge that runs the
   generator and queues the map load.
3. **`ui_generate.c`** — the menu and the schematic preview.

### 2.1 Why a child process, not the VM

The game/cgame/ui modules are sandboxed dylibs with no filesystem-write or
process API. Map authoring has to happen in the trusted engine layer. The
engine already forks helpers (`Sys_Exec` for the zenity/kdialog dialogs), so we
add a small generic `Sys_RunProcess(const char **argv)` and call it from
`qcommon`, which has `Cvar`, `FS`, and the command buffer (`Cbuf`).

### 2.2 Why loose files into `homepath/baseoa/maps`

idTech3's filesystem finds loose `maps/<name>.bsp` (and the matching `.aas`)
the moment they exist on disk — no `fs_restart` needed mid-session (confirmed by
the existing `engine_api.generate_map` deploy path). So strafegen writes the
`.bsp`/`.aas` directly into the **writable** game dir
(`fs_homepath` + `com_basegame` + `maps`) and `devmap` picks them up. Shaders
come from the already-deployed global identity override pk3, so no per-map pk3
is required to play. (A `--pk3` could be added later for shipping a forged map.)

---

## 3. The `forge` console command

```
forge <kind> <seed> [difficulty] [length] [novoid]
```

| arg | values | default | maps to strafegen |
|-----|--------|---------|-------------------|
| `kind` | `course` `combat` `arena` `surf` `killbox` | `course` | flag: none / `--combat` / `--arena` / `--surf` / `--killbox` |
| `seed` | any 32-bit int | required | positional seed |
| `difficulty` | `0` `1` `2` | `1` | `--difficulty` |
| `length` | `1`–`5` | `1` | `--length` (course/combat only) |
| `novoid` | present = off | void on | `--no-void` |

Behaviour:

1. Resolve `python` = cvar `forge_python` (default `python3`) and the script =
   cvar `forge_strafegen` (default the repo path
   `…/strafe64-engine/tools/strafegen/strafegen.py`). Both `CVAR_ARCHIVE` so a
   user with a moved checkout can point them.
2. Compute the map name: `forge_<kind>_<seed>` (filename-safe; deterministic so
   the same inputs reproduce the same course).
3. Build argv and run `Sys_RunProcess`, **blocking**. On non-zero exit, print
   the failure to the console and abort (no map load).
4. On success, `Cbuf_AddText("devmap forge_<kind>_<seed>\n")`.

The output dir is `FS_BuildOSPath(fs_homepath, com_basegame, "maps")`.

### 3.1 Blocking & feedback

v1 is synchronous: the generator (including the bspc AAS compile) takes roughly
5–30 s and the window is frozen for that span — the same trade-off the engine's
native file dialogs already make. The menu paints one **`FORGING…`** frame
before the command runs (the command buffer executes *after* the current UI
frame), so the player sees feedback. The blocking generation is the documented
v1 limitation; §7 sketches the async upgrade.

---

## 4. The FORGE menu (`ui_generate.c`)

NERV/MAGI styling consistent with `ui_menu.c` / `ui_arena.c` (amber headline,
terminal green, dim labels). Layout, top to bottom:

```
                         FORGE
       AUTHOR A COURSE  //  SCRUB THE SEED, READ THE SPINE

  [ PARAMETERS ]                    ┌──────────────────────────┐
   KIND:        COURSE              │  SCHEMATIC — not to scale │
   DIFFICULTY:  II                  │                          │
   LENGTH:      x1                  │     ·––·                 │
   RISING VOID: ON                  │    /    \___             │
   SEED:        [ 1337     ]        │   ·         \··          │
   > RANDOMIZE                      │  start        finish     │
                                    └──────────────────────────┘
                         FORGE
                         BACK

  STRAFEGEN // baseoa/maps // forge_course_1337
```

### 4.1 Controls

| item | widget | id | notes |
|------|--------|----|-------|
| KIND | spincontrol | `ID_KIND` | course / combat / arena / surf / killbox |
| DIFFICULTY | spincontrol | `ID_DIFF` | I / II / III (0/1/2) |
| LENGTH | spincontrol | `ID_LENGTH` | x1…x5; grayed unless kind ∈ {course, combat} |
| RISING VOID | spincontrol | `ID_VOID` | ON/OFF |
| SEED | numeric field | `ID_SEED` | `QMF_NUMBERSONLY`, up to 9 digits |
| RANDOMIZE | ptext action | `ID_RANDOM` | reseed from `uis.realtime` LCG |
| FORGE | ptext (hero) | `ID_FORGE` | emit the `forge` command |
| BACK | ptext | `ID_BACK` | `UI_PopMenu` |

Changing KIND re-grays LENGTH and updates the preview. Empty/zero seed on FORGE
is auto-randomized.

### 4.2 Emitting the command

```c
Com_sprintf(cmd, sizeof(cmd), "forge %s %i %i %i %s\n",
    kindArg, seed, diff, length, voidOn ? "" : "novoid");
trap_Cmd_ExecuteText(EXEC_APPEND, cmd);
```

The menu does **not** pre-set mode cvars the way ARENA does — a forged course is
a plain time-trial-style run. (Void on/off is handled by the generator baking
the `worldspawn` void keys, matching `--no-void`.)

---

## 5. The layout preview (the "visualize")

The preview is a **schematic**, not the real BSP — it is drawn entirely in C
from the same inputs (kind, seed, length) with a small deterministic LCG, and it
updates the instant any parameter changes. It is explicitly labelled
*"SCHEMATIC — not to scale"* so it reads as a sketch of the spine, not a
minimap.

What it conveys, honestly:

- **seed → a different winding spine** (the node walk is seeded by the seed),
- **length → a longer spine / more nodes** (course & combat),
- **kind → a different silhouette**: `course`/`combat` draw a top-down serpentine
  descent of section nodes; `arena`/`killbox` draw a bounded ring/box with a
  centerpiece; `surf` draws a banked lap loop.
- **difficulty / void** tint the spine (hotter = harder; a dashed rising edge
  when the void is on).

Drawing primitives are the menu's existing `UI_FillRect` (segments as thin
rects) and `UI_DrawString` (start/finish labels). No new renderer calls, no
generation, no file I/O — cheap enough to redraw every frame.

The preview deliberately does **not** claim BSP fidelity; if a player wants the
exact geometry they FORGE and fly it. A future upgrade (§7) can have strafegen
emit a tiny `forge_<…>.layout.json` of real section centroids that the menu
reads back for an accurate overhead.

---

## 6. Files touched

| file | change |
|------|--------|
| `tools/strafegen/strafegen.py` | `--name` flag → forces the output basename. |
| `engine/code/sys/sys_unix.c` | `Sys_RunProcess(argv)` — fork/execvp/wait, returns exit code. |
| `engine/code/sys/sys_win32.c` | `Sys_RunProcess` stub (returns -1, prints "unsupported"). |
| `engine/code/qcommon/qcommon.h` | `Sys_RunProcess` prototype. |
| `engine/code/qcommon/common.c` | `Com_Forge_f` + `forge` command + `forge_python`/`forge_strafegen` cvars. |
| `engine/code/q3_ui/ui_generate.c` | the FORGE menu + schematic preview (new file). |
| `engine/code/q3_ui/ui_local.h` | `extern void UI_GenerateMenu(void);` |
| `engine/code/q3_ui/ui_menu.c` | `FORGE` main-menu item + `ID_S64_FORGE` case → `UI_GenerateMenu()`. |
| `engine/cmake/basegame.cmake` | add `ui_generate.c` to the q3_ui sources. |
| `docs/forge-ingame-strafegen-spec.md` | this document. |

Requires an engine rebuild (new `forge` command + UI module) and a dylib
redeploy to the homepath game dir (per the deploy gotcha in memory).

---

## 7. Future work

- **Async generation** — fork without `wait()`, poll the child in the main loop,
  draw a real progress/spinner screen, `devmap` on exit. Removes the freeze.
- **Accurate overhead** — strafegen emits `--layout-json`; the menu loads real
  section centroids for a true minimap and a start/finish marker.
- **Forge library** — name + save forged maps (`--pk3`) so they persist and can
  be re-played or shared (ties into DAILY's shareable-seed idea).
- **Windows** — implement `Sys_RunProcess` with `CreateProcess`.
- **Tuning passthrough** — expose a couple of generator knobs (voidrise rate,
  archetype for killbox) once the basic loop is proven.
```
