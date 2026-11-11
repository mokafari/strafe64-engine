# Gameplay test coverage audit — 2026-11-04

This is a behavioural coverage inventory from the test targets and production
code, not a measured line/branch coverage percentage.

## Current automated baseline

`scripts/test.ps1`: **195 native CTest cases + 54 Python checks**.
The Python engine-API checks exercise parsing/configuration logic without a
running game. They are not 40 additional live gameplay scenarios.

| Area | Covered | Important limits |
| --- | --- | --- |
| Movement (94 cases) | Acceleration, braking, diagonal speed, held jump, air-jump limit, landing/refunds, blocked standing, wall sliding, corners, coyote window/expiry, slide entry/exit, terminal velocity, slope walking, coyote head-bump/held-input/spawn/rearm cases, finite ledge walk-off/idle, held bunnyhops, wall jumps/limits, wall-run detach, low/high steps and clear/blocked vaults, water depth/swimming/sinking/exit, water-jump clear/blocked lip and falling release, wall-run exhaustion/reattachment and encoded presentation state; each at 1x and 0.04x | Full production pmove and slide-move with synthetic swept-box/plane traces; experimental fractional path, not BSP collision or legacy prediction replay |
| Bot traversal (14 cases) | Production route controller and steering through pmove: jump, wall kick, held input, timeout, death, missing wall; normal/slow steps | Authored connections, not global route learning; see [traversal notes](BOT_TRAVERSAL.md) for live evidence and limits |
| Slow-motion clock/movement | Fractional integration, clock ownership/reset/overload, accumulation at several frame rates, sub-ms movement, gravity, timers, responsive angles | Render-schedule clock checks do not replay a full real input stream through the host/server/cgame loop |
| Sword (27 cases) | Reach, wall obstruction, corpse handling, combo dispatch, lunge selection, normal/slow swing and buffering | Damage is stubbed in the sword harness; a separate combat harness now tests damage resolution |
| Damage resolution (16 cases) | Real G_Damage and CheckArmor: clean/glancing/wrong-line parries, raise/break timing boundaries, rear hits, riposte/stagger, armor/bypass, friendly fire, lethal dispatch and knockback | Death callback and effects are stubbed; not full weapon/projectile-to-death integration |
| Projectile impacts (8 cases) | Real G_MissileImpact, G_DeflectMissile and G_Damage: frontal/rear hits, guard readiness, reflection origin, visible/occluded return targets and returned damage ownership | Controlled trace services; no full projectile flight, splash/bounce or BSP integration |
| Movers (7 cases) | Real rider translation/rotation, blocked rotation fallback, stop-on-contact rider/nonrider rules, two-rider carry and transaction rollback through G_MoverTeam | Position obstruction and entity queries are controlled by fixtures; not BSP crush behaviour or multi-part doors |
| Entity presentation | Smoothing transitions, stale history, reset/teleport helper, monotonic interpolation | No camera/body alignment measurement or render-to-hitbox latency measurement |
| Menu/video/audio/assets | Availability, bot roster, resolution selection, codec fallback, packaging and smoke validator | Synthetic services and isolated runtime smoke do not establish visual quality or audible timing |
| Runtime smoke | Startup, map readiness, engine/shader errors, screenshots, exit; scheduled input and cvar changes | A successful run does not prove a particular jump, hit, or parry actually occurred |

## Highest-priority remaining scenarios

1. **Real collision geometry:** multi-step staircases and seams, high-speed
   corners, steep surf ramps, BSP-backed vault clearance and wall-jump surfaces, wall-run duration and
   reattachment on real geometry. Single-step, wall-jump, detach and vault clearance now
   have synthetic fixtures; follow with a BSP course asserting positions/events.
2. **Movement interacting with world state:** multi-entity moving/rotating platforms, doors,
   BSP water traversal/water jumps, spawn/teleport clearance, knockback and
   death/respawn resets. Test both slow-motion transitions and normal time.
3. **Combat integration:** projectile flight/splash/bounce, riposte/counter-hit consumption
   across weapon and damage code, kick/dash cooldowns, full death/respawn and
   pickup rewards. Core G_Damage/armor/guard boundaries now have their own tests.
4. **Movement chains and scheduling:** bunnyhop/slidehop boost caps, slide carving
   and downhill behaviour, air-control symmetry, changing scale during contact,
   rapid input reversals, long-frame catch-up, pause/restart with held inputs.
   Replay timestamped commands through the actual host loop at 30/60/144/240 Hz.
5. **Legacy network/demo path:** command replay, prediction correction, packet
   loss/reordering, remote rendering and recording/playback. The experimental
   local path intentionally does not support remote clients or demo recording.
6. **Presentation and integration:** sword mesh/trail vs hit-volume alignment,
   aim-to-hit latency, camera/body continuity, controller input, audio time
   policy, and measured performance at high resolution/refresh rate.

## Tests outside the default runner

`tests/renderer_font` exists but is not included by `tests/CMakeLists.txt` or
`scripts/test.ps1`. Its FreeType-dependent checks require a separate build.
`scripts/test-slowmo-clock.py` is also standalone (expects a C compiler named
`cc`), rather than part of the Windows suite. These are execution-coverage gaps;
existing files should not be counted as routinely executed tests.

## Finding from this audit

The new landing test failed in both step modes. On a flat vertical landing,
`PM_WalkMove` could normalize the tiny OVERCLIP residual back to the original
falling speed. At fractional timesteps, the ground probe can engage before the
collision sweep, producing an unrequested upward bounce. Flat-ground contact
now consumes vertical landing velocity before slope-speed preservation. Actual
slope handling remains unchanged and is covered by an uphill fixture.

Landing tolerances allow the existing 0.25-unit ground probe; the assertion also
requires grounded state, no remaining vertical velocity, and restored air/wall
jump resources. Tests remain enabled in Release builds.

Validation: full suite and Windows Release build passed. Logs are
`engine/build/movement-before.log`, `movement-tests.log`, and
`movement-build.log`. This audit does not claim all movement bugs are fixed.


## Ledge follow-up: no jump input

The user clarified that the observed boost happens without pressing jump.
Coyote jumping checks `upmove`, so it cannot directly cause that no-input boost.
A new partial-overhang fixture reproduced **automatic vault retries**: eight
launch attempts at normal speed and seventeen at 0.04x within 64 world ms,
with jump input zero. The destination above/beyond the ledge was clear, but the
player's current headroom was blocked. Each launch hit the ceiling, erased
upward velocity and made the next tick eligible to vault again.

Vaulting now checks full-hull lift and passage clearance before applying its
impulse. The blocked fixture produces zero vault events; clear vaulting still
succeeds once. An idle-at-edge fixture also verifies no jump, air-jump or vault
without motion/input. These fixtures identify a plausible cause of the user's
report, not a reproduction at their exact Nocturne location.

Separate coyote failures were also reproduced and fixed:

- A head-bumped ground jump could rearm grace because its vertical velocity had
  already been clipped before the next timer update. A successful jump now
  consumes grace immediately.
- Held jump could trigger coyote while airborne. Coyote now requires a fresh
  press; held-jump bunnyhops still work after genuine landings.
- An airborne spawn could receive grace without ever touching ground. Grace is
  now armed by the actual support-loss transition, including walk-offs that
  happen at the end of a movement tick. Landing still rearms it normally.

This follow-up added 34 cases (17 scenarios at two time scales), bringing movement
to 62 cases and the suite to 117 native cases plus 49 Python checks. Windows
Release build and the suite passed. Failure evidence: `engine/build/coyote-before.log`,
`coyote-spawn-before.log`, `ledge-before.log`; current validation:
`engine/build/coyote-tests.log`, `coyote-build.log`. The isolated Nocturne smoke
run checks runtime health rather than verifying the exact ledge manoeuvre.

Still outstanding: moving platforms/doors/water, real BSP traversal, bounded
wall-run lifetime across reattachments, damage/parry resolution and network/input
latency scenarios described above. No claim of full gameplay coverage is made.


## Water, wall-run, combat and mover follow-up

Added 35 cases: eight water cases, four wall-run exhaustion/reattachment cases,
two wall-run encoding/presentation cases, sixteen real damage-resolution cases
and five real single-entity mover-push cases. Movement scenarios run at both
normal and 0.04x world speed. Total: **152 native cases + 49 Python checks**.

Three confirmed defects were fixed:

- Clean parries set damage to zero, then the general minimum-damage clamp dealt
  one point anyway. Fully stopped hits now return after clash/riposte processing,
  without health damage or victim knockback.
- Wall-run detachment (including duration expiry) cleared the elapsed time, so
  releasing forward or simply waiting one tick refilled the airtime budget.
  Detached state now retains consumed world milliseconds until grounded.
- A rotating platform whose push failed could restore rider position without
  restoring view yaw. The fallback now restores both.

Wall-run storage uses the existing signed 16-bit `STAT_WALLRUN`; it adds no wire
fields. Active values encode signed (elapsed+1), including sub-ms attachment
before a whole millisecond passes. Detached values encode 0x4000|elapsed. Shared
helpers decode budget and active side; camera, player animation, HUD, entity
conversion and playtest counters use active side rather than nonzero raw state.
The retained budget is clamped to 16382 world milliseconds; the configured default
is 1600. Use matched game/cgame builds with this changed stat interpretation.
Tests verify range/side/time round trips and both entity-state conversion paths.

The full suite and Windows Release build passed. Failure evidence is in
`engine/build/remaining-before.log` (parry), `wallrun-before.log` and
`movers-before.log`. Final checks: `remaining-tests.log`, `remaining-build.log`.
A populated old-Nocturne GL2 smoke checks runtime health; it does not establish
live wall-run feel, platform carry accuracy or a successful parry at a chosen
map location. Current fixtures do not cover the full multi-rider push transaction,
BSP-level geometry, water-jump clearance, remote prediction, demo compatibility
or measured input-to-render latency. Those were the next integration gaps at
that checkpoint; the follow-up below adds transaction and water-jump fixtures.

## Projectile, water-jump and platform-transaction follow-up

Added 16 cases: six water-jump cases (three scenarios at normal and 0.04x speed),
two multi-rider platform cases, and eight projectile-impact cases. Current total:
**168 native cases + 49 Python checks**.

Four defects reproduced before their fixes:

- When a later rider blocked a platform, rollback restored earlier riders'
  saved positions but relinked displaced collision positions. Rollback now
  restores runtime origin/angles before relinking each entity.
- Projectile deflection ignored the guard-raise delay. It now uses the same
  configured readiness boundary as damage resolution (79/80 ms tested).
- Reflection started at the trajectory's frame endpoint, potentially beyond
  the actual contact. It now starts at the collision trace's impact position.
- Guided returns could select an enemy behind a wall. Candidate selection now
  checks visibility; visible targets still receive guided returns.

The impact harness calls production impact, deflection and damage functions,
including a second impact that verifies the reflected shot damages its original
shooter and credits its new owner. Effects, splash/bounce, hook handling and
trace services are stubbed. It does not test full projectile flight.
Water-jump fixtures check lip clearance and falling release through full pmove;
they required no production movement changes in this follow-up.

Failure evidence: `engine/build/mover-transaction-before.log` and
`missile-before.log`. Passing suite/build logs: `projectile-water-tests.log`
and `projectile-water-build.log`. Real BSP traversal, multi-part moving doors,
full death/respawn integration, remote prediction and measured latency remain
open. The isolated classic-Nocturne smoke is a runtime-health check, not proof
of a particular live projectile parry or water-jump manoeuvre.


## Standard local slow motion and wall catches (2026-11-09)

Local smooth timing now defaults on in the engine and both launchers. Dedicated
launches use legacy timing; Windows `-LegacyTiming` explicitly selects it for
network hosting/demo recording. An existing archived setting is respected when
launching the executable directly. Engine and all native modules must match.

Six new movement scenarios, each at normal and 0.04x speed, exercise a front-wall
catch: held approach, fresh-press kick, expiry, release, actual ground-jump approach,
and no-input falling. Hold forward into a near-vertical wall to catch for up to
350 world milliseconds, then release/repress jump to kick away. Holding the
approach jump never auto-kicks. Catching uses the existing wall-run airtime budget;
release/recontact does not refill it, a kick consumes its remaining catch allowance,
and grounding resets it. There is no new network field or camera roll for a catch.
Low lips remain vaultable, verified by existing vault fixtures. Catching does not
refund air jumps or wall jumps. Normal tangential wall running remains available.

Total: 180 native cases + 49 Python checks. Logs: `wall-catch-tests.log` and
`wall-catch-build.log` under `engine/build`. Synthetic collision tests establish
behaviour; a fresh-profile smoke checks default local mode activation. Human
playtesting is still needed to tune the 350 ms allowance and catch distance.


## Immersive HUD prototype (2026-11-09)

`exec immersive` enables a minimal HUD, five view-sword health lights, critical
health perception and `g_noArmor 1`. `exec immersive_off` restores the normal
HUD and armor rule. The presets do not overwrite binds or baseline grade/rim/DoF
values. Render prerequisites (grade/postprocess/rim) are enabled by the preset.
No armor is consumed for protection, armor pickups/purchases are rejected, and
held armor is cleared each server end frame. Turning armor back on does not
restore armor previously removed. The real damage harness adds a no-armor case;
195 native cases and 49 Python checks pass.

Critical health starts below 35% of maximum, reaching full strength at 10%.
Effects ease on the real clock: peripheral desaturation/warm tint, bounded detail
sharpening and increased existing model rim light (not visibility through walls).
`cg_adrenaline 0` disables the effect independently of the HUD. No pulse, camera
shake or blur is added. Minimal-HUD active gameplay bypasses DoF through a
transient renderer flag, preserving the user's setting and killcam treatment.
A normal health readout is retained for hidden weapons/third person/non-sword
weapons. Scoreboard, damage direction, crosshair preference and death report remain.
GL2 implements the post effects; GL1 retains sword lights and minimal HUD only.

Live isolated smoke checks cover health 100 -> 20 -> 100 and preset disable,
with screenshot inspection. `give health <value>` is a cheat-only bounded command
for reproducible visual checks. Logs: `immersive-tests.log`, `immersive-build.log`;
smoke directories start `polish-immersive-`. These checks do not establish enemy
readability in a crowded live fight, accessibility for every player, or no-armor
balance. Those require the next human playtest.
