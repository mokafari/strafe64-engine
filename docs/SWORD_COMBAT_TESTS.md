# Sword combat regression coverage

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1`.
The sword suite contains 27 deterministic cases. CMake extracts the actual
`Weapon_Sword`, `G_SwordFindTarget` and `PM_SwordMagnetLunge` functions from source;
full production pmove is also linked for swing/cooldown/buffer tests. No copied
combat implementation is maintained in the tests.

## Assertions

- Standing and maximum-speed reach, with explicit bot maximum-range cases.
  Blade rays start at the authoritative eye and extend 80–160 units, according
  to horizontal velocity at hit resolution. Target bounds still matter: a body
  with a 15-unit half-width can be contacted before its center enters that range.
- Walls block hits, including a wall inside the former 14-unit gun-muzzle offset.
- Multiple rays damage a target only once per swing; combo damage progresses and
  resets after its window. Corpses neither award kills nor shield live targets.
- Assist selection excludes dead targets, teammates, out-of-cone targets and
  targets behind cover. Kill-chain momentum redirects to a visible living target;
  a dead body does not obstruct that selection.
- Lunge weighting is symmetric left/right. Bots get no player magnet assist;
  distant, point-blank, corpse-only and wall-obstructed targets retain the baseline
  step instead of a stronger assisted lunge.
- The minimum-range rule uses nearest body-surface distance, consistently across
  all sweep rays; high-speed fly-bys retain their existing exemption.
- Full pmove produces one impulse per swing and correctly buffers a follow-up at
  both 1x and 0.04x world speed, including repeated integer command timestamps.

## Changes supported by failures

The first run reproduced seven failures: standing reach, fast reach, bot reach,
corpse kill rewards, wall-obstructed target selection, teammate selection and
asymmetric magnet strength. A subsequent minimum-range test exposed diagonal
rays bypassing the close-range restriction. Those failures were fixed, then
coverage was expanded for corpse occlusion and full movement timing.

The weapon no longer inherits the gun's forward muzzle offset or rounded muzzle
coordinates. Aim-snap uses the current speed-dependent reach. Blade assistance
uses visibility checks, and blade/lunge traces ignore corpse contents. Angular
weighting replaces array-index weighting for lunge strength.

The 28-unit minimum-range setting, 80–160-unit blade range, baseline lunge,
1000-unit/s lunge speed gate, damage tuning and combo rules remain. Assistance
is not a guarantee of closing every gap or adding momentum at every speed.

## Validation and limits

Windows Release build passed. All **55 native cases and 49 Python checks** passed.
Logs: `engine/build/sword-before.log`, `sword-tests.log`, and `sword-build.log`.
Isolated populated old-Nocturne GL2 smoke runs exercised attack input, movement
and slow-motion transitions. These check startup/rendering/runtime errors; they
are not a measured record of successful live sword impacts.

The harness uses deterministic segment/AABB collisions and records damage
calls. It does not replace BSP collision tests, visual sword/trail alignment,
network latency tests, or armor/parry damage resolution. In particular, these
fixes do not prove every reported distant-looking hit is eliminated. Use
`g_debugDamage 1` during a reproduction to compare actual attacker distance,
blade range and speed with what is rendered. Subjective reach and finishing
pull should be checked in the next hands-on playtest.
