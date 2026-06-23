# Movement Tech & Level Geometry — Research
> Research for STRAFE 64 procedural arena tuning. Sources cited at bottom.

How each movement technique is *afforded by geometry* — the concrete walls, gaps,
slopes and spacings that let the kit work — drawn from Quake/CPMA strafe-jumping &
surf, Titanfall 2's pilot kit, Doom Eternal, Ghostrunner and the surf/bhop communities.
Grounded in our constants: run **320 ups**, jump velocity **300**, gravity **1000**
(→ single-jump **airtime ≈ 0.6 s, apex ~46u**), double-jump ledge **~92u**, wall-jump
kick **200 horiz / 250 vert (max 2)**, slide min **250 ups**, bhop boost up to **1.10×**,
momentum portals preserve velocity.

### Quick reachability cheat-sheet (derived)
| Move | Vertical reach | Horizontal reach |
|---|---|---|
| Step-up (auto) | 18u | — |
| Single jump | climb ≤ **64u** | flat ≈ **190u** @320 ups; **~300u** @500; **~460u** @768 |
| Double jump | climb ≤ **92u** | + a second arc mid-air (extends gap by ~one jump) |
| Wall-jump | +**250u** vz, +**200u** horiz per kick (×2) | ricochet between faces **~150–400u** apart |
| Jump pad | designer-set (we throw to catwalk ~400u+) | designer arc |
| Slide-jump | ×1.08 speed into the jump | longer flat arc the faster you enter |

## TL;DR
- **Geometry must never kill momentum.** Sharp 90° walls into the travel line, abrupt
  stops, and friction traps are the cardinal sins of a movement map. Curves, ramps,
  and openings preserve speed.
- **Strafe-jumping wants open, gently-curving space.** Speed is gained by air-accel into
  a turn; long sweeping arcs and circular routes let a player keep accelerating, while
  tight switchbacks bleed it.
- **Bhop chains skip ground friction** — landing zones should let you jump *immediately*
  on touchdown (flat or downward, not into a wall), and our bhop adds up to **1.10×** per
  clean chain.
- **Wall-running needs long, tall, vertical walls placed in series.** A wall worth running
  is **≥256–512u long, ≥128u tall**, near-vertical (±~15°), with the *next* wall reachable
  from the end of the current run.
- **Wall-jumping needs facing surfaces ~150–400u apart.** Our 200u horiz / 250u vert kick
  lets a player zig-zag up a column gap or chimney; columns spaced in that band chain.
- **Gaps are sized to *intended* speed, not run speed.** A flat single jump clears ~190u
  at 320 ups but ~300u at flow (500) and ~460u at peak (768) — so a gap meant to be taken
  *fast* can (and should) be wider than a walk-up gap.
- **Ledges obey the jump ladder:** ≤64u = single jump, ≤92u = double jump, higher = needs a
  pad or wall-jump. Step ≤18u is free.
- **Surf/revector wants 30–50° banked faces.** Our bounce-corner chevrons (45° walls you
  revector off) are correct; a tilted *floor*-bank reads wrong-way and kills flow (do not
  re-add it).
- **Slabs that weave in/out AND up/down force a fresh air-strafe each hop** — far better
  than a flat line of evenly spaced pads (which becomes a metronome, not a skill).
- **Pace rest vs intensity.** A run needs low-intensity connective stretches (build speed,
  breathe) between high-intensity sections, or it's exhausting noise.

## Strafe-Jumping & Bhop
**Mechanic.** In the Quake family, air acceleration is capped per-frame but *re-applies in
the direction you're looking relative to your velocity*; by holding a strafe key and
smoothly turning into it, you add speed every frame you're airborne. VQ3 air-control is
narrow (you must zig-zag); CPM-style adds W-only air-control (you can curve on a single
key). Bhop = chaining jumps so you never touch ground long enough to lose speed to
friction.

**Geometry that affords it:**
- **Open lanes and sweeping curves.** A long, gently curving corridor or an open ring lets
  the player keep turning-into-velocity. **Sharp corners (90°+) bleed speed** — replace
  them with rounded/chamfered turns where a fast line matters.
- **Circular / looped routes** are ideal accel tracks (the velodrome idea): a continuous
  arc the player rides to build speed.
- **Clean landing zones for bhop:** the touchdown spot must be flat or sloping *down/forward*
  and free of obstructions so the next jump fires on the 3-frame window; landing into a
  step or wall breaks the chain.
- **Run length to reach flow:** to hit 500 ups (flow) from 320 takes a few good strafe
  jumps — give **≥512–1024u** of uninterrupted accel space before any gate that needs flow
  speed.

## Surf & Ramps
**Mechanic.** Riding a steep ramp, the player slides along it and uses air-accel against
the slope to gain speed — the basis of surf maps. Slope angle matters: too shallow and you
just walk it; too steep and you can't hold it.
- **Surf faces: ~30–50°.** Banked *walls/ramps* you ride sideways.
- **Revector chevrons (our bounce-corner): 45° walls** placed as corner chevrons that bounce
  a fast line back into the arena — preserves and redirects momentum.
- **Anti-pattern:** a tilted *velodrome floor-bank* reads as "wrong-way," fights the player's
  read of the line, and kills flow. **Do not re-add it** (already reverted once).
- Ramps also feed **slides:** entering a downslope at ≥250 ups maintains/extends the slide.

## Wall-Running (Titanfall-style)
**Mechanic.** The pilot runs along a near-vertical wall for a few seconds, can jump off it,
and chains wall-to-wall. Titanfall 2's campaign is built as continuous wall surfaces so a
pilot is rarely without a runway.
- **Wall must be long enough to matter: ≥256–512u** (a short wall is a stutter, not a run).
- **Tall enough: ≥128u**, ideally taller so the run has vertical room.
- **Near-vertical (±~15°).** Heavily sloped faces become surf, not wall-run.
- **Chain spacing:** the *next* wall must be reachable from the end/jump-off of the current
  one — place opposing or staggered walls so wall-run → wall-jump → wall-run links.
- **"Every wall is a runway":** in a movement game, perimeter walls and tall columns should
  be runnable, not just collision. (STRAFE 64's kit centers on wall-*jump* more than
  sustained wall-*run*, but the same geometry — long tall vertical faces — serves both.)

## Wall-Jumping
**Mechanic (ours).** Kick off a wall for **+200u horizontal, +250u vertical**, up to **2**
times before landing. This is the vertical-recovery and chimney-climb tool.
- **Facing-wall gap ~150–400u.** Two walls/columns that close (a corridor, a column gap, a
  chimney) let the player ricochet upward; outside that band the kick can't bridge.
- **Corner columns / chimneys:** four columns near the corners (our killbox) give classic
  chimney climbs — kick corner-to-corner to gain the catwalk.
- **Pillar forests:** varied-height columns **~300–500u apart** let a player wall-jump
  laterally between them while keeping speed (juke + climb).
- **Cap awareness:** with only 2 wall-jumps, don't require >2 to clear any mandatory gap —
  always offer a pad/ramp alternate for non-experts.

## Double-Jump & Dash
- The **air-jump** extends a gap by roughly a second arc and lifts the climbable ledge to
  **~92u**. It's the "save" for a missed strafe and the reach extender.
- Keep at least one route across any gap doable with **single jump + run speed** (≈190u) so
  the gap is fair; reserve double-jump/wall-jump for the *fast/optimal* line.
- Dash/air-jump bursts reward committing a direction mid-air — pair with apex gates so the
  burst carries the cut.

## Slide
- **Slide entry ≥250 ups**; **slide-jump ×1.08** rewards sliding into a jump.
- **Downslopes maintain/extend slides** — a ramp into a gate lets the player slide-jump the
  gate at speed.
- Slide is low-profile (duck under sightlines) and momentum-locked (skips walk accel) — give
  it *flat or downhill* runways, not uphill ones (uphill kills it).

## Platform / Aerial Chains
- **Gap = intended speed × ~0.6 s airtime.** Size slab gaps to the speed you *want* there:
  ~**150–220u** for a run-speed hop, up to **300–460u** for a flow/peak-speed leap (with a
  slight drop the gap can be larger).
- **Height deltas within reach:** step ≤18, single ≤64, double ≤92; bigger rises need a pad
  or wall-jump assist.
- **Weave in/out (radius) AND up/down (height)** between slabs so each hop demands a *new*
  air-strafe direction — this is what makes our spiral-tower archetype sk{ill-expressive
  rather than a flat metronome of identical jumps.
- **Bait the chain:** a pickup (shard) on every other slab pulls the player along the line.

## Momentum & Flow
- **Preserve momentum end-to-end.** The "desire line" through a space should never hit a
  flat wall, a forced stop, or a friction trap. Curves over corners; openings over doors at
  speed; downhill/flat landings over uphill.
- **Rest vs intensity pacing.** Alternate low-intensity accel stretches (open, safe, build
  speed) with high-intensity sections (tight, vertical, combat). Constant intensity is
  fatigue; constant rest is boredom. Our flow loop (Descend → Tighten → Revector → Slice →
  Release) is exactly this cadence.
- **Momentum gates / portals** keep a sealed arena from dead-ending: a velocity-preserving
  portal turns a wall into a runway and recycles a fast line back into play.

## Implications for STRAFE 64
- **Make the catwalk loop a strafe-accel track** (gentle curve / full ring), with ≥512u of
  clean run before any flow-gated gate.
- **Corner columns & forest pillars spaced 300–500u** so wall-jump chains actually link;
  keep facing-wall gaps in the 150–400u kick band.
- **Spiral/aerial slabs: gaps 150–350u, height deltas ≤92u (or pad-assisted), weaving in/out
  and up/down** — never a flat evenly-spaced ladder.
- **Bounce-corner chevrons at 45°**, never a tilted floor-bank.
- **Every mandatory gap clearable with single jump + run speed (~190u);** reserve wider/
  taller lines for wall-jump/double-jump/pad (the skill route).
- **Momentum portals** at catwalk height as discrete gates, dest a few hundred u inward of
  the far wall (no full-wall fields → bots pinball).
- **Landing zones flat/downward** near pads and gate exits so bhop chains continue.

## TUNING KNOBS → strafegen
| Finding | Generator knob | Suggested value / target |
|---|---|---|
| Strafe-accel needs open curve + run length | Catwalk/loop geometry | full-ring loop; ≥512–1024u clean accel before a flow gate; chamfer sharp corners on fast lines |
| Bhop needs clean landings | Landing-zone check near pads/gate exits | flat or downslope, obstruction-free for ≥ one player length |
| Wall-jump chains | Column / facing-wall spacing | columns 300–500u apart; facing-wall gaps 150–400u; corner chimneys present |
| Wall-run runways | Perimeter/column wall faces | ≥256–512u long, ≥128u tall, near-vertical (±15°) |
| Aerial slab gaps sized to speed | Platform gap distance | 150–220u (run) … 300–460u (flow/peak, with slight drop) |
| Slab height deltas in reach | Platform vertical step | ≤92u between climbable slabs (else pad/wall-jump assist) |
| Weave forces re-strafe | Platform layout | alternate radius in/out AND height up/down per slab (spiral); never a flat ladder |
| Surf/revector angle | Bounce-corner chevron angle | 45° walls; NO tilted floor-bank (reverted) |
| Fair + skill routes | Per-gap route redundancy | one route ≤190u single-jump at run speed; optimal route may need wall-jump/DJ/pad |
| Slide runways | Ramp/slope direction into gates | flat or downhill into a gate; never uphill |
| Momentum recycling | Momentum-portal placement | discrete gates at catwalk height; dest a few hundred u inward of far wall |
| Rest/intensity pacing | Section sequencing | alternate open accel stretch ↔ tight/vertical/combat section |
| Telemetry — movement working | Bot playtest metrics | high midair %, wall-jump/bhop usage >0, avg max speed ≥500; flag avg speed <320 or stuck ms high |

## Sources
- [Strafe-jumping — Quake Wiki](https://quake.fandom.com/wiki/Strafe-jumping) — air-accel model, VQ3 vs CPM air control, turn-into-velocity to gain speed.
- [Bunny hopping — Quake/Source movement docs](https://quake.fandom.com/wiki/Bunny_hopping) — skipping ground friction via jump cadence; landing requirements.
- [Surfing (Source/GoldSrc) — Valve Developer Community](https://developer.valvesoftware.com/wiki/Surf_ramp) — ramp angles ~30–50°, air-accel against slope, banked-surface design.
- [Movement in Titanfall 2 — design talks & wiki](https://titanfall.fandom.com/wiki/Wall-running) — wall-run duration/chaining, "every wall a runway," level geometry built for traversal.
- [Ghostrunner movement design — developer interviews](https://en.wikipedia.org/wiki/Ghostrunner) — chaining wall-run/slide/dash; the traversal line IS the play.
- [DOOM Eternal movement & arena platforming — design analyses](https://en.wikipedia.org/wiki/Doom_Eternal) — monkey bars, jump pads, dashes; verticality woven into combat arenas.
- [The Level Design Book — Flow](https://book.leveldesignbook.com/process/layout/flow) — flow = speed + direction; smooth generous turns for fast games; never kill momentum.
- STRAFE 64 internal: `docs/MOVEMENT.md`, `tools/strafegen/strafegen.py` physics header (run 320, jump 300, gravity 1000, wall-jump 200/250, DJ ledge ~92u) and memories [[strafe64-bounce-corner]], [[strafe64-slide-feel]].
