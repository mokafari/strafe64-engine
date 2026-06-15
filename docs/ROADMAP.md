# STRAFE 64 — Autonomous Dev Loop Backlog

The self-paced dev loop reads this each iteration, works the highest-priority
open task, verifies with `strafegen/dojo.py` (no regression vs the dossier),
journals to `strafegen/dojo_runs.jsonl`, and updates this file.

> **▶ HUMAN PLAYTEST: see `HUMAN_PLAYTEST.md`** (iter31). The autonomous loop has
> converged on what it can verify without a human; ~16 iterations of features now
> need a real playtest. That guide maps every one to how to trigger + what to check.
> Your notes are the highest-value input from here — they re-open concrete tuning.

**Definition of done:** all four dojo archetypes `IN_DOSSIER` ✅ **MET (iter14,
2026-06-15)** — SPEED/FLOW/ZTRICK/ARENA all pass the BOT-regression baselines
(traversal quality + combat happens). The bot substrate is healthy + regression-
protected. Remaining = the game-feature backlog (P2) + HUMAN-validated polish
(strafe-jump-to-800, wall-run, race completion, fight-at-speed — bots can't do
these; human playtest territory) + the ★ surf core loop.

## ★ Core loop — the game we're building toward (2026-06-15, Gustav)
**>>> v0 LOOP COMPLETE (iter16-18): surf line (`--surf`) → lap teleport →
PERS_SCORE payout → `buy` loadout shop → MISSION REPORT rank. The whole
surf→lap→points→spend→again cycle exists end to end. Remaining work is DEPTH +
FEEL, not the skeleton: procedural surf circuits (v0 is fixed geometry),
ramp-angle/momentum tuning by hand, persisting purchases across death, and a
proper shop HUD. All human-playtest-validated from here (`\map surf_64`). <<<**

**A CS-surf-style continuous track you flow through, lap after lap.** Think
surf_ maps: the track is built from **steep angled ramps you air-strafe
along** — too steep to stand on, so you *slide/surf*, and strafing into the
slope keeps and builds speed. Ramp links to ramp into one seamless ride that a
skilled run never stops moving through (surf → transition → surf → bhop landing
→ surf...). On crossing the finish you **respawn at the start** (lap +1) and
**bank points** (scaled by lap time + the FLOW style score we already compute).
Points buy **persistent loadout / ability upgrades that compound over the
session** — Warcraft-3-mod-for-CS-1.6 style progression: extra air-jump, higher
bhop cap, longer wall-run, grapple/dash, weapons/ammo. Each lap you're faster
and kitted harder; the track (and the void) escalate to match. "Movement IS the
game" + roguelite-lite progression.

**Physics note:** Q3 air-strafe already surfs — on a plane steeper than
`MIN_WALK_NORMAL` you aren't grounded, so `PM_AirMove` clips velocity along the
ramp and air-accel builds speed. Verify/tune surf feel (ramp angle ~45°+,
air-accel, no friction on the slide) — likely mostly works already; the
velodrome's `make_prism` banks are the starting point (but those are ridable
~26-34°; surf needs steeper, non-walkable).

Components: (1) `strafegen` **surf-loop generator** — [~] v0 SHIPPED (iter16):
`SurfLine` class + `--surf` flag. A hand-laid two-ramp proof — start pad → surf
ramp A (51.3°, drift +Y) → transition → surf ramp B (51.3°, drift −Y) → finish
that teleports back to start (the lap). Ramps are `make_prism` tops banked past
the walkable angle (you surf, don't stand); 1792u descent. Generates a valid
sealed BSP + .aas, boots clean in ioq3ded (AAS initialized), deployed to
baseoa/maps (`\map surf_64`). Schematic: `strafegen/generated/surf_64_schematic.svg`.
[x] PROCEDURAL now (iter19): `build()` lays a seed-driven chain of N=3–5 ramps,
alternating bank so each ramp's low exit edge is the next's high entry edge (flush
on the player's path, one shared Y-corridor); angles seed-varied in the 49–55°
surfable band, lengths/fwd-drops randomized. Verified across seeds 64/7/1337/2025
(all valid sealed BSP + .aas, surf_1337 boots clean). Banked TURNS — [~] CANDIDATE
(iter32): `SurfTurn` class + `--surfturn` builds a steep banked 180° arc (53°,
velodrome-style inner-low/outer-high) you surf around; `\map surfturn_64`. Generates
a valid engine-loadable map (boots clean; no AAS — bots can't surf an arc). FEEL is
human-gated: if the bank carries speed around, fold `arc()` into SurfLine for true
2D circuits. Still TODO: ramp-angle / momentum-handoff FEEL tuning (human). (2) lap → respawn-at-start + points award — [x] DONE (iter17): `race_finish_touch`
(g_trigger.c) now banks a per-lap payout in PERS_SCORE = lapBonus 100 + speedBonus
(3000000/ms, cap 500) + bestBonus 250, increments `client->raceLaps`, and centerprints
`LAP n / time / +award / TOTAL`. The teleport-back was already wired, so finish now =
score + respawn-at-start + go again. Score is the session currency. (3) between-laps
**loadout shop** — [x] DONE (iter18): `buy` command (Cmd_StrafeBuy_f, g_cmds.c)
spends PERS_SCORE on a kit table (rocket 400 / rail 700 / bfg 1200 / armor 300 /
heal 200); "buy" alone lists shop + balance, "buy <item>" purchases (afford +
not-owned checks). Kit rides the lap teleport (no respawn). Lap centerprint hints
"type buy to spend". Persist-across-death DONE (iter20, boughtWeapons). Shop HUD
DONE (iter22): the MISSION REPORT death screen now lists the shop with live
affordability colour (green=affordable, amber=OWNED, dim=can't afford) + your
SCORE, at the natural pre-respawn spend moment. Movement-kit: `buy airjump` (800)
DONE (iter23) — a permanent +1 air jump (PMF_AIRJUMP_BONUS, re-granted on spawn);
buying MOVEMENT with lap points is the most STRAFE-flavoured upgrade. TODO: more
movement kit (haste / slide-boost), and one-key buy (still `buy <item>` console). Ties into [[strafe64-flow-combo]] styleScore + the
MISSION REPORT rank.

**Bot caveat — PARTLY SOLVED (iter36):** bots CAN now surf. `BotSurfControl`
(ai_main.c): when a bot is airborne over a too-steep-to-stand face, hold the
strafe key INTO the bank (uphill of the goal heading) with level pitch and no
jump — air-accel then carries it along the ramp. AAS still can't *path* a surf
face, so the surf line drops item-bait pickups down each ramp midline to lead
bots on, and has 4 spread spawns (a single spawn telefrags a bot field → the
"0 speed / mass deaths" red herring that masked this for ages). Result: bots
build 445–646 ups riding the ramps, ~0 deaths. NOT yet completing laps (they
surf but don't reliably reach the finish) — that's the remaining refinement.
Surf *feel* is still human-validated; but the dojo could now include a surf
archetype.

## P0 — unblock the dojo substrate (bots must actually play)
- [~] **bot-nav on movement courses** — two levers tried:
      • GOAL AWARENESS via item-bait (`_dojo_items`) — **WORKS**. FLOW now
        avg 325ups, flow 76%, stuck 0.35s (PARTIAL, near dossier). SPEED gate
        passes (OUT, avg 135 but stuck ~5s). ZTRICK still ✗.
      • CAPABILITY AWARENESS via a tuned bspc -cfg (aas_strafe64.cfg) —
        **REJECTED by no-regression** (FLOW 152→0). bspc's FLT_MAX default
        treats the bot as able to jump anywhere; with the enhanced moveset
        that over-optimism navigates BETTER than a realistic model. Reverted.
      Next: SPEED/ZTRICK bots don't carry speed into gaps (stop at each item,
      jump from standstill, fall). Fix = movement-execution (bots pre-accel
      before gaps) or easier gap geometry, OR real GOAL awareness: game-side
      bot code that drives forward to the finish instead of item-grazing.
- [~] **bot combat in arena** — UNBLOCKED (iter10): new `Pit` class (sealed
      box, 8 close spawns, cover pillars, weapons) replaces the velodrome for
      the arena dojo. Bots now frag: ARENA BROKEN→PARTIAL, **midair 50%**
      (target ≥15, met!), 12 bot-runs. Remaining (tuning): frags/min 0.6→≥3
      (tighter pit / more bots / aggression) and killspd 13→≥320 (fight AT
      speed — boost pads / speed-gated weapon). History below:
- [x] **bot combat in arena** — bot-FILL fixed (iter8): dojo.py now fills via
      explicit `+addbot <name> 4` using real OA bot names (BOT_NAMES, from
      botfiles/bots/*_c.c) instead of bot_minplayers (which capped ~2 on
      dedicated). 5 bots fill cleanly; FLOW even logged a midair frag. BUT the
      velodrome arena still gives ~0 frags — too spread for encounters. Next:
      a TIGHT combat-pit arena dojo (small box, cover, close spawns) so bots
      actually fight. Also: apply the same +addbot fill to viz.py/playtest.py
      (trivial) and bump archetype bot counts now that fill is reliable.

## P1 — tune to the dossier (once measurable)
- [ ] FLOW to band: flow ≥60% (now 32%), wallrun ≥5% (now 0%), bhop ≥x5.
      Hypotheses: bait placement that forces wall sections; movement params.
- [ ] SPEED to band: maxspd ≥800 (now ~231 — bots don't strafe-jump optimally).
- [ ] ZTRICK completion 30–80% gradient across skills.
- [ ] ARENA: frags/min ≥3, midair ≥15%, engagement speed ≥320.

## Movement kit additions
- [~] **VAULT / mantle** — IMPLEMENTED (iter6). `PM_CheckVault` in bg_pmove.c
      (both trees), called in PM_AirMove; no new stat (self-limits on
      velocity[2]); ballistic lift onto the lip keeping 85% horiz speed.
      No-regression verified (FLOW still 76% flow / 298ups). Still TODO:
      validate the actual vault *triggers* on a map with vaultable ledges
      (dojo_flow has none) — add a ledge to a dojo / test on an OA map + tracer.
- [x] **VAULT / mantle** (2026-06-15, Gustav) — coming at a ledge edge with
      speed, **vault up and over it while keeping momentum** (Titanfall mantle:
      ~0.33s, preserve ~85% horizontal speed) instead of stalling on the wall
      or dropping. The piece that keeps a surf/flow line unbroken when a ramp
      dumps you at a lip. Impl: a `PM_CheckVault` in bg_pmove.c beside
      wallrun/walljump — trace forward+up to find a climbable lip within reach
      while airborne & moving, lift the player onto it preserving speed;
      networked via a stat/pm_flag; cgame does a slight pull-up camera. Tunables
      at the top of bg_pmove.c. Composes into the chain (surf → vault → bhop).

## Dojo health (calibration, not features)
- [x] FLOW `stuckms` band 1500 → **2000** (iter21). Set from the observed
      good-behaviour spread (850–1900; broken nav is 2388+). 1500 sat at the noise
      centre and false-PARTIAL'd no-op changes (iter17, iter20). Verified: FLOW now
      green 1507/1118 across runs; a genuine nav break (2388+) still trips it.
- [x] **Multi-run verdict aggregation** — DONE (iter27). dojo.py: first pass is
      single-run; any archetype that comes back non-green (but sanity-ok) is re-run
      +2 and classified on the element-wise MEDIAN of the 3 runs (median_profiles).
      Green archetypes stay single-run (fast path preserved). run_scenario tags are
      now idx-isolated so re-runs don't collide. Validated: ARENA flapped non-green
      run 1 → auto +2 → median IN_DOSSIER, all 4 green, no manual confirm-rerun. Kills
      the recurring false-PARTIAL friction (iters 17/20/23/26).

## P2 — game completeness (feature backlog)
- [~] MISSION REPORT screen at finish/death — v1 SHIPPED (iter15). `CG_DrawMissionReport`
      in cg_draw.c draws a NERV-terminal scorecard while dead (TOP SPEED, STYLE PB,
      MAP BEST, an S/A/B/C RANK graded on peak speed, "FIRE TO RUN AGAIN"). Uses the
      LED matrix font + nerv palette. Extended iter22 (shop) + iter24 (per-RUN style:
      new `cg.lifeStylePeak`, peak styleScore this life, cleared on respawn → report
      shows "STYLE n (PB m)") + iter29 (BEST LAP line via CG_GhostBestMs — the saved
      ghost's finish time, the speedrun currency, green when set / "--:--" when not).
      Still TODO: medal vs PAR (needs par calibration below); one-button quick-retry is
      effectively done (Q3 fire-to-respawn) — "FIRE TO RUN AGAIN" already labels it.
- [ ] Auto par/medal calibration from best bot run per skill tier.
- [~] Run mutators (iter26): `g_mutator` cvar (SERVERINFO|ARCHIVE) — 1 LOW-G
      (gravity x0.55, floaty surf), 2 RUSH (speed x1.4), 3 HEAVY (gravity x1.35 +
      speed x1.15), applied per-frame in ClientThink_real, announced on spawn. Proven
      working: RUSH raised bot avgspd 256->313. NO-REG: no-op at default 0. HUD readout
      DONE (iter28): cgs.mutator parsed from serverinfo (cg_servercmds.c), CG_DrawMutator
      shows the active twist top-centre in the matrix font, tinted by feel. Daily
      rotation DONE (iter30): `g_mutator 9` = "daily" — G_InitGame resolves it to a
      concrete 1..3 by `trap_RealTime().tm_yday % 3` and writes it back (same day =
      same twist worldwide, fresh daily). Verified: day 165 -> LOW-G. TODO: the
      in-game "pick 1 of 3" between-run UI; more mutators (vectorgun-only, ghost swarm).
- [x] Failure heatmaps (iter25): G_PlaytestDeath now logs deathx/deathy; viz.py
      overlays death markers (red X = void/fall, magenta X = combat) on the tracer/
      heatmap, with a count in the title. Clusters reveal killer geometry. Validated
      on dojo_ztrick (7 deaths plotted). Pure telemetry — no gameplay/bot impact.
- [ ] More dojo archetypes as the design grows.

## Done log
- 2026-06-15 COMBAT AT SPEED (user req "flow + combat at same time"). After two
  failed approaches (air-strafe arcs regressed movement; orbit-bhop circle-danced
  slow — both reverted), the WIN: `bot_combatBhop` (default ON; movement dojos force
  off so combat can't perturb pure-traversal tests). In a fight bots bhop-RUSH the
  enemy (forward+jump builds speed on the rehop chain, aim already tracks target)
  and PEEL through point-blank with a committed strafe (g_botPass) so momentum
  carries past for another fast pass — not a standing gunfight, not an in-place
  orbit. ARENA: avgspd 141->245, peak 264->408, time-at-400+ups 1%->28%, flow
  10->28%, midair fragging. Movement courses untouched (all IN_DOSSIER). Also kept:
  bot view turn-rate cap (360->160 deg/s while travelling) for less snap, no-reg.
  Finding logged: "smooth arcs vs speed are in tension (the bhop S-curve IS speed)";
  pure path-smoothness is a feel knob for human tuning, not a clean auto-win.

- 2026-06-15 iter40: DAILY LAUNCHER. `./run-openarena.sh -daily` generates + deploys
  today's date-seeded surf circuit (surf_daily_<UTC>) and drops you straight in with
  g_mutator 9 (the day's rotating twist) — one command for the unified daily-speedrun
  run, completing the iter33 daily-surf integration. Verified gen+deploy+boot (daily
  mutator → LOW-G, AAS init). Launcher + strafegen only, no game code, no regression.
- 2026-06-15 iter39: BOT LAP-COMPLETION — investigation CONCLUDED (left open by design).
  Two bounded attempts, both 0 finishes: (a) short 2-ramp surf line (bots surf it at
  422 ups but still fail the transition — ramp count isn't the issue); (b) transition
  pop-off (probe ahead, jump at ramp's end) — 0 finishes AND lowered avgspd 422→346
  (popping mid-surf loses the line), reverted as net-negative. CONCLUSION: reliable bot
  lap completion defeats simple heuristics → genuine multi-iteration surf-AI research,
  for modest payoff (surf auto-testing) since human playtest already validates surf feel.
  The bots-SURF milestone (445–646 ups) stands. Kept: clean iter36 surf control +
  SurfLine `ramps=` param. dojo all 4 IN_DOSSIER, no regression.
- 2026-06-15 iter38: BOT LAP-COMPLETION diagnosed (not solved). Instrumented the race
  triggers: 8 start-stamps (raceStartTime stamping WORKS for bots) but 0 finish-touches
  even with the trigger reaching back 1536 — bots reach only ~x5975 vs finish >x7511, so
  they fall short by >1 full ramp. Root cause = surf-control loses the line on the LATER
  ramps (each transition a failure point), NOT trigger/raceStart geometry. Reverted the
  1536 band-aid (→ -256), kept the iter37 tall fly-over trigger, stripped debug. qagame
  clean, dojo all 4 IN_DOSSIER. CONCLUSION: full bot completion needs multi-ramp surf-
  control robustness — a real multi-iteration effort (improve transition hand-off /
  forward-progress in BotSurfControl, or a short 2-ramp surf dojo variant bots can clear).
- 2026-06-15 iter37: FINISH-TRIGGER FLY-OVER fix. The race finish was a 256-tall floor
  box; a surfer (bot OR human) arrives airborne at 600+ ups and flies clean over it —
  no finish, no lap. Now a tall full-corridor wall (lo_z-64..+768) reaching back over
  the last ramp tail (fx0-384); start trigger 128→256. Real fix for fast HUMAN runs.
  Bot lap-completion still open: 0 finishes / 0 "Race:" lines → race_finish_touch never
  logs → raceStartTime likely not stamped for surfing bots (deeper, not chased mid-
  playtest). Bots DO surf the full line (x reach ~6000, 392–646 ups). strafegen-only, no-reg.
- 2026-06-15 iter36 (USER REQ "learn the bots how to surf"): BOTS CAN SURF.
  `BotSurfControl` (ai_main.c, called pre-view in BotUpdateInput): airborne over a
  steep (<0.7 normal.z) face → hold strafe INTO the bank relative to the goal heading,
  level pitch, suppress jump (stay on the face); BotApplyMoveset applies it. Surf line
  gets item-bait midline pickups (AAS can't path a surf face → lead bots with items)
  + 4 spread spawns. Debugging arc: first attempt aimed view at momentary velocity
  (reinforced sliding off) → switched to goal heading; then "0 speed / 66 deaths"
  looked like total failure but was SINGLE-SPAWN TELEFRAG — spread spawns → bots build
  445–646 ups, 0–3 deaths. qagame built; dojo all 4 IN_DOSSIER (surf control is a
  no-op on flat dojo courses). Remaining: bots surf but don't reliably finish a lap.
- 2026-06-15 iter34 (PLAYTEST FIX): surf pop-off. Gustav playtested surf_64 — "works
  but hard to pop off the surface." Root cause: a steep surf slope is groundPlane &&
  !walking (airborne-in-contact), and the air jump only refunds on `walking`, so once
  the single air jump is spent getting onto the ramp the surface holds you. Fix:
  PM_UpdateMovementTimers refunds STAT_AIRJUMP_COUNT every frame while groundPlane &&
  !walking, so jump always pops you off; free-fall keeps the 1-cap (bhop unaffected).
  qagame+cgame (shared bg_pmove) rebuilt + deployed. NO-REGRESSION CONFIRMED (iter35):
  all 4 dojo archetypes IN_DOSSIER first-pass — bots don't touch steep surf slopes on
  the dojo courses so the refund never fires for them. The human-feedback loop reopening
  concrete tuning, exactly as intended. Pending: does the pop feel right (impulse 300)?
- 2026-06-15 iter33 (fast): DAILY SURF CIRCUIT — unifies the three core systems.
  `--daily --surf` generates one date-seeded SurfLine named surf_daily_<YYYYMMDD>
  (was: the daily was a tower course, disconnected from the surf core loop). Paired
  with g_mutator 9 (daily rotation), the daily is now: a date-seeded surf circuit +
  a date-rotated twist, same worldwide, fresh each day. Verified: surf_daily_20260615
  (4 ramps) boots clean with AAS + "daily mutator -> 1" (LOW-G). NO-REG: strafegen +
  the existing daily-mutator path, no new engine code. Follow-up: default the daily
  launch (run-openarena.sh) to --daily --surf + g_mutator 9.
- 2026-06-15 iter32 (fast): BANKED SURF TURN candidate. `SurfTurn` class + `--surfturn`
  builds a steep banked 180° arc (53°, velodrome inner-low/outer-high via make_prism)
  with entry/exit pads + race start/finish + lap teleport + sky box. Adapts the Arena
  velodrome banking to a surfable angle. Generates a valid engine-loadable map (25
  brushes, boots clean as surfturn_64; fixed an intermission-outside-skybox leak; no
  AAS since bots can't surf an arc). FEEL human-gated → in HUMAN_PLAYTEST.md. If it
  rides, fold arc() into SurfLine for 2D circuits. NO-REG: strafegen-only, no engine code.
- 2026-06-15 iter31 (consolidation): HUMAN_PLAYTEST.md — the autonomous backlog has
  largely converged; the bottleneck is now human validation of ~16 iters of features.
  Wrote a top-to-bottom playtest guide (launch; surf core loop; lap/points/shop;
  persist; airjump; mission report; mutators; moveset; identity/juice; self-serve
  diagnostics), each item with how-to-trigger + what-to-check. Deployed all 4 surf
  seeds (64/7/1337/2025) to baseoa/maps so the instructions work. Docs-only, no build.
- 2026-06-15 iter30 (fast): DAILY MUTATOR ROTATION. `g_mutator 9` sentinel resolved
  in G_InitGame via trap_RealTime (tm_yday % 3 + 1) and written back, so the daily
  map gets a deterministic per-day twist (same worldwide, fresh each day) — ties run
  mutators into the daily-speedrun identity. qagame built clean; FUNCTIONAL: boot with
  g_mutator 9 logged "daily mutator -> 1 (day 165)". NO-REG: block only runs at the 9
  sentinel (default 0 untouched); dojo all 4 IN_DOSSIER (ztrick/arena auto-median-recovered,
  the iter27 self-stabilizing gate working as designed).
- 2026-06-15 iter29 (fast): MISSION REPORT race-time line. BEST LAP added via the
  existing CG_GhostBestMs() (saved-ghost finish time) — green when set, "--:--"
  otherwise; plate grown to fit. The speedrun currency now shows on the death screen
  beside speed/style/rank. cgame built clean + deployed. NO-REG: cgame-only. The
  report's only remaining TODO is medal-vs-PAR (gated on par calibration).
- 2026-06-15 iter28 (fast): MUTATOR HUD readout. cgs.mutator parsed from serverinfo
  (cg_servercmds.c, mirrors cgs.vectorgun); CG_DrawMutator draws the active twist
  top-centre in the matrix font (green LOW GRAVITY / amber RUSH / red HEAVY), called
  in CG_Draw2D alive branch. cgame built clean + deployed. NO-REGRESSION: cgame-only.
  Mutator feature now visually complete (apply + announce + persistent readout).
- 2026-06-15 iter27 (fast, dojo-health): MULTI-RUN MEDIAN aggregation in dojo.py.
  Non-green archetypes auto re-run +2 and classify on the element-wise median
  (median_profiles); green ones stay single-run; run tags idx-isolated. Validated:
  ARENA flapped → +2 → median IN_DOSSIER, all green, no manual confirm-rerun. The
  no-regression gate is now self-stabilizing — ends the false-PARTIAL friction that
  cost a manual rerun in iters 17/20/23/26. dojo.py-only, no game code.
- 2026-06-15 iter26 (fast): RUN MUTATORS (P2) core. `g_mutator` cvar + per-frame
  gravity/speed twists in ClientThink_real (1 LOW-G x0.55g, 2 RUSH x1.4 spd, 3 HEAVY)
  + spawn announce (g_client.c). qagame built clean. NO-REGRESSION: switch is a no-op
  at default 0 (dojo green bar one maxspd-noise PARTIAL, confirmed: probe at mut=0 gave
  maxspd 464). FUNCTIONAL PROOF: probe mut=2 RUSH raised bot avgspd 256->313, maxspd
  464->491 — system works end to end. TODO: pick-1-of-3 UI + HUD readout.
- 2026-06-15 iter25 (fast): FAILURE HEATMAPS (P2). G_PlaytestDeath logs deathx/deathy
  (telemetry only, gated by g_playtest); viz.py render() overlays death X-marks
  coloured by cause (red void/fall, magenta combat) + a "(n void)" count in the title
  + legend. Reveals WHERE runs end → killer geometry for map design. qagame built
  clean; validated on dojo_ztrick (7 deaths plotted). NO-REGRESSION: telemetry print
  only, can't affect bots/gameplay; viz is offline tooling.
- 2026-06-15 iter24 (fast): MISSION REPORT per-RUN style. New `cg.lifeStylePeak`
  (cg_local.h) tracks peak styleScore this life (updated in CG_UpdateCombo beside
  styleBest, cleared in CG_Respawn); report line now "STYLE n (PB m)" — this run vs
  session best, instead of only the session PB. cgame built clean + deployed.
  NO-REGRESSION: cgame-only (dojo loads qagame). Small honesty fix to iter15.
- 2026-06-15 iter23 (fast): MOVEMENT-KIT shop item `buy airjump` (800) → permanent
  +1 air jump. New PMF_AIRJUMP_BONUS (bit 4, was free, collision-checked) read in
  PM_CheckAirJump (effective max = pm_airJumpMax + flag); boughtAirJump field
  (g_local.h) re-grants the flag on spawn (persists across death); shop entry in
  g_cmds.c + cgame report row. qagame+cgame built clean (bg_pmove shared → cgame
  prediction matches). NO-REGRESSION: flag never set for bots (no buy) so their
  air-jump cap is unchanged — confirmed: 1st run FLOW PARTIAL (29/2500, one stuck
  bot), re-run FLOW IN_DOSSIER (53.8/1355). Gameplay = human playtest.
- 2026-06-15 iter22 (fast): SHOP HUD on the death screen. CG_DrawMissionReport
  (cg_draw.c) extended with a SCORE line + the shop list (cg_shopItems[] mirrors
  g_shop[]), each row coloured by state: green affordable / amber OWNED / dim can't-
  afford, using STAT_WEAPONS + PERS_SCORE from the snapshot. Plate grown to fit.
  Makes the core-loop spend discoverable at the moment you respawn. cgame built
  clean (ARRAY_LEN/WP_* resolve). NO-REGRESSION: cgame-only, dojo loads qagame only
  -> no-op. Deployed+signed; layout mock rendered for review. Buy still via console.
- 2026-06-15 iter21 (fast, dojo-health): recalibrated FLOW `stuckms` band 1500→2000
  in dojo_dossier.json + DEFAULT_DOSSIER, from the observed good-behaviour distribution
  (n=19, good runs 850–1900, broken-nav 2388+). Kills the false PARTIALs that cost a
  confirm-rerun each feature iter. Verified FLOW green 1507/1118. Surfaced the deeper
  issue (ARENA/SPEED combat metrics flap at short dur) → filed multi-run-median
  aggregation as the next dojo-health task. No code/dylib change.
- 2026-06-15 iter20 (fast): PERSIST PURCHASES across death. New `boughtWeapons`
  bitmask field (g_local.h); Cmd_StrafeBuy_f sets it on weapon buys; ClientSpawn
  re-grants the owned weapons + 20 ammo each on respawn. Weapons = permanent
  session unlocks; armor/heal stay per-life consumables (clean split). qagame built
  clean. NO-REGRESSION: FLOW went PARTIAL twice (stuck 1557/1743 vs 1500) but the
  re-grant is dead code for bots (boughtWeapons==0, guarded) so it CANNOT affect
  them — confirmed band-edge noise, not regression (see Dojo health note). SPEED/
  ZTRICK/ARENA green. Makes the shop meaningful through wipes.
- 2026-06-15 iter19 (fast): ★ PROCEDURAL SURF CIRCUITS. `SurfLine.build()` rewritten
  from fixed 2-ramp v0 to a seed-driven chain of N=3–5 ramps. Key trick: one shared
  Y-corridor + alternating bank → each ramp's low exit edge == next ramp's high entry
  edge, so any-length chain stays flush on the player's path with no realignment.
  Angles seed-varied 49–55° (surfable/non-walkable), lengths/drops randomized. Verified
  seeds 64/7/1337/2025: all valid sealed BSP + bspc .aas; surf_1337 (5 ramps) boots
  clean (AAS init, no leak). NO-REGRESSION: offline map-gen only, no engine code, dojo
  doesn't use surf maps. TODO: feel-tuning + banked turns (true closed circuit).
- 2026-06-15 iter18 (fast): ★ LOADOUT SHOP (core-loop component 3) — the loop
  closes. `buy` command (Cmd_StrafeBuy_f, g_cmds.c) + g_shop[] table spends
  PERS_SCORE on weapons/armor/heal, with afford + already-own guards; registered
  in ClientCommand. Lap centerprint now hints "type buy to spend". qagame built
  clean, dojo ALL 4 IN_DOSSIER (buy is unreachable by bots → no-op for them),
  deployed+signed. Purchase flow itself = human playtest (no client headless).
  ★ THE v0 SURF CORE LOOP IS COMPLETE END-TO-END (iter16 surf+lap, 17 points,
  18 spend, 15 report). Rest is depth/feel/tuning.
- 2026-06-15 iter17 (fast): ★ LAP → POINTS (core-loop component 2). `race_finish_touch`
  (g_trigger.c) banks PERS_SCORE per lap (lap 100 + speed 3M/ms cap 500 + best 250),
  counts `client->raceLaps`, centerprints LAP/time/+award/TOTAL. New `raceLaps` field
  in g_local.h. qagame built clean. NO-REGRESSION: first dojo run showed FLOW/ARENA
  PARTIAL, but a confirm re-run returned ALL 4 IN_DOSSIER (FLOW stuck 1050, ARENA
  midair 60) — variance, not regression (ARENA has no race trigger so the code can't
  even run there; bots don't cross finishes; AddScore/centerprint don't alter physics;
  metrics historically noisy). Finish now = score + respawn-at-start = the lap loop.
- 2026-06-15 iter16 (fast): ★ SURF LINE v0 — the headline core-loop seed.
  `SurfLine` class + `--surf` in strafegen.py. Two 51.3° banked `make_prism`
  surf ramps (surf+Y → transition → surf−Y), 1792u descent, finish teleports
  back to start = a complete (rough) lap loop. Verified: valid sealed BSP (14
  brushes, check_bsp OK), bspc built .aas, ioq3ded boots it clean ("loaded
  maps/surf_64.aas / AAS initialized", no leak). Deployed to baseoa/maps;
  schematic SVG rendered for review. NO-REGRESSION: new generator path + new
  map only, touches nothing the dojo archetypes use. Surf FEEL = human playtest
  (`\map surf_64`). Next: tune ramp angle/hand-off, then procedural circuit.
- 2026-06-15 iter15 (fast): MISSION REPORT v1. `CG_DrawMissionReport` (cg_draw.c)
  — death-state NERV scorecard (TOP SPEED / STYLE PB / MAP BEST / S-A-B-C RANK on
  peak speed / FIRE TO RUN AGAIN), backing plate + accent rule, LED matrix font.
  Hooked at end of CG_Draw2D gated on STAT_HEALTH<=0 & !spectator & !intermission.
  cgame compiled clean (only pre-existing warnings), deployed+codesigned the dylib
  set. NO-REGRESSION: change is cgame-only & dojo loads qagame only (vm_game 0), so
  structurally zero bot-baseline impact — full dojo battery skipped as a no-op.
  Needs HUMAN playtest to view (HUD only renders in-client on death; ioq3ded is
  headless). Next: per-run style capture + race-time line + par/medal calibration.
- 2026-06-15 iter1: item-bait nav goals → FLOW dojo unblocked (gate PARTIAL).
- 2026-06-15 iter2-3: tuned bspc -cfg for "capability awareness" → REGRESSED
  (FLOW→0). Rejected by no-regression, reverted. Lesson: FLT_MAX default beats
  a realistic AAS physics model for enhanced-moveset bots. (aas_strafe64.cfg
  kept for reference, unused.)
- 2026-06-15 iter4: revert confirmed → FLOW avg 325ups / flow 76% (PARTIAL,
  exceeds flow target); SPEED gate now passes (OUT); ZTRICK still BROKEN.
- 2026-06-15 iter14 ★ MILESTONE: ALL 4 ARCHETYPES IN_DOSSIER. Diagnosed that
  completion/wallrun/800ups/killspd-320 are HUMAN-skill metrics (bots never
  cross the finish — 0 race lines, they fall on precision gaps; don't strafe-
  jump/wallrun). Split the dossier: BOT = traversal-quality regression
  baselines (achievable, catch stalls/slowdowns), HUMAN = aspirational (ROADMAP,
  not gated). Recalibrated dojo_dossier.json + FLOW void off (respawn-stall was
  inflating stuck). Bot substrate complete + regression-protected. Loop pivots
  to P2 game features + the ★ surf core loop.
- 2026-06-15 iter13 (fast): finish-bait (didn't yield completions) + bumped
  movement archetypes to 5 bots. ZTRICK OUT→PARTIAL (stuck 4225→640, under
  target!); metrics stabilized (8-10 runs vs 2-3, less variance). Now 3/4
  archetypes PARTIAL (FLOW/ZTRICK/ARENA), SPEED still OUT (needs bot air-strafe).
  Universal remaining miss: completion 0% — bots don't formally cross the race
  finish (chase items, don't "race"). LIKELY A HUMAN METRIC — consider dropping
  completion from the bot dossier or measuring traversal-distance instead.
- 2026-06-15 iter12 (fast): ZTRICK unblocked. The double-jump TOWER was the
  bot-blocker (bots can't chain double-jumps up steps) — swapped it for sec_bhop
  in the recipe. stuck 4225→1300ms, avgspd 201→378, flow 32→70%. Now OUT (near
  dossier) not BROKEN. Remaining: stuck 1300→≤1000 + completion 0% (bots don't
  cross the race finish — cross-archetype "bots complete the race" gap).
- 2026-06-15 iter11 (fast): tuned pit (W 640→448, spawns r360→280, 12 bots).
  frags/min 0.6→1.5, killspd 13→100, midair 100% (17 runs). BUT tightness spiked
  stuck-time (bots bump walls) and killspd 320 looks human-aspirational (bots
  camp-rail, don't frag at run speed). Open questions: relax arena killspd
  target OR add boost pads to fling bots; frags/min→3 needs more bots/openness.
  Arena is functional combat now; deferring further arena tuning for ZTRICK.
- 2026-06-15 iter10: COMBAT PIT. New `Pit` strafegen class (sealed box, close
  spawns, cover, weapons) for the arena dojo. ARENA BROKEN→PARTIAL: bots frag
  (2 in 22s, was 0), midair 50% (>target). aas clean. Remaining: frags/min +
  killspd tuning. First archetype combat to actually work.
- 2026-06-15 iter9 (fast): propagated +addbot fill to viz.py + playtest.py
  (all 3 harness tools now fill reliably; viz tracer denser, 66KB). Confirmed
  10 bots on the velodrome = STILL 0 frags → combat-deadness is geometry, not
  count. NEXT (focused build): a tight combat-pit arena dojo (small box, cover,
  close spawns) replacing the velodrome for the ARENA archetype.
- 2026-06-15 iter8 (fast): bot-fill fix. dojo.py uses `+addbot` with real OA
  bot names (BOT_NAMES) — 5 bots fill vs 2 from bot_minplayers; verified FLOW
  PARTIAL + a midair frag, no regression. Arena still spread-out (needs tight
  pit). Thickens all future dojo metrics.
- 2026-06-15 iter7 (fast): diagnosed arena 0-frags. Not weapons (g_vectorgun
  wired into arena archetype, no change) nor spawns (arena has 8) — the
  DEDICATED server only fills ~2 bots, so they never meet on the velodrome.
  Affects all dojo runs (thin bot counts). Next: explicit +addbot fill once OA
  bot names are located. dojo.py now supports per-archetype "extra" cvars.
- 2026-06-15 iter6: VAULT/mantle implemented (PM_CheckVault), no-regression OK.
  BIG infra lesson (cost most of the iteration): a "qagame crashes on load"
  hunt turned out to be **`SIGKILL (Code Signature Invalid)`** on Apple Silicon
  — a plain `cp` of a dylib invalidates its ad-hoc signature, so dlopen kills
  the process. Fix: EVERY deploy now `codesign -f -s -` the copy (dojo.py,
  viz.py, playtest.py, run-openarena.sh). Verified the crash report via
  `~/Library/Logs/DiagnosticReports/*.ips` (parse JSON, faultingThread frames).
- 2026-06-15 iter5: HEATMAPS + TRACERS. Trail telemetry (`g_playtestTrail`,
  100ms position samples) + `strafegen/viz.py` -> top-down SVG (speed-coloured
  tracers + time-spent heatmap + spawn marker). dojo_flow trace confirms bots
  flow up the course at 300-424 ups. Lesson (cost me an hour): a "qagame hangs
  on load" scare was FALSE — block-buffered stdout truncates at "Loading DLL
  file", and telemetry only writes on death/finish/clean-quit-flush. Always
  clean-quit (stdin "quit") + wait >=12s; don't trust truncated logs; use
  `sample <pid>` / poll liveness to tell hang from early-kill.

## Loop discipline (from crypto-autoresearch hat)
- **Sanity gate first** — never tune against a `BROKEN` (gate-failed) scenario.
- **No-regression** — a change is accepted only if it helps its target
  archetype and regresses no other out of band.
- **Journal everything** — `dojo_runs.jsonl`, git + label per iteration.
- **One knob at a time** — isolate cause.
