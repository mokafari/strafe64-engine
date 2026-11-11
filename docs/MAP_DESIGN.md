# STRAFE 64 — Map Design Doc & Direction

_Where the game is heading, and the rules the procedural generator follows to
get there._ Written 2026-06-18. Companion to [ROADMAP.md](ROADMAP.md) (the
work backlog), [MOVEMENT.md](MOVEMENT.md) (the physics), and
[ART_DIRECTION.md](ART_DIRECTION.md) (the look).

This doc is the bridge between **design principles** (synthesised from Neon
White, Titanfall 2, Ghostrunner, Mirror's Edge, Quake/Defrag/surf, Trackmania,
Clustertruck, DOOM Eternal) and **generator parameters** — the actual knobs in
[strafegen.py](../tools/strafegen/strafegen.py). Every principle below is tagged
with its status against the current engine:

- **[x] shipped** — exists and is wired
- **[~] partial** — half-built or needs a feel pass
- **[ ] proposed** — the generator/engine doesn't do this yet; this is the spec

A confidence note carried from the source research: qualitative principles are
well-corroborated; hard numbers come from two tiers — **(physics)** constants
from the Quake/surf community (high confidence) and **(derived)** ratios I've
turned into starting parameters (tune in-engine, not gospel).

---

## 0. The thesis — what this game IS

**Movement is the game; combat is fused into movement; the world is a course you
flow through, lap after lap, chasing the discovered millisecond.**

The core verb chain — already named across the codebase as the **flow loop** —
is:

> **Descend → Tighten → Revector → Slice → Release**

(gravity donates speed → the line narrows → you carve into a new heading off a
banked wall → you cut a sliceable enemy at the apex → the kill surges time/speed
into a bhop landing, and the next descent begins.)

Everything in this doc serves making that loop **readable at speed**,
**replayable**, and **addictive**. The three pillars the whole design optimises:

1. **Never force a full stop.** No beat in any generated map may require zero
   velocity to clear. (Titanfall's continuous-mobility invariant.)
2. **The kill is a consequence of moving, not a reason to stop.** (DOOM Eternal
   push-forward; Neon White enemies-as-waypoints.)
3. **The dopamine is the discovered millisecond.** Deterministic-per-seed
   courses you route-learn; ghost-by-default racing; ms-resolution scoring that
   always rewards the fast line. (Trackmania + Neon White.)

Where we already are: the surf core loop ([SurfLine](../tools/strafegen/strafegen.py)),
the lap→points→shop→again cycle, the ghost system, sword bullet-time slowmo,
slice-drone gates, the LATTICE mode, and the dev-texture readability palette all
exist. Where we're going: turning the *generator* from "valid + varied geometry"
into "geometry that obeys the seven principles below."

---

## 1. Enemy placement — kills reinforce movement

The slice-drone ([`Course.place_drone`](../tools/strafegen/strafegen.py),
[`--combat`](../tools/strafegen/strafegen.py)) is the unit here. A drone is a
**gate on the line you cut THROUGH**, not a wall beside it — an un-sliced gate
already passes clean (it doesn't break the line). The katana slice grants a
time-dilation surge (`g_timeBind`, the sword-slowmo identity). That's the fusion
working. The placement rules:

- **[x] Slice grants momentum/time back.** Already true — a swing surges time
  out of the near-freeze floor (`g_timeBindMin`). _Keep this invariant: never
  add an enemy that requires braking. If a kill would need a stop, convert it to
  a parry/deflect the player passes through, or cut it._
- **[ ] Kill lands AT the action beat, place enemy before the corner.** The
  slice should resolve as you carve off the revector wall, so the surge feeds
  the release/bhop. **(derived)** parameter:
  `enemy_offset = corner_position − (jump_distance × 0.7)`. Today `place_drone`
  drops on the line at a `fwd`/`side` offset; the generator should compute that
  offset *relative to the next revector wall*, not free-place it.
- **[ ] Telegraph: face the player, silhouette against contrast, ≥1.5–2 s of
  travel ahead.** **(derived)** Spawn every on-line drone facing the player,
  lit, on a contrasting backdrop, far enough out to be parsed and pre-aimed at
  current speed. Routing happens *before* arrival (Neon White / Ghostrunner
  tactical overlay).
- **[~] Pulse, don't carpet.** Memory
  [strafegen-combat-recipe](../../.claude/projects/-Users-gustav-strafe64-engine/memory/strafegen-combat-recipe.md)
  already lays "2-3 enemy phrases at spice beats; openers/flow = rest." Formalise
  as: **one kill-beat every ~2–4 s of flow, clustered into 2–3-drone phrases
  separated by pure-movement rests. Never >3 simultaneous on-line threats** —
  that forces a stop. **(derived from DOOM wave pacing + Neon White brevity.)**
- **[ ] Rest-beat enemies advance.** Any drone at a rest beat should drift toward
  the line so standing still decays (Titanfall anti-camping).

---

## 2. Corners & bounce geometry — the revector wall

This is where surf/Defrag and Trackmania give us hard, reusable numbers.

- **[x] Surfable bank angle.** **(physics)** A face must be **> 45°** (steeper
  than `MIN_WALK_NORMAL`) to surf — too steep to stand, so you slide and
  air-accel along it. `SurfLine` already lays ramps in the **49–55°** band; the
  community 5:4 rise/run ≈ **51.3°** is the sweet spot. **Keep revector-wall
  pitch in 51–58°** (steeper = harder/slower to read).
- **[ ] Bend ~30–45° per revector, never 90°.** **(physics + folk-canon)** The
  Quake/Source air-accel model only adds speed from the projection of velocity
  onto wish-dir. A hard 90° corner *kills* strafe speed; a series of 30–45°
  kinks reads as a carve and *rewards* carried speed. **Generator rule:
  consecutive line segments bend by 30–45°, chained — not one big turn.**
  `Course.turn()` currently takes arbitrary yaw; constrain the revector emitter
  to the 30–45° band.
- **[ ] Late-apex exit into a straight.** **(Trackmania, derived)** When a
  revector wall is followed by a release/bhop straight, bias the wall so the
  natural exit vector points straight down the next segment — place the wall's
  far edge for a late apex (straighter/faster exit), not a tight clip.
- **[ ] Run-up before every wall.** **(derived)** Precede each revector wall with
  a straight run-up ≥ **1.5×** the player's per-second travel, so the wall is
  visible and the entry angle settable before contact.
- **[~] Banked turns / true 2D circuits.** `SurfTurn` builds a steep banked 180°
  arc (velodrome inner-low/outer-high). If the bank carries speed (human-gated
  feel call, see ROADMAP), fold `arc()` into `SurfLine` so circuits curve in 2D
  instead of snaking one Y-corridor.

> See also memory
> [strafe64-bounce-corner](../../.claude/projects/-Users-gustav-strafe64-engine/memory/strafe64-bounce-corner.md):
> corners = 2×45° bounce-walls on a flat pad; do **not** re-add the velodrome
> floor-bank (it read wrong-way + cost flow).

---

## 3. Verticality — Z donates speed

- **[~] Descent-dominant net elevation.** **(Titanfall)** The *descend* phase is
  a controlled downhill that donates speed (gravity feeds the bhop); *tighten/
  revector* spends it going slightly up or across. **Net elevation per segment
  trends downward** so momentum is always being replenished. `SurfLine` already
  descends ~1792u over its chain; generalise the rule to all course types.
- **[ ] Never hide the landing.** **(Ghostrunner)** Any drop must show its target
  platform / next wall inside the player's forward cone *before* commitment.
  Frame the descent so the next anchor is in-frame at jump-off.
- **[ ] Wallrun arc as connective tissue, gap ≤ max-jump.** **(Titanfall)**
  Wallrun speed builds over time, so the optimal exit is near the run's end —
  put the next anchor there. **Wallrun-to-next-anchor gap ≤ the distance a
  max-speed jump covers**, so "always a path" holds. (Wallrun + `PM_CheckVault`
  mantle exist; the generator doesn't yet *place* wallable surfaces to this
  rule.)

---

## 4. Multiple lines — safe vs fast

- **[~] Safe line + fast line per segment.** **(Neon White gold vs ACE)** Every
  segment ships (a) a **safe line** always completable at moderate speed = gold
  pace, and (b) a **fast line** — a tighter revector, a skip, a sword/rocket-jump
  shortcut — higher-risk, only pays at ACE pace. `Course` has `fork` already;
  the spec is to make one branch *honestly faster* and one *safe*, not just two
  cosmetic paths.
- **[ ] One speed-gated skip per ~2–3 segments.** **(Defrag overbounce-as-content)**
  Intentionally place a skip that bypasses a chunk of the safe line **if the
  player carries enough speed** — gate on a velocity threshold, not a switch.
- **[ ] Forks diverge in a shallow Y, never a T.** **(Mirror's Edge)** Both
  branches keep heading down-line; a route choice never forces a reversal or a
  stop. **Max 2–3 simultaneous lines** — more is unreadable at flow speed.

---

## 5. Readability at speed — the Mirror's Edge rules

These are directly implementable as generator + shader constraints. Our
Source/dev-texture palette (orange floors, grey walls, subtle measure grid — see
memory
[strafe64-source-dev-textures](../../.claude/projects/-Users-gustav-strafe64-engine/memory/strafe64-source-dev-textures.md))
deliberately leaves a clean accent slot for exactly this.

- **[ ] One reserved guidance hue for "the line."** **(Mirror's Edge Runner's
  Vision — red was chosen so players can judge distance)** Reserve **exactly one
  accent hue** for the on-line chain: next anchor, revector wall, on-line
  sliceable enemy. **Forbid that hue everywhere else.** This is the single
  highest-leverage readability change for a proc-gen map.
- **[ ] Light = go, dark = stop.** Light the on-line anchor chain; let off-line
  geometry fall darker. (Generator drives lightgrid / shader emissive on anchors.)
- **[ ] Next anchor within ±30° forward FOV on entry.** When a beat resolves, the
  next point of interest is already in the player's forward cone. Keep momentum
  directional — exit on the same side you entered.
- **[ ] Distinct silhouette + guidance-hue outline per on-line enemy.** Drones
  recognisable at distance so routing happens before arrival; outline on-line
  drones in the guidance hue (Ghostrunner tactical overlay).
- **[ ] Cause and effect in-frame.** A trigger and its result stay in the same
  shot — the player never hunts for what changed.

---

## 6. Rhythm, pacing, escalation

- **[~] teach → combine → test.** **(Ghostrunner three-beat)** Each course:
  segment 1 introduces one new mechanic in isolation at low stakes; middle
  segments combine it with priors; the final segment demands the full chain. The
  existing course recipe (memory
  [strafegen-clustertruck-sections](../../.claude/projects/-Users-gustav-strafe64-engine/memory/strafegen-clustertruck-sections.md):
  speed→flow→spice arc) is the skeleton — escalate *difficulty of the same
  mechanic*, not just variety.
- **[ ] Mandatory rest beats.** **(derived)** A pure-movement rest (no enemies,
  wide line) every **~10–15 s** of intensity; rest length ≈ **30–40%** of the
  preceding intensity block.
- **[ ] Short trials.** **(Neon White — seconds, not minutes)** Target a full
  flow-trial of **~20–60 s**; a single phrase (descend→…→release) **~4–8 s**.
- **[~] Checkpoint before each phrase, instant restart.** **(Ghostrunner cheap
  death)** `Course.checkpoints` already places one respawn per section; the rule
  is **checkpoint before each new mechanic-phrase** with near-zero
  restart-to-control latency (Q3 fire-to-respawn is already near-instant).
- **[x] Never emit a zero-velocity beat.** The generator must never produce a
  gate, valve, or lock that requires standing still. (Currently honoured by
  construction — gaps assume carried speed; keep it an explicit invariant.)

---

## 7. What makes it addictive — the meta loop

This is the layer that turns "a good course" into "one more run."

- **[x] Ghost system.** Record/replay on scaled `cg.time`, finish on real
  `trap_Milliseconds()` (no slow-mo cheat); cyan hologram + trail ribbon. Exists
  (memory
  [strafe64-ghost](../../.claude/projects/-Users-gustav-strafe64-engine/memory/strafe64-ghost.md)).
  **[ ] Make racing your own PB ghost the DEFAULT on restart** — biggest "one
  more run" driver across Trackmania + Neon White.
- **[ ] Tiered, gated leaderboard reveal.** **(Neon White's key trick)** Casual
  play reliably nets gold; the **global** leaderboard only unlocks after you ACE
  a course. Show your own / medal target first → friends → global as you clear
  thresholds. Protects beginners from demoralising times; gates the chase behind
  mastery.
- **[~] High-resolution (ms) scoring that always rewards the fast line.** The
  per-lap payout (lapBonus + speedBonus + bestBonus, PERS_SCORE) and the
  par/medal system (`par_calibrate.py`, `cgp_<map>`, MISSION REPORT S/A/B/C rank)
  exist. **The invariant to enforce: taking the fast line must always out-score
  the safe line.** Score the awake/speed metric continuously at ms resolution; a
  new PB should be celebrated loudly.
- **[ ] Deterministic per seed.** **(Neon White "think three moves ahead")**
  Courses must be **memorisable** — fully determined by their seed.
  **Randomise the seed, not the run.** strafegen is already seed-driven
  (`--daily` = date-seeded, same course worldwide each day); make seed→geometry
  *stable* across generator versions for any seed surfaced to players.
- **[ ] Mastery curve = assistance that fades.** Guidance-hue intensity becomes a
  difficulty/assist slider; the scoring *rewards* turning it down (Mirror's Edge
  Runner's Vision dials off as you improve).

---

## 8. The map archetypes — current vs. target

| Archetype | Class / flag | Status | Direction |
|---|---|---|---|
| **Surf circuit** (the core loop) | `SurfLine` / `--surf` | [~] procedural chain, feel-gated | Fold `SurfTurn.arc()` in for true 2D circuits; apply §2–§3 angle/run-up/late-apex rules |
| **Banked turn** | `SurfTurn` / `--surfturn` | [~] 180° arc, no AAS | Validate the bank carries speed; merge into SurfLine |
| **Flow course** | `Course` + `sec_*` / default | [~] section recipe | Re-author sections to §1–§6: slice-at-apex, 30–45° kinks, rest beats, teach→combine→test |
| **Combat-laced** | `Course(combat=True)` / `--combat` | [~] slice-gate phrases | Enforce enemy-offset = corner − 0.7×jump; pulse density |
| **Arena / Pit** | `Arena`, `Pit` / `--arena` | [x] bot-fight pit | Push-forward combat; fight-at-speed boost pads |
| **Killbox** | `Killbox` / `--killbox` | [x] vertical melee + momentum portals | Reuse momentum-portal trick (dest angles[0]>999999 skips velocity reset) |
| **Lattice arena** | `LatticeArena` / `--latticearena` | [x] sealed, 24 spawns, AAS | ≥16 spread spawns to kill telefrag noise; escalate by collapse-rate |
| **Dojo (bot regression)** | `--dojo speed/flow/ztrick/arena/surf` | [x] regression-guarded | Calibration substrate, not a player map |

---

## 9. Top 10 concrete next changes (ranked by impact on addictiveness)

1. **Ghost + your-own-PB race-by-default on restart.** _[~] ghost exists; make
   PB the default opponent._ (§7)
2. **Tiered leaderboard reveal — gold = obvious line, ACE unlocks global.** _[ ]_ (§7)
3. **Every drone a gate killed at the revector apex that returns speed/time.**
   `enemy_offset = corner − 0.7×jump_dist`. _[ ] generator placement._ (§1)
4. **Replace 90° corners with chained 30–45° kinks at 51–58° wall pitch.** _[ ]
   constrain `Course.turn` + revector emitter._ (§2)
5. **One reserved guidance hue for the on-line chain; light=go/dark=stop; next
   anchor within ±30° FOV.** _[ ] shader + generator._ (§5)
6. **Deterministic-per-seed, stable across versions — randomise the seed, not the
   run.** _[ ] lock seed→geometry._ (§7)
7. **ms "awake/speed" scoring that always rewards the fast line over the safe
   line.** _[~] payout + par exist; enforce the invariant._ (§7)
8. **Safe-vs-fast fork per segment + one speed-gated skip per 2–3 segments,
   shallow Y not a T.** _[~] `fork` exists; make branches honestly safe/fast._ (§4)
9. **teach→combine→test escalation + rest beats every 10–15 s + ~20–60 s trials +
   checkpoint per phrase.** _[~] recipe + checkpoints exist; pace them._ (§6)
10. **"Always a path": descent-dominant elevation, wallrun-gap ≤ max-jump, no
    zero-velocity beat.** _[~] surf descends; generalise the invariant._ (§3)

---

## 10. Open questions / human-gated calls

These can't be measured headlessly (bots can't surf-finish, parry, or feel) —
they need a `\map` playtest:

- The exact revector bend band: is 30° or 45° the carry-speed sweet spot for our
  air-accel constants? (Sweep `pm_airaccelerate` × bend angle.)
- Does `SurfTurn`'s bank carry speed around the arc, or bleed it? (Decides §2's
  2D-circuit merge.)
- `g_bulletSpeed` feel (~0.3–0.5 recommended) so deflectable shots read at normal
  time, co-tuned with the parry window.
- Lattice `g_latticeRadius` (~120–160) + `g_latticeSelfMs` (~900–1200) defaults.
- Guidance-hue intensity default + whether the assist slider should be
  score-rewarded or purely cosmetic.

---

### Source principles
Synthesised from: Neon White (enemies-as-waypoints, gated leaderboard, the
discovered ms), Titanfall 2 (continuous-mobility, wallrun-builds-speed,
rush-the-player), Ghostrunner (verticality + loops, teach/combine/test,
checkpoint-before-zone), Mirror's Edge (one guidance colour, light=go, frame the
next objective), Quake/Defrag/surf (>45° ramp, 5:4 ≈ 51.3°, air-accel projection,
overbounce-as-content), Trackmania (late apex, author/PB ghost), Clustertruck
(momentum-through-hazards), DOOM Eternal (push-forward combat, pulse-don't-carpet).


## Nocturne Flow prototype — 2026-11-09

Separate map: `strafe64_nocturne_flow`, generated by
`tools/strafegen/strafegen_nocturne_flow.py`. Classic Nocturne is preserved.
The new PK3 uses the installed shared marble/night materials and needs no
external Nocturne-Reservoir pack. Do not replace the classic map to try this.

### Research and decisions

- OpenArena's [mapping manual](https://openarena.fandom.com/wiki/Mapping_manual/Hint_brushes)
  and [bot guide](https://openarena.fandom.com/wiki/Mapping_manual/Bot_play)
  distinguish visibility subdivision from bot navigation and emphasize simplifying
  collision. Flow uses sealed brush geometry, full VIS, and a separately compiled
  AAS. Avoid adding hint planes indiscriminately: useful sightline blockers and
  connected routes come first. The Fandom pages were search-index accessible;
  direct full-page retrieval was unavailable during this research.
- Titanfall 2 designer Griffin Dean describes the pressure caused by unpredictable
  encounters in [Bounty Hunt's design](https://www.griffindean.com/tf2-multiplayer.html).
  His [Live Fire breakdown](https://www.griffindean.com/live-fire.html) explains why
  compact high-mobility maps need careful spawn space and objective pressure.
  Our application: discrete courts to focus fights, covered approaches and
  alternate routes, rather than random obstacles across a single exposed plaza.
- The traversal layout is our adaptation, not a recreation of a Titanfall map:
  continuous flat wall faces, low 40-unit vault cover, stairs to 128-unit galleries,
  224-unit gallery headroom, roof routes and wide ground-level cross-connections.

### Built layout

3200 x 3600 playable footprint, mirrored ends, ten validated spawns, two raised
flank galleries, covered pockets, central sightline screens and three marked
courts A/B/C. Ground routes connect without requiring advanced movement.
A/C sit at y=-1040/+1040; B is at the center. Stair access keeps high routes
available to bots. Static low trim avoids cladding snag points.

`exec nocturne_flow_tdm` starts a local 3v3 (pilot plus five bots), 50-frag /
10-minute team deathmatch with smooth local time. It uses mixed weapons; forcing
sword-only with ranged pickups caused repeated failed pickup attempts in the
first smoke, so that combination is excluded from this preset. TDM currently
uses the existing distributed deathmatch spawn selection, not locked team bases.
For network hosting use legacy timing explicitly; smooth time is local-only.

### Conquest direction approved by the user

Combine kills with held-point scoring. Proposed first tuning: 2 points per kill,
1 point per owned objective every 2 world-seconds, 200 points to win. World-time
capture and payout clocks keep slow motion from becoming a scoring exploit.
Three courts permit split pressure; B must have approaches from both flanks.

This is a design proposal, NOT implemented conquest. Required next work:
server-authoritative ownership, uncontested capture progress, contested freeze,
team-safe spawn selection, objective-aware bots, score/ownership replication,
world-space point markers (compatible with minimal HUD), overtime/tie rules and
regression cases for deaths, simultaneous capture, neutralization and map restart.
Kills alone must remain viable without making objective control irrelevant.

### Build and verification

Windows compiler: official NetRadiant-custom 20251023 release, q3map2 SHA256
`1ADB20EF8A52DF583EEA00C43BC163F38CF644C8B4E173FB9BA768170B64DCB2`.
Downloaded compiler is under ignored `engine/build/map-tools/`; it is not a
shipping dependency. Supply its executable via `Q3MAP2`, then run:

```powershell
$env:Q3MAP2="<path-to-q3map2.exe>"
py -3.13 tools/strafegen/strafegen_nocturne_flow.py
```

Generated `.map`, BSP/VIS/light logs, package and `metrics.json` are under
`tools/strafegen/generated/nocturne_flow/`. The bake's VIS option is opt-in;
other map generators keep their previous bake behavior. A missing AAS or VIS
is an error for this generator rather than an accepted incomplete package.

Measured structure: Flow has 91 brushes / 289 draw surfaces, classic has 87 /
368. Opt-in compiler META merging reduces surfaces despite the extra cover. Flow BSP
is 1,136,564 bytes versus 2,204,092; its 316-cluster VIS table is 12,648 bytes.
The mean set of visible clusters is 74.19% (not a frame-time speedup claim).
Classic has no VIS lump. Flow uses 16-unit lightmap samples and one bounce.
The static-camera GPU comparison was inconclusive: depth-of-field timing differed
between runs despite matching requested settings. No FPS improvement is established.

Four new source-layout tests cover spawn hulls, clear objective courts, connected
walking routes and absence of external/mirror material dependencies. Full suite:
195 native + 54 Python checks. Two isolated TDM runs passed startup/render/exit
checks with five bots. The longer run recorded movement, wall jumps/runs, pickups
and a kill, but also up to 5.32 seconds of low-speed/stuck telemetry for one bot
in 43.14 world-seconds; further navigation tuning and spawn balance remain open.
Logs: `engine/build/nocturne-flow-build.log`, `nocturne-flow-tests.log`, and
`polish-nocturne-flow-*` smoke folders. This is a playtest candidate, not a final
competitive or performance-certified map.

Authored bot movement routes and their debug graph are documented in
[BOT_TRAVERSAL.md](BOT_TRAVERSAL.md). The graph is an experimental navigation
layer, separate from conquest rules.
