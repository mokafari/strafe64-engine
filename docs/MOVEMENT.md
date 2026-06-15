# Total Movement Mod

A movement overhaul of vanilla Quake III, implemented entirely inside the
shared player movement code so client prediction and server simulation stay
identical. Vanilla strafe jumping is untouched — everything below stacks on
top of it.

All changes live in [bg_pmove.c](../engine/code/game/bg_pmove.c) (tunables at the top of
the file), with externs in [bg_local.h](../engine/code/game/bg_local.h) and four new
`stats[]` slots in [bg_public.h](../engine/code/game/bg_public.h).

## The kit

**Auto bunny hop with chain bonus.** Jump can be held: you rehop on the exact
landing frame with zero friction loss. Each chained hop (≤100 ms on the
ground) multiplies horizontal speed — ×1.05 on the first chained hop, +1% per
consecutive hop, capped at ×1.10. Standing on the ground longer than the
window breaks the streak.

**CPM-style air control.**
- A/D-only strafing uses accel 70 against a 30 ups wishspeed cap — sharp
  air direction changes.
- W-only input curves velocity toward the view direction without changing
  speed (aircontrol 150).
- Pushing against your velocity decelerates at 2.5 instead of 1.0 (air stop).
- Diagonal input is untouched, so classic strafe jumping works exactly as in
  vanilla Q3.

**Double jump.** A fresh jump press in mid-air (away from walls) gives one
extra jump per airtime, refunded on landing. Falling speed is cancelled and
half of any rising speed carries over, so jumping early in the arc goes
higher than jumping at the apex.

**Stair jump.** Rejumping within 400 ms of the previous jump gets +75 jump
velocity (270 → 345) — CPM style. The window is shorter than a full flat-
ground jump arc on purpose: it only triggers on quick landings, i.e. stairs,
ramps, and uphill hops.

**Crouch slide.** Ducking above 250 ups cuts ground friction to 15%. Jumping
out of a slide kicks horizontal speed ×1.08. Duck no longer doubles fall
damage, since landing into a slide is the intended play. A wind-rush hiss
loops while you slide ([g_active.c](../engine/code/game/g_active.c), `G_SetClientSound`)
— grounded-gated, so it sustains on real slides and stays silent through
airborne bhops.

**Wall jump.** A fresh jump press (not held) while airborne next to a wall
kicks you 200 ups away from it and to at least 250 ups vertical, preserving
tangential speed. Two walljumps allowed between groundings; touching the
ground refunds them.

## Combat layer (speed = damage)

Server-side game code ([g_combat.c](../engine/code/game/g_combat.c),
[g_active.c](../engine/code/game/g_active.c)), all toggleable via cvars registered in
[g_main.c](../engine/code/game/g_main.c):

| Cvar | Default | Effect |
|---|---|---|
| `g_speedDamage` | 1 | Attacker speed scales damage dealt: 1.0× at run speed (`g_speed`), +50% per extra run speed of velocity, 0.85× floor when near-still |
| `g_speedDamageMax` | 2.0 | Cap on the speed multiplier |
| `g_selfDamageScale` | 0.25 | Self-damage scale — knockback untouched, so rocket jumps keep full kick at quarter cost |
| `g_killReward` | 1 | +25 health / +10 armor per frag (capped at 2× max health) |
| `g_ammoRegen` | 1 | Ammo per second regenerated for every owned weapon, up to 50 |

Weapon switching is also twice as fast (100 ms drop/raise instead of
200/250, in `PM_Weapon`).

At 640 ups a rail hits for 150; at 960+ it's a 200-damage one-shot. Standing
still it's 85. Move or die.

## Pressure layer (the floor is never safe)

| Cvar | Default | Effect |
|---|---|---|
| `g_hotFloor` | 1 | Near-still on the ground for 2 s flashes "MOVE!", then burns 5/10/15/20 health per second (armor doesn't help). Obituary: "was in the wrong place" |
| `g_flowRegen` | 3 | Sustained flow (≥500 ups, the milestone threshold) heals this many HP/sec up to max health — the reward inverse of the hot floor. Mutually exclusive with it by speed. Never overheals |
| `g_speedKnockback` | 1 | Attacker speed scales knockback with the damage curve (cap raised 200→280); self-knockback unscaled so rocket jumps stay consistent |
| `g_grapple` | 1 | Everyone spawns with the grappling hook (weapon slot 10). Hook flies at 1500 ups (was 800). Latches with a metallic clank from the impact point. **Pendulum swing + reel**: the rope length is seized on attach (`STAT_GRAPPLE_LEN`) and acts as a one-sided constraint — when taut it cancels only outward velocity and never adds inward speed, so it holds you at radius and lets gravity swing you along the arc. **Jump reels in** (shortens the rope at `pm_grappleReel` = 900 ups → smooth inward speed: wind up a swing, or rip straight up to a ceiling anchor); **crouch reels out** (drop). Slack inside the radius (free fall until taut); air-control pumps the arc; release at the bottom to fling out carrying all your built speed. While grappling, jump reels instead of air/wall-jumping. Composes with the chain |

Speed feedback escalates too: crossing each 100 ups above 500 for the first
time **that life** plays an announcer reward (impressive → excellent →
holy shit at 1000+) with a centerprint, and the bhop pop ladder rises in
tone at streaks 5 and 9. The milestone tier and flow latch reset on every
respawn ([cg_playerstate.c](../engine/code/cgame/cg_playerstate.c) `CG_Respawn`), so
each run celebrates its own peak — fitting the per-run speedrun framing.

**Speed ladder (canonical thresholds).** The systems share one ladder so
they stay coherent: 320 ups = run speed (1.0× damage, flow 0); 500 = flow
entry (surge sound + FOV punch, regen on, vignette begins); 640 = lethal
hits (1.5×, hit marker turns red); 768 = peak flow (vignette/speedometer
red); 960 = cap (2.0× damage, `CG_Flow` = 1, max FOV). Keep these aligned
when tuning — the surge, regen, HUD, and wash all key off them.

**Known limitation — warmup.** Hot floor and the void key off `pm_type` /
ground state, not match phase, so with `g_doWarmup 1` (off by default) they
would punish players during the pre-match countdown. The `idleSeconds` grace
*is* cleared on respawn (the `ClientSpawn` memset), so per-life behaviour is
correct. Before competitive/tournament play, gate both on the match being
live — but note stock Q3's `level.warmupTime` state machine is subtle for
FFA, so verify the gate doesn't disable them in normal play.

## Feel layer (the camera is kinetic)

- **Landing dip** ([cg_playerstate.c](../engine/code/cgame/cg_playerstate.c)): every
  ground contact feeds the fall-deflect view machinery a subtle impact-scaled
  dip (~1–3.6 u), so bhop landings have a weighted footfall in sync with the
  streak pop. Capped under the hard-fall range and set before the event pass,
  so a real `EV_FALL` still overrides with its bigger dip.
- **Air-strafe lean** ([cg_view.c](../engine/code/cgame/cg_view.c)): carving through
  the air banks the camera into the turn, proportional to lateral speed and
  capped at 8°, eased so takeoff and landing roll smoothly to and from level.
  Only airborne — ground movement stays calm. Sits on top of the wallrun
  lean and the subtle always-on `cg_runroll`, all sharing the same direction.
- **Flow-surge FOV punch** ([cg_view.c](../engine/code/cgame/cg_view.c)): a quick +6°
  kick decaying over 300 ms at the instant you break into flow (≥500 ups),
  so the surge sound lands with a physical snap-wider on top of the
  continuous speed widen. The flow-entry moment now fires across three
  senses at once — sound (surge), sight (vignette/milestone), and feel (FOV).

## Feedback layer

- **Speedometer HUD** ([cg_draw.c](../engine/code/cgame/cg_draw.c)): current ups (`VEL`),
  peak this run over the map record to beat (`PK 612/840`), the live damage
  multiplier (`SYNC`, mirrors the server curve), and hop streak as current>peak
  (`HOP 3>9`), centered under the crosshair in the NERV amber→red palette. The
  record is shown only once set, so the target is visible during the run.
  Per-run peak and streak reset each life with the milestones. Toggle with
  `cg_drawSpeed`.
- **Per-map speed record** ([cg_playerstate.c](../engine/code/cgame/cg_playerstate.c)):
  dying ends the run; if this life's peak speed beat *this map's* all-time
  best, a "NEW RECORD  840 UPS" centerprint + announcer fires and the record
  saves to a per-map archived cvar (`cgr_<map>`, registered in
  [cg_main.c](../engine/code/cgame/cg_main.c) at map load) — so a daily-speedrun PB
  survives quitting and each map keeps its own (a fast open map can't make a
  tight technical map's record unbeatable). Only on a genuine record above
  flow speed, so it self-limits to rare real PBs and reframes death as a
  run-end scorecard.
- **Dynamic FOV** ([cg_view.c](../engine/code/cgame/cg_view.c)): up to +15° as speed
  builds past run speed. Toggle with `cg_speedFov`.
- **Speed-tinted hit marker** ([cg_draw.c](../engine/code/cgame/cg_draw.c)): a brief
  "+"-bracket confirmation when you damage someone — baseq3 never had a hit
  marker. Amber normally, alert-red when you connect at ≥640 ups (the 1.5×
  lethal multiplier), so speed = damage is legible at the point of aim.
- **Directional damage indicator** ([cg_draw.c](../engine/code/cgame/cg_draw.c)): a
  red arc on a ring around the crosshair pointing toward whoever just hit
  you, fading over 1 s — baseq3 only flashed the whole screen red. Knowing
  where to turn is knowing where to keep moving. Reuses the networked
  `ps->damageYaw`, so no protocol change.
- **Speed vignette** ([cg_draw.c](../engine/code/cgame/cg_draw.c)): amber side-glows
  bloom in from the screen edges as the speed-damage multiplier climbs,
  deepening to alert-red at the 2× cap — the ambient, peripheral counterpart
  to the speedometer number. Shares the NERV palette and the `cg_drawSpeed`
  toggle.
- **Bhop streak pop** ([cg_playerstate.c](../engine/code/cgame/cg_playerstate.c)):
  a tick sound on every chained hop from streak 2 up — the pulse of the run.
- **Frag-heal flash** ([cg_draw.c](../engine/code/cgame/cg_draw.c)): a brief amber
  bloom over the scene when a kill heals you, so the `g_killReward` health/
  armor gain reads as a surge of life rather than a silent stat bump.
  Detected client-side from a `PERS_SCORE` increase — no protocol change.
- **Flow-entry surge** ([cg_playerstate.c](../engine/code/cgame/cg_playerstate.c)): a
  warp-whoosh the moment you cross into flow speed (≥500 ups) — the single
  threshold where regen, the vignette, and the first milestone all switch on.
  Hysteresis (latch at 500, release below 440) stops it retriggering while
  hovering at the edge.
- **Speed = accuracy** ([g_weapon.c](../engine/code/game/g_weapon.c)): machinegun
  spread tightens proportionally above run speed (bullets only — the
  shotgun pattern is recomputed client-side from a seed and would desync).
- **Midair rockets** ([g_missile.c](../engine/code/game/g_missile.c)): direct rocket
  hits on airborne players deal 1.5× and flash "MIDAIR!" to the attacker.
- **More gibs**: overkill threshold raised from -40 to -15 health
  (`GIB_HEALTH` in [bg_public.h](../engine/code/game/bg_public.h)).

## The loop

```
strafe jump → land ducked (slide, no friction) → slide jump (×1.08)
  → chained hop (×1.05+) → air control around the corner
  → wall jump off the pillar → double jump window still open → repeat
```

## Race layer (the game around the movement)

The mod is now a daily speedrun game, not just a moveset. Four systems,
all in the same game/cgame pair:

**Race timer** ([g_trigger.c](../engine/code/game/g_trigger.c)): strafegen places
`trigger_race_start` / `trigger_race_finish` volumes. Defrag-style
timing — the start pad restamps on every touch, so the clock starts the
moment you leave it. Finish reports the time (centerprint + `NEW BEST`),
best-per-session survives death. The stamp is mirrored to the client in
`STAT_RACE_START` (deciseconds) for the live HUD clock in
[cg_draw.c](../engine/code/cgame/cg_draw.c).

**Rising void** ([g_main.c](../engine/code/game/g_main.c) `G_RunVoid`): a kill
plane climbs from below the course on a pace schedule read from
worldspawn keys (`voidbase` / `voidrise` / `voiddelay`, written by
strafegen). Fall behind the pace and the world erases you
(`MOD_FALLING`, always gibs). `g_voidRise` scales the rate; 0 disables
it for practice. The client gets the schedule once via the
`CS_VOIDINFO` configstring and integrates the plane height locally —
drawn as a translucent animated plane (`strafe64/void` shader) plus a
flashing `VOID <distance>` HUD warning.

**Ghosts** ([cg_view.c](../engine/code/cgame/cg_view.c)): pure cgame, no
protocol. Your run is sampled at 20 Hz while the race stat is live;
beating your best saves `ghosts/<map>.gho` and the next run replays it
as a translucent player model pacing you. Dying discards the run.
`cg_ghost 0` hides the replay.

**Vectorgun** (`g_vectorgun 1`, latched): one gun. Railgun only,
infinite ammo, no weapon/ammo pickups spawn. Damage and spread already
scale with speed; in this mode the *fire rate* does too — 1500 ms at a
standstill down to 450 ms at 960+ ups, predicted client-side through a
`pmove` flag mirrored from serverinfo. Movement is the only economy.

## State & networking

Mod state is stored in six new `playerState_t->stats[]` slots
(`STAT_BHOP_STREAK`, `STAT_GROUND_MS`, `STAT_JUMP_MS`,
`STAT_WALLJUMP_COUNT`, `STAT_AIRJUMP_COUNT`, `STAT_RACE_START`), so it
is networked and predicted like any other player state — no protocol
changes, no prediction drift. 14 of the 16 available stat slots are used
including MISSIONPACK. The rising void uses one new configstring slot
(`CS_VOIDINFO`, set once at spawn — never per-frame).

## Tuning

Every gameplay/feedback cvar is gathered, annotated, and set to its canonical
value in one place: [strafe64.cfg](../tools/strafegen/strafe64.cfg). The launcher stages
it into `baseoa`, so `/exec strafe64` in-game resets the full ruleset after
experimenting. It's the single knob-board for the combat, survival, movement,
and HUD cvars (the physics constants themselves are not cvars — see below).

The physics constants are in the "movement mod parameters" block at the top of
[bg_pmove.c:48](../engine/code/game/bg_pmove.c:48). They are tuned together — the bhop
window, boost curve, and double-jump window define the rhythm; touching one
cascades.

Rebuild after changing anything (`./scripts/build.sh`) — the file compiles
into both the `qagame` and `cgame` dylibs, and they must match or prediction
will stutter.

## Running on macOS (Apple Silicon)

This tree is self-contained: the ioquake3 engine and this mod build together,
and the OpenArena 0.8.8 free assets are bundled under `assets/openarena/`
(see [STATE.md](STATE.md) for the full layout).

```sh
./scripts/build.sh      # cmake + ninja, arm64 Release -> engine/build/Release
./scripts/run.sh        # -f fullscreen, -b [n] [map] bots, -d <map> dedicated
```

[run.sh](../scripts/run.sh) deploys the modded `qagame`/`cgame`/`ui` dylibs
into `baseoa/` (re-signed for Apple Silicon dlopen — never deploy one alone,
they share networked headers) and forces `vm_game 0`, `vm_cgame 0`,
`vm_ui 0`, `sv_pure 0` so the engine loads the native dylibs (including the
STRAFE 64 menu) instead of OpenArena's stock QVMs.

After tuning movement constants just rerun `./scripts/build.sh`; `run.sh`
re-deploys the rebuilt dylibs on the next launch.
