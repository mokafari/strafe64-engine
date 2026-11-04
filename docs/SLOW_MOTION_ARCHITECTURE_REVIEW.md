# Slow-motion architecture review — 2026-11-04

Reviewed local source at `dd66950`, including the entity smoothing fix. This is
a source-based timing audit, with arithmetic derived from the implementation;
it is not a capture of end-to-end input latency or proof that all live hitches
have the same cause. The old Nocturne map remains staged for playtesting.

## Intended behavior

The user confirmed: **character movement slows with the world; aiming remains
responsive**. Entering and leaving slow motion should feel continuous. A held
movement key should wake time promptly; releasing it should settle smoothly.
Bots, projectiles and player displacement should share the same world-time law.
Looking around should not wait for a physics step. Whether looking alone wakes
time is a separate design choice; retain current movement/action-driven intent
until explicitly tuned otherwise.

## Verdict

The engine can support this goal, but the current slow-motion pipeline is a
prototype with compensating filters, not yet a coherent timing architecture.
Further smoothing alone will trade visible steps for positional lag. Preserve
the renderer, content, collision system and useful gameplay code; refactor the
clock contract and movement/render handoff in stages.

## Current path and evidence

| Layer | Current behavior | Source |
| --- | --- | --- |
| Intent | Input magnitude, jump, attack, dash, guard and crouch choose target scale | `g_active.c:1057–1273` |
| Easing | Exponential rise/fall on real milliseconds; defaults 30/s up, 8/s down | `g_active.c:1124–1128,1216–1230`; `g_main.c:228–229` |
| Publication | The eased value reaches `timescale` only at magnitude/time thresholds; small changes wait at least 50 ms | `g_active.c:1235–1260` |
| Main clock | `timescale` is SYSTEMINFO; fractional milliseconds are accumulated, but the returned world delta remains integer | `common.c:2939,3190–3228` |
| Server | Tick length is truncated from `(1000 / sv_fps) * scale`, minimum 1 world ms | `sv_main.c:1094–1103,1152–1157` |
| Movement | Commands round upward to an 8 ms world-time grid; movement skips commands that do not advance commandTime | `g_active.c:1328–1348`; `bg_pmove.c:3154–3192`; `cg_predict.c:543–616` |
| Snapshot | Origins and angles snap to integer coordinates; snapshot spacing also truncates a scaled interval | `bg_misc.c:1555–1580,1647–1663`; `sv_snapshot.c:654–670` |
| Client | Prediction reads current scale once for a replay of command history; render time is still an integer | `cg_predict.c:499–508,543–619`; `cl_cgame.c:1016–1023` |
| Presentation | Bot and camera filters dead-reckon between steps, clamp against raw position and low-pass the residual | `cg_ents.c:1056–1132`; `cg_view.c:1149–1242` |

The fractional accumulator is worth keeping: it prevents the old minimum-one-ms
per-render-frame speedup. Real-time intent easing and the latest-command aiming
update (`cg_predict.c:630–643`) are also sound building blocks.

## Main problems

### 1. Deep slowdown starves movement updates

For the configured `pmove_msec=8`, a sustained scale produces these nominal
intervals. These are derived values, not measured frame rates; input delivery,
server scheduling and hitches can add further irregularity.

| World scale | 1 world-ms in real time | 8 world-ms movement interval | Nominal movement updates/s |
| --- | ---: | ---: | ---: |
| 1.00 | 1 ms | 8 ms | 125 |
| 0.50 | 2 ms | 16 ms | 62.5 |
| 0.10 | 10 ms | 80 ms | 12.5 |
| 0.05 | 20 ms | 160 ms | 6.25 |
| 0.04 | 25 ms | 200 ms | 5 |

The saved player configuration uses 0.04 as its floor. Rendering at 144/240 Hz
cannot recover collision and direction changes that have not been simulated.
With `sv_fps=20`, the server is trying to retain roughly a 50 ms real-time tick,
but fixed movement remains on its separate 8-world-ms grid. Increasing sv_fps
or snaps alone does not remove that movement bottleneck.

At 0.04, the bot and camera filters also expand their time constants to 200 ms.
That is a filter parameter, not a measured 200 ms input delay: dead reckoning
reduces lag on steady paths, but reversals, stops, contacts and authority
corrections remain harder to conceal.

### 2. Network publication determines the simulation clock

`cur` is eased continuously, but the actual clock uses the thresholded published
value. The minimum 0.004 scale increment is 10% of a 0.04 floor; three-decimal
formatting has 0.001 granularity. Small changes can wait 50 ms, while large
changes bypass that wait. This is not a uniform 20 Hz cap.

The threshold exists for a legitimate reason: continuously rewriting SYSTEMINFO
can overflow reliable commands. The architectural fix is to separate local
simulation-scale updates from network publication, not remove the throttle and
reintroduce the overflow. Send timestamped time-state changes/samples and recover
intermediate scale continuously. Define initialization, correction and ownership
explicitly before altering the protocol or VM boundary.

### 3. Movement does not consistently use world time

Air acceleration and air steering divide by scale (`bg_pmove.c:1199,1297`).
Movement-window timers reconstruct real time by dividing a whole physics step
by the current scale (`bg_pmove.c:2850`). Displacement and gravity still use world
dt. Thus the existing movement is a hybrid, rather than simply the same movement
played slower. The compensation applies through shared pmove, including bots.

Real-time action forgiveness may be desirable, but it should be an explicit
exception. For the confirmed goal, start with world-time movement/acceleration,
and real-time aiming/input sampling; assess jump grace and other windows
individually. Do not blindly remove compensation before restoring update cadence.

Prediction also uses the latest scale across buffered commands, while authority
may have simulated them at earlier scales. That is a transition-divergence risk,
not a measured cause of a specific observed jump. Both sides need the same time
history or per-step integrated world dt, rather than a current-value lookup.

### 4. Several systems own the same global cvar

Gameplay writes `timescale`; killcam independently writes it with different
thresholds (0.03/80 ms, large-change bypass); legacy client fading can also write
it if enabled (`cg_killcam.c:808–833`, `cg_view.c:2141–2153`). Dead-player guards
avoid some conflicts, but ownership is distributed rather than enforced.

Use one controller with explicit gameplay/killcam/pause/recovery states. Requests
from other systems must not directly write the effective clock. Preserve clear
respawn, spectator, map-reset and disconnect behavior. Current implementation
assumes one human time owner; multiplayer arbitration is outside this review's
first implementation scope.

### 5. Presentation hides discontinuities after the fact

The last fix corrected stale bot history on normal-to-slow transitions,
teleport/reappearance and long gaps. It did not change movement cadence or
snapshot precision. The camera still has separate static state inside a gated
slow-motion block; short bypass/reentry can reuse an older timestamp. Consolidate
reset/update policy so camera and remote bodies do not accumulate inconsistent
history. Keep any smoothing bounded and measured against authoritative error.

AI decision cadence also uses world time: default `bot_thinktime=100` corresponds
to 2.5 real seconds at 0.04 (`ai_main.c:2135–2145,2226–2249,2334`). Slow decisions
may be intentional. Separate decision pacing from continuously sampled steering,
collision integration and visual pose evaluation; raising AI frequency is not
the first remedy for position jumps.

## Proposed clock contract

1. **Real clock:** monotonic, high precision. Drives input, look, controller
   easing, UI and presentation scheduling.
2. **World clock:** integrate effective scale over each real interval. Retain
   sub-millisecond precision internally and at movement/render interfaces.
   Round only at legacy boundaries that have a defined residual/error policy.
3. **Movement schedule:** fixed real-time cadence (125 Hz is a starting candidate,
   not a performance claim), with fractional world dt for each step. At 0.04,
   an 8 ms real step represents 0.32 ms world time. That value must survive pmove;
   converting it back to integer commandTime would recreate the problem.
4. **Prediction:** use the same integrated dt/scale timeline as authority for
   each replayed command. Keep command identity and ordering distinct from scaled
   time; test deterministic replay through arbitrary ramps.
5. **Rendering:** interpolate coherent transforms using high-precision render
   phase and preserved positions. Limit extrapolation and correction error.
   Mouse look is evaluated every rendered frame. Animation, projectiles and
   effects use world phase where appropriate, not ad hoc reverse-scaled timers.
6. **Transport:** publish bounded time-state updates independently of local
   evaluation. Preserve fractional positions where needed and evaluate bandwidth
   cost; existing float position fields do not justify assuming all protocol or
   old-client compatibility automatically holds.

Physics still needs numerical and collision regression testing: fixed real
cadence with scale-dependent dt changes integration behavior relative to the
old fixed world step. Frame-rate-independent scheduling is not by itself proof
of physically equivalent trajectories or deterministic multiplayer replay.

## Implementation order and acceptance gates

1. Add a per-frame/tick trace: real dt, integrated world dt, target/effective
   scale, owner, movement step count, snapshot age, raw/rendered transforms and
   prediction error. Capture stationary, start/stop, reversal, jump, collision,
   moving platforms, death and respawn at several scales. Preserve a baseline.
2. Introduce one clock owner and separate effective scale from SYSTEMINFO
   publication, initially behind an opt-in local-play flag. Verify bounded
   reliable traffic and no competing writers.
3. Introduce fractional movement time and shared prediction timing. Test the
   confirmed world-slowed movement contract before removing compensation.
4. Replace compensating filters with coherent interpolation, then tune the
   feel on the old Nocturne test map. Keep existing path available for A/B.

Required tests include sustained 0.04/0.05/0.1/0.5/1 scales; rapid ramps and
reversals; 60/120/144/240 rendering schedules; irregular frame intervals; contact
and teleport behavior; view response during zero-integer-world-ms frames;
killcam ownership; command replay parity; and no reliable-command overflow.
Measure visual position error and discontinuities, not just process survival.

Suggested initial gates (targets to validate, not current achievements): clock
integration error bounded below one legacy world millisecond; no unexpected
200 ms movement holds at the floor; consistent trajectories across render rates;
no unexplained position discontinuity on scale changes; responsive latest-input
aiming; and bounded snapshot/prediction corrections under a documented error
budget. Choose visual tolerances from captured camera distance and pixel error.

The current rise/fall defaults have ideal 90% response times of approximately
77 ms and 288 ms respectively (`-ln(0.1)/rate`), before publication/scheduling.
Keep those as an A/B baseline, then tune them after fixing the clock contract.
Do not bury timing problems under longer smoothing windows or a higher slow-mo
floor and call the architecture solved.

## Validation boundary

The preceding build passed 16 native cases and 49 Python checks, including
five entity-motion cases; a populated Nocturne smoke session survived explicit
scale transitions. Those checks do not prove this proposed architecture or
certify the desired feel. No simulation changes were made during this review.
