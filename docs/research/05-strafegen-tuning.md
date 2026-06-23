# STRAFE 64 — Arena Generator Tuning Spec (Synthesis)
> Consolidates docs 01–04 into concrete, actionable targets for `tools/strafegen/strafegen.py`
> and the `engine_playtest_report` loop. This is the working spec for tuning procedurally
> generated arenas. Units = Quake units (u) / ups; our constants: run 320, jump 300, gravity
> 1000 (apex ~46u), DJ ledge ~92u, wall-jump 200/250 (×2), slide-min 250, bhop ≤1.10×.

## The one-line thesis
**Make the fast line and the aggressive line the same line.** Geometry should deliver the
player into melee *while moving* (speed = reach = damage = time), never reward stopping, and
recycle momentum so a sealed arena never dead-ends. Diversity comes from *archetypes*
(structural niches), proven by *expressive-range analysis*, and tuned by *bot-telemetry
fitness*.

## A. Geometry targets (consolidated)
| Domain | Knob | Target | Source |
|---|---|---|---|
| Shell | Killbox footprint | duel-feel pockets 600–1000u across; full box ~2816u OK for FFA | 01,03 |
| Shell | Ceiling / vertical use | ≥256u over fight zones; ensure reachable play uses z 256–900u, not just deck | 01,02 |
| Topology | Routes per tier | ≥2 distinct (pad + wall-jump/portal); catwalk = full loop; no dead ends | 01 |
| Openings | Door/gap width × height | ≥96u × ≥128u; corridors ≥128u | 01 |
| Cover | Micro-cover cadence | LOS break every ~256–512u; centerpiece must break cross-box sightline | 01,03 |
| Pillars | Juke-post clear radius | 150–250u clear floor around each | 03 |
| Pillars | Spacing (wall-jump chain) | 300–500u apart; facing-wall gaps 150–400u | 02,03 |
| Platforms | Slab gap | 150–220u (run) … 300–460u (flow/peak, slight drop) | 02 |
| Platforms | Height delta | ≤92u between climbable slabs (else pad/wall-jump assist); weave in/out AND up/down | 02 |
| Walls | Wall-run runway | ≥256–512u long, ≥128u tall, near-vertical (±15°) | 02 |
| Ramps | Surf/revector angle | 30–50°; bounce-corner chevrons at 45°; NO tilted floor-bank | 02 |
| Air combat | Elevated platforms | 1–2 at 150–300u above floor, each pad/wall-jump reachable | 03 |
| Fairness | Per-gap routes | one route ≤190u single-jump@run-speed; optimal route may need DJ/wall-jump/pad | 02 |
| Momentum | Portal placement | discrete gates at catwalk height; dest a few hundred u inward of far wall (no full-wall fields) | 01,02 |
| Landings | Bhop continuity | flat/downslope, obstruction-free near pads & gate exits | 02 |

## B. Item & combat-beat placement
| Knob | Target | Source |
|---|---|---|
| Item set (melee mode) | health / armor / mega / quad only — NO guns/ammo | 01,03 |
| Respawn timing | mega-analog ~35 s, armor ~25 s, quad ~120 s | 01 |
| Item layout | distribute around catwalk+deck so a lap ≈ item cadence (the "rotation") | 01 |
| Quad / power pickup | at the end of the most-exposed / most-committed (fastest) line — greed = speed | 01,03 |
| Slice-gate validity | reject any gate whose only approach is <320 ups (standing slice = ~0 dmg) | 03 |
| Gate placement | at apex (pad/wall-jump peak), landing, or along a fast straight; never a dead stop | 03 |
| Gate approach angle | diagonal ~30–60° off head-on, so the player arcs through the moving 120–180u reach shell | 03 |
| Flow-phrase spacing | 2–3 gates 120–200u apart along a fast line (chain time-surges) | 03 |
| Density / wave shape | per section escalate 1→1→2→3→5; openers/rest = 0; cap ~5 simultaneous in a 600–1000u pocket | 03 |
| Beat cadence | one combat beat every ~2–4 s of run, at apex/landing only | 03 |
| Anti-turtle | every cover has a flanking lane; no enclosed safe nook; hot/dissolving floor in dwell spots | 03 |
| Spawns | ≥8, perimeter, facing in, off items & columns; inner ring on diagonals; clearance ≥ player radius + margin | 01 |

## C. Target telemetry bands (how to grade a generated arena)
From `engine_playtest_report` (5 Assassin bots, 35 s, skill 4). **Arenas are graded on
combat + air + flow-of-motion, NOT race flow%** — low flow% is correct for a vertical box.

| Metric | Good (keep) | Warn (tweak) | Bad (discard / bug) |
|---|---|---|---|
| Frags (35 s, 5 bots) | ≥5 | 2–4 | 0 (no combat / pinball) |
| Midair % | 55–85% | 40–55% / >90% | ~98% + 0 frags = portal pinball |
| Avg max speed | ≥500 | 350–500 | <300 (turtling / blocked) |
| Wall-jump / bhop usage | >0, varied | near-0 | 0 across all bots |
| Stuck ms | <400 | 400–800 | >1000 (spawn-in-solid / nav trap) |
| Deaths by cause | mostly MOD_SWORD | some MOD_FALLING | telefrag-heavy / trigger_hurt loops |
| AAS present | yes | — | missing → un-bot-testable, regenerate/seed-swap |

Observed baselines (overnight set): kb_7001 spire, kb_8224 ring (6 frags / stuck 307),
kb_8228 spiral (689 speed), kb_9000 forest (8 frags) — all keepers; the no-AAS velodrome
arena (dm_7140) was the discard.

## D. Validity gate (generate-and-test)
Reject or repair before a map "counts":
1. **Reachability** — every spawn/item/tier reachable (graph/AAS BFS).
2. **Spawn clearance** — no spawn inside/against a column/spire/pad footprint (`avoid_footprints`; clearance ≥ player radius + margin). *Done — keep.*
3. **AAS present** — pk3 contains a `.aas`; if bspc failed, swap seed (banked-velodrome Arena seeds often fail).
4. **No pinball portals** — discrete gates only; dest inset; reject full-wall portal fields.
5. **No mandatory >2 wall-jumps / >92u unassisted climb** — every required gap has a fair route.
6. **selftest** — `python3 tools/strafegen/strafegen.py --selftest` must pass (28 maps).

## E. Telemetry fitness (for search / ranking)
A starting scalar (or use as a Pareto set; don't over-collapse — see doc 04 pitfalls):
```
fitness =  1.0 * clamp(frags/8)
         + 0.6 * tri(midair_pct, lo=55, hi=85)      # tent peaks in the good band
         + 0.5 * clamp(avg_max_speed/700)
         + 0.3 * has_varied_moveset                  # wall-jump & bhop both >0
         - 0.8 * clamp(stuck_ms/1000)
         - 1.0 * pinball_flag                         # midair>90 & frags==0
         - 1.0 * (not aas_present)
         - 0.4 * clamp(telefrag_deaths/total_deaths)
```
Pair with a **diversity bonus** (novelty vs already-kept maps in metric-space) so search
doesn't converge all seeds onto one archetype.

## F. Diversity / expressive-range plan (answers "are maps actually different?")
1. Generate **N≥200 seeds** per archetype (and across knob settings).
2. Compute structural metrics per map: **verticality-used, openness/density, symmetry,
   route-redundancy, linearity, item-fairness**.
3. **Plot a 2-D expressive-range histogram** (suggest **verticality × openness**, plus a
   second on **density × symmetry**).
4. Check: do the 6 archetypes occupy **distinct regions** (good) or overlap (reskin)? Is any
   corner never reached (bias)?
5. **Run before/after** generator changes to *prove* a feature widened the range — e.g.,
   document the spread pre- vs post-archetypes.
6. Treat the archetype set as a **MAP-Elites grid**: cells = archetype × size × verticality
   bin; keep the best-telemetry seed per cell as the curated set (replaces ad-hoc keepers).

## G. Prioritized change list (next strafegen work)
1. **Sightline gate:** ensure each centerpiece breaks the cross-box LOS (ring/forest/cross/
   twin do; verify thin spire variants add a blocker) — kills the "one big power lane" risk.
2. **Item ring on the rotation:** distribute health/armor around catwalk+deck; quad on the
   most-exposed line. (Today items cluster on the spire crown.)
3. **Route redundancy check:** assert every tier has ≥2 access routes (pad + wall-jump/portal);
   add a wall-jump alternate wherever only a pad exists.
4. **Slice-gate combat layer for arenas:** optional `--combat` style apex/landing gates with
   the 1→1→2→3→5 wave shape and the <320 ups validity reject.
5. **Validity gate module:** fold reachability + AAS-present + pinball + spawn-clearance into
   one pre-count check; log any rejects.
6. **Metrics + expressive-range script:** compute the section-E/F metrics over a seed sweep,
   emit the histogram + diversity numbers (this is how we *measure* tuning, not eyeball it).
7. **Shell variety (later, careful):** scale archetype radii with shell size so tall-narrow
   "chimney" vs wide-flat "pit" shells don't break interior geometry.

## Cross-references
- [01 — Arena Map Design](01-arena-map-design.md): topology, scale, items, spawns.
- [02 — Movement Tech & Geometry](02-movement-tech-and-geometry.md): affordances & dimensions.
- [03 — Melee, Bullet-Time & Movement-Combat](03-melee-bullettime-combat.md): combat geometry & pacing.
- [04 — Procedural Arena Generation](04-procedural-arena-generation.md): methods, metrics, diversity, closed loop.
- Internal: `docs/MAP_DESIGN.md`, `docs/MOVEMENT.md`, `tools/strafegen/strafegen.py`, `tools/strafegen/FINDINGS.md`.
