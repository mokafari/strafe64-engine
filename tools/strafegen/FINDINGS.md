# Measured findings (via the engine API)

Data gathered by driving the live engine through `engine_api.py` / the MCP tools
(`engine_measure`, `engine_compare`, `engine_state`, scripted air-strafe + bhop,
multi-trial medians). Numbers are medians of repeated trials unless noted; the
scripted air-strafe is an approximation of skilled play, so treat magnitudes as
relative, not absolute.

## Movement / air-strafe tuning

Peak speed via the air-strafe + **pulsed**-bhop harness (held jump does *not*
rehop — it just runs at the ground cap; jump must be pulsed). Ground cap is
`g_speed` 320; air-strafing builds well past it.

| cvar (live) | swept values → median peak (u/s) | shipped | read |
|---|---|---|---|
| `pm_strafeAccelerate` | 40:408 · **70:422** · 110:417 | 70 | optimal — best + tightest spread |
| `pm_wishSpeedClamp` | 15:408 (±50) · **30:428** (±12) · 60:409 | 30 | optimal — fastest *and* most consistent |
| `pm_airControlAmount` | 75:422 · 150:413 · 250:417 | 150 | ~flat on peak speed (governs turn feel, not top speed) |

**Conclusion:** the shipped live air-strafe constants are well-tuned; the speed
ceiling lives in these params, **not** in the bhop boost.

**Bhop ceiling (source-tier experiment):** raising `pm_bhopBoostMax` 1.10→1.30
(+ `pm_bhopBoostPerHop` 0.04), rebuilt, gave **no** speed gain (407 vs 413
baseline). `pm_bhopBoostMax` is the wrong lever for a faster game.

## Bullet-time (`g_timeBind`) response

Dilated `timescale` by player state (the SUPERHOT-melee core):

| state | timescale |
|---|---|
| still | ~0.20–0.22× |
| moving | 1.0× |
| attacking (swing, stationary) | ~0.65× |

**Config inconsistency worth a decision** (the *only* config conflict — checked:
of the 5 cvars both startup configs set, just this one differs): the still floor
`g_timeBindMin` is **exec-order-dependent**. Source (`g_main.c`) and
`strafe64.cfg` say **0.05** ("near-freeze"); `autoexec.cfg` (auto-exec'd at
startup) says **0.2** (shallower). So a normal boot runs 0.2, but `/exec strafe64`
flips it to 0.05 — the freeze depth silently depends on which ran last. Pick one:
align `autoexec.cfg` to 0.05 if "almost stop time when still" is the intent, or
bump the source/strafe64.cfg default to 0.2 if the shallower freeze is intended.
(`g_timeBindMin` is a live cvar — tunable without a rebuild.)

> Live cvar values can diverge from `g_main.c` defaults via `autoexec.cfg` —
> trust `engine_movement_get` / `get_cvar` (live) over source when they disagree.

## Dead ends (so they aren't re-tried)

- **`misc_model` does not render at runtime.** Verified: `engine_spawn("misc_model",
  model="models/players/sarge/upper.md3")` (a confirmed-present md3) spawns the
  entity but renders nothing — `misc_model` is a compile-time hint baked into the
  BSP by q3map, ignored by a runtime spawn. So there's no runtime mesh-import /
  prop-audition via `misc_model`. To show a custom mesh: make it a player/weapon
  model (`engine_audition_model`) or bake it into a generated map.
- **Raising `pm_bhopBoostMax`** doesn't increase achievable speed (see above) —
  the air-strafe params set the ceiling.
