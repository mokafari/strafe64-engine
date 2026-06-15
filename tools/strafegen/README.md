# strafegen — procedural movement runs for Quake III Arena

Seed-based course generator in the STRAFE 64 mold. It writes **playable
IBSP v46 `.bsp` files directly** — no q3map compile pass, no Radiant —
so a fresh course is one command away:

```sh
python3 strafegen.py 1337            # -> generated/strafe64_1337.bsp
python3 strafegen.py 1337 --pk3      # + .pk3 with an .arena entry
python3 strafegen.py 1337 --map      # + Radiant-editable .map source
python3 strafegen.py 1337 --difficulty 2 --length 2
python3 strafegen.py --daily --pk3   # today's tower: same course worldwide
python3 strafegen.py --selftest      # generate + validate a seed matrix
```

`--daily` derives the seed from the UTC date and builds a 3-deck tower
(`strafe64_daily_YYYYMMDD`) — everyone generates the identical course,
so a finish time is a complete, shareable score.

Copy the `.bsp` into `baseq3/maps/` (or the `.pk3` into `baseq3/`) and:

```
\map strafe64_1337
```

Same seed + difficulty + length always produces the same course, so a
seed is a shareable race track.

Or skip all of that: `./play.sh new` / `./play.sh arena new` generates,
installs and launches in one step (ioquake3 + OpenArena assets).

## Arena mode (with bots)

```sh
python3 strafegen.py 1337 --arena --pk3    # -> strafe64dm_1337
BOTS=3 ./play.sh arena 1337                # generate + launch + 3 bots
```

A deathmatch bowl instead of a linear run: a 16-segment **banked
velodrome** rings the arena — 26–34° bank (seeded), so humans ride it
like a wall-of-death at speed and bots can still walk it (under the
45.5° MIN_WALK_NORMAL limit). The loop is the fast way around: 3–5
**boost gates** (`trigger_push` bands on the bank) fling riders a
segment forward, and an **armor-shard speed line** runs the lane.

A **lap-race line** crosses the lane (green start / cyan finish stripes,
one segment apart on a gate-free straight) wires the velodrome into the
game's race layer: cross the start, ride a counter-clockwise lap, cross
the finish and the HUD timer reports it — same `trigger_race_start/finish`
contract as the courses, so ghosts record here too. Bots ignore the race
triggers, so deathmatch is unaffected.

The center is seeded per map: a three-tier **tower** with risers tuned
to the movement mod (56u single-jump, double-jump-only upper tiers,
quad on top), or a **crater** — double-jump-only 74u walls around a
damage floor with the quad on a safe pedestal: dive in, pay, climb out.
Around it: ramps, north/south **kicker wedges** that turn ground speed
into a launch arc onto the center, 3–6 seeded pillars for walljumps,
four jump pads, and a full item loadout. Railgun and megahealth sit on
ledges at the top of the bank — ride the velodrome to shop.

**Bot support**: `build-bspc.sh` compiles id's `bspc` AAS compiler from
`code/bspc` (works on modern macOS/clang — see the portability notes in
the script). When the `bspc` binary is present next to `strafegen.py`
(or in `$BSPC`/`$PATH`), every generated map gets a `.aas` navmesh
packed into its pk3 automatically and bots just work. Three source
fixes were needed and live in the repo: an lvalue cast in
`l_bsp_hl.c`, AAS plane-limit headroom in `aas_store.c`, and — the
sneaky one — `md4.c`'s `UINT4` was `unsigned long` (8 bytes on LP64),
which silently broke the BSP checksum embedded in `.aas` files and made
the engine reject them as "out of date".

## Tuned to the movement mod

Every gap, ledge and hall is derived from the constants in
`code/game/bg_pmove.c` / `bg_local.h` and asserted solvable at
generation time (80% safety margin on the physics bound):

| Section | Mechanic exercised | Geometry rule |
|---|---|---|
| strafe gaps | vanilla SJ + CPM air control | gaps 150u → ~230u, sized to `speed × 0.675s` airtime |
| bhop lane | held-jump rehop (`pm_bhopWindowMs`) | 100–144u gaps, small pads, chain builds ≤ ×1.10 |
| slide ramp | crouch slide + `pm_slideJumpBoost` | −160z ramp into a ~260u gap only a slide-jump clears |
| walljump hall | `pm_wallJumpKick`, 2 kicks max | floorless 448–504u hall, length ≤ jump + 2 kicks reach |
| dj tower | `pm_doubleJumpBoost` window | 68u risers — above single-jump (63.6u), below DJ (92.4u) |
| lift gate | — | length > 1 chains decks: the gate teleports a full 1024u deck up |
| finish gate | — | teleports back to start; the void below does too |

If you retune the mod, update the mirrored constants at the top of
`strafegen.py` — the asserts will then re-derive what is reachable.

## The race layer

Every course wires into the mod's race systems
(see [MOVEMENT.md](../MOVEMENT.md)):

- **Race triggers**: a `trigger_race_start` volume covers the start pad
  (clock starts when you leave it) and a `trigger_race_finish` shares
  the finish arch with the lap teleporter — live HUD timer, finish
  centerprint, and ghost recording all hang off these.
- **Rising void**: worldspawn carries `voidbase` / `voidrise` /
  `voiddelay`. Towers pace the void at one deck per 90/70/55 s
  (difficulty 0/1/2) after a grace period; flat courses use a gentler
  rate. `--no-void` omits it; `--voidrise`/`--voiddelay` override it.
  The fall-rescue teleporter keeps working until the void overtakes it.
- **Identity shaders + procedural detail**: every `--pk3` bundles
  `scripts/strafe64.shader` and two procedurally-generated tiling detail
  maps (`build_detail_textures` → `textures/strafe64/d_floor.tga` /
  `d_wall.tga`, 64×64, ~8 KB zipped total — no hand-painted art). The
  floor/wall surfaces map the detail texture with `rgbGen exactVertex`,
  so the section's vertex **colour** still reads as its identity (N64
  clarity) while near-white grid/panel/rivet lines + faint noise carve
  tech detail into it. Walls add a second **additive** stage
  (`accent.tga`, ~160 B zipped — near-black with faint conduit lines) at
  `rgbGen exactVertex`, scrolling upward: section-tinted energy conduits
  drifting up the walls, motion that reads as speed. The **sky** is a
  procedural starfield
  (`sky_stars.tga`, 128×128 ~5 KB: periodic nebula + sparse amber/cyan
  stars with cross-glints) rendered as two parallax layers scrolling at
  different scales over the void dome — a space backdrop with no painted
  skybox. The `strafe64/void` kill-plane (drawn by the cgame) is no
  longer a flat sheet: a procedural digital lattice (`void_hex.tga`,
  ~4 KB) modulates the red, with two layers scrolling + `tcMod turb`
  warping against each other so it churns like dissolving data rising to
  eat the world. No lightmaps.
  At ~1 texel/unit it's deliberately lo-fi and crisp under the PSX
  point-sampling preset.

## How the direct BSP write works

- **No vis**: empty VISIBILITY lump → `CMod_LoadVisibility` /
  `R_LoadVisibility` treat everything as visible. Fine at these brush
  counts.
- **No lightmaps**: surfaces use `LIGHTMAP_BY_VERTEX (-3)` with baked
  vertex colors — section identity is a color, N64-clarity style.
- **Light grid**: emitted at exactly the size `R_LoadLightGrid`
  computes from the world bounds, so player models get lit.
- **Collision**: a real KD tree over the brushes (≤8 per leaf); every
  leaf gets its own cluster, area 0.
- **Winding**: faces are wound clockwise viewed from outside —
  front-sided shaders cull `GL_FRONT` (see `tr_backend.c GL_Cull`).
- **Triggers**: inline submodels (`*1`, `*2`, …) referenced by
  `trigger_teleport` entities, contents `CONTENTS_TRIGGER`.
- `--check FILE.bsp` re-parses any output and verifies every index,
  the tree, entity/model references and the light grid formula.

## Testing

`--check` and `--selftest` validate structure offline. For a real load
test, the dedicated server exercises the strict collision loader and
entity spawn without a display:

```sh
./run-openarena.sh -d strafe64_1337     # after copying the .pk3 to baseoa/
```

Verified loading on ioquake3 + OpenArena assets with the movement-mod
`qagame.dylib` (the only complaint is the expected missing `.aas`).
Note OpenArena's texture set differs from pak0, so stock-Q3 texture
names may fall back to the default shader there — courses stay fully
readable through the vertex colors.

## Known limits

- **Shaders live in the pk3** — a bare `.bsp` without
  `scripts/strafe64.shader` renders every surface with the grey default
  shader. Still fully playable since color lives in the verts; ship the
  pk3.
- Geometry is axis-aligned boxes + ramps by design: parseable at
  speed, cheap to trace, trivially correct collision.
