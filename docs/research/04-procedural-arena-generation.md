# Procedural Arena Generation & Tuning — Research
> Research for STRAFE 64 procedural arena tuning. Sources cited at bottom.

How to generate AND *evaluate/tune* arena maps, drawn from the PCG literature
(Shaker/Togelius/Nelson; Smith & Whitehead; Togelius et al. search-based PCG; Mouret &
Clune MAP-Elites) and practical FPS generators (Oblige/Obsidian, SLIGE/SLUMP, WFC). The
through-line for STRAFE 64: we already have a constructive generator (`strafegen.py`) and
a simulation-based evaluator (the bot playtest). The gap is **diversity measurement** and
a **closed tuning loop** — exactly the concern behind "every map is the same, you only
change the seed."

## TL;DR
- **Three PCG families:** *constructive* (build once, correct by construction — what
  strafegen is), *generate-and-test* (build, check constraints, reject/repair), and
  *search-based* (evolve content toward a fitness function). We should move from pure
  constructive toward **generate-and-test + light search**.
- **Simulation-based evaluation is our superpower.** Running bots and reading telemetry
  (frags, midair %, stuck ms, speed, moveset use) is textbook *simulation-based fitness* —
  most generators can't do this. Lean into it as the objective signal.
- **"Reskin vs diverse" is an Expressive Range problem.** Smith & Whitehead's *expressive
  range analysis*: generate many maps, score each on 2+ structural metrics, and **plot the
  distribution**. A tight blob = reskins; broad, well-covered = diverse. This is how you
  *prove* the archetypes fixed sameness.
- **Archetypes ≈ a hand-built MAP-Elites grid.** MAP-Elites keeps the best solution in each
  cell of a "behavior" grid to *illuminate* a space rather than find one optimum. Our 6
  centerpiece archetypes are manual niches; formalize them with behavior axes and keep the
  best seed per niche.
- **Pick the right metrics.** Useful, computable arena metrics: *connectivity/reachability,
  linearity, leniency, density/openness, symmetry, path-redundancy, vertical range, item
  fairness.* Define them concretely and compute per generated map.
- **Controllable > fully-random.** Designers want knobs (size, verticality, archetype,
  density) with predictable effect, plus a seed for variety within a knob setting. Expose
  intent, randomize detail.
- **Constraint checks prevent degenerates.** Reachability, no-spawn-in-solid, no
  unreachable item, no pinball portal field, AAS-builds — validate every map before it
  counts (we already hit spawn-in-pillar and no-.aas failures).
- **Close the loop.** Define fitness from telemetry → generate a batch across seeds/knobs →
  evaluate → plot expressive range → adjust generator params → repeat. Optionally automate
  with a parameter search.

## Generation Approaches
- **Constructive (correct-by-construction).** Place geometry by rules so the result is
  always valid; fast, controllable, but variety is bounded by the rules. *This is
  strafegen.* The risk is exactly the user's complaint: low expressive range unless you
  build in structural variation (→ archetypes).
- **Generate-and-test.** Generate, then run constraint checks; reject or repair failures.
  Cheap insurance against degenerates (unreachable items, stuck spawns, no-AAS maps). We
  partially do this (selftest, spawn clearance) — formalize it as a gate.
- **Search-based PCG (SBPCG)** (Togelius et al.): represent a map as a genotype, evolve a
  population toward a fitness function (often via a genetic algorithm). Powerful for tuning
  but needs a fast, meaningful fitness — which our bot telemetry provides. Start with
  *parameter* search (tune knob ranges) before *content* search (evolve geometry).
- **Grammar / graph-based.** Generate a connectivity graph (rooms+edges) then realize
  geometry — strong for guaranteeing topology (loops, route counts). Good future direction
  for multi-room arenas beyond a single killbox.
- **WaveFunctionCollapse (WFC)** (Gumin): tile-based local-constraint solving; great for
  texture/tile variety, weaker for global structure (connectivity) without extra
  constraints.
- **Agent-based / BSP / cellular automata:** diggers, partitions, caves — more for organic
  or dungeon layouts than tight competitive arenas.

## Real Generators (prior art)
- **Oblige / ObAddon / Obsidian (Doom):** the gold standard for FPS map gen — themed,
  parameterized, produces playable multi-room levels with monster/item budgets. Lessons:
  heavy use of *prefab rooms + connection rules*, difficulty/density knobs, and lots of
  *validation*.
- **SLIGE / SLUMP (Doom):** older constructive generators; readable algorithms for
  room/corridor/item placement and "build a node graph, then realize it."
- **Roguelite arena generators:** rooms-as-nodes graphs with locked encounters; emphasize
  guaranteed reachability and pacing.
- **Modern AFPS (Diabotical/UGC):** lean on *human-authored prefabs* assembled
  procedurally — a hybrid that keeps competitive quality while varying layout.
- **Takeaway for us:** the strongest practical FPS generators are *hybrid* — authored
  building blocks (our archetypes, catwalk, portals) assembled and varied by rules, gated by
  validation, tuned by playtest.

## Evaluation Metrics (computable)
Define and compute these per generated arena (most are cheap geometry/graph stats):
- **Connectivity / reachability:** is every spawn/item/tier reachable from every other?
  (graph BFS over the navmesh/portal graph). *Hard gate.*
- **Route redundancy:** average number of distinct routes between key nodes (≥2 good).
- **Linearity:** how "on-rails" vs open the traffic is (variance of the desire line / number
  of meaningful choices). Arenas want *low* linearity.
- **Leniency:** how punishing — count of hazards/falls/kill-volumes weighted by exposure.
  Tune to a band (some stakes, not a meat-grinder).
- **Density / openness:** solid-volume fraction and average open-space radius; controls
  duel-vs-chaos feel.
- **Verticality range:** z-extent actually *used* by reachable play (not just box height).
- **Symmetry:** rotational/mirror score (fairness for competitive).
- **Item fairness:** distance balance from each spawn to each power item.
These map cleanly to the design rules in docs 01–02 and give the axes for expressive range.

## Expressive Range & Diversity (the anti-reskin tool)
**Expressive Range Analysis** (Smith & Whitehead 2010): to understand *what a generator can
make*, generate a large sample (e.g., 200–1000 seeds), score each on two chosen metrics
(classically **linearity × leniency**, but pick what matters — e.g., **verticality ×
openness**, or **density × symmetry**), and plot the 2-D histogram.
- A **tight cluster** = low expressive range = "reskins." A **broad, evenly-covered**
  region = diverse output.
- **Bias detection:** heatmap reveals if the generator over-produces one corner (e.g., all
  forests) and never reaches another.
- **Before/after:** run it pre- and post-change to *prove* a feature (like our centerpiece
  archetypes) widened the range. **This directly answers "are my maps actually different."**

**Diversity metrics** beyond the plot: average pairwise distance between maps in
metric-space; coverage of the metric grid; per-archetype centroid separation (do archetypes
occupy distinct regions, or overlap?).

**MAP-Elites** (Mouret & Clune 2015): instead of one optimum, maintain a grid whose cells
are "behavior descriptors" (e.g., verticality bin × density bin) and keep the
highest-*fitness* map in each cell. This *illuminates* the space and guarantees a diverse
high-quality set. **Our 6 archetypes are a manual MAP-Elites grid** — formalizing it (add
behavior axes, keep best seed per cell, scored by telemetry) is the natural next step.

## Search-Based / Closed-Loop Tuning
1. **Fitness from telemetry.** Combine our bot metrics into a scalar (or a Pareto set):
   reward frags, midair %, moveset usage, avg max speed; penalize stuck ms, pinball
   (≈98% air + 0 frags), telefrags, no-AAS. (See proposed function in `05-strafegen-tuning.md`.)
2. **Batch evaluate** across seeds (and knob settings) — we already script this.
3. **Expressive-range + diversity plots** to watch coverage, not just average fitness.
4. **Search the parameters** (knob ranges: size, archetype weights, column counts, pad/portal
   placement) with a simple loop — random search → hill-climb → (optionally) GA/MAP-Elites —
   selecting for *both* fitness and diversity.
5. **Validation gate** every candidate (reachability, spawn clearance, AAS, no pinball).
6. **Human-in-the-loop:** keep the screenshot+cinematic review (our overnight studio) as the
   qualitative check the metrics can't capture (vibe, readability).

## Pitfalls
- **Optimizing one scalar collapses diversity** — a single fitness drives every map toward
  one "best." Use Pareto/MAP-Elites or explicitly reward novelty.
- **Telemetry can be gamed by degenerates** (e.g., a tiny box maxes frags). Gate with
  validity + sanity bands, and keep human review.
- **Simulation noise:** bot runs vary; average ≥3 runs or lengthen the test; treat single
  bot_runs=0 as transient (retry).
- **Over-fitting to bots:** bots ignore race triggers and use portals poorly; weight
  human-relevant metrics and don't punish human-only fast lines.
- **Silent truncation:** if you cap seeds/metrics, log it — a partial sweep can look like
  "covered everything."

## Implications for STRAFE 64
- We have constructive gen + simulation eval; **add (a) a validity gate, (b) structural
  metrics, (c) an expressive-range script, (d) a telemetry fitness function.**
- **Formalize archetypes as MAP-Elites niches:** behavior axes = {verticality used, openness,
  symmetry}; keep the best seed per archetype×size cell, scored by telemetry → a curated
  "greatest hits" set instead of ad-hoc keepers.
- **Prove the archetype upgrade** by plotting expressive range over seeds before/after — the
  spread should visibly widen (spire/spiral/forest/ring/cross/twin occupying distinct
  regions).
- **Tune knob *ranges*, not single values**, then search them against fitness + diversity.

## TUNING KNOBS → strafegen (method, not geometry)
| Finding | Generator method | Suggested approach / target |
|---|---|---|
| Constructive → generate-and-test | Validity gate before "counts" | reachability + spawn-clearance + AAS-present + no-pinball; reject/repair failures |
| Simulation-based fitness | Telemetry → scalar/Pareto | reward frags, midair%, moveset use, avg speed; penalize stuck, pinball, telefrag, no-AAS |
| Prove diversity, not reskin | Expressive-range script | generate 200+ seeds, score on 2 metrics (e.g. verticality×openness), plot 2-D histogram; rerun before/after changes |
| Diversity metric | Pairwise + coverage | avg pairwise metric-distance; archetype-centroid separation; grid coverage % |
| Illuminate space | MAP-Elites grid | cells = archetype × size × verticality bin; keep best-telemetry seed per cell |
| Per-map structural metrics | Compute on the BSP/graph | connectivity, route redundancy, linearity, leniency, density, vertical-range-used, symmetry, item fairness |
| Controllable generation | Designer knobs + seed | expose size/verticality/archetype/density; seed varies detail within a setting |
| Robust evaluation | Repeat & average | ≥3 bot runs or longer test; retry bot_runs=0; keep human screenshot/cinematic review |
| Don't collapse to one map | Multi-objective / novelty | Pareto front or novelty bonus; never single-scalar hill-climb alone |
| Honest sweeps | Log caps/skips | record dropped seeds / capped metrics so a partial sweep isn't read as full coverage |

## Sources
- [Procedural Content Generation in Games — Shaker, Togelius, Nelson (free online book)](https://www.pcgbook.com/) — taxonomy (constructive / generate-and-test / search-based), simulation-based evaluation, metrics.
- [Analyzing the Expressive Range of a Level Generator — Gillian Smith & Jim Whitehead (PCG 2010)](https://dl.acm.org/doi/10.1145/1814256.1814260) — expressive range analysis; linearity & leniency; plotting generator output distributions.
- [Search-Based Procedural Content Generation: A Taxonomy and Survey — Togelius, Yannakakis, Stanley, Browne (IEEE TCIAIG 2011)](https://ieeexplore.ieee.org/document/5756645) — SBPCG, fitness functions, simulation-based fitness.
- [Illuminating Search Spaces by Mapping Elites — Mouret & Clune (2015)](https://arxiv.org/abs/1504.04909) — MAP-Elites; keep best per behavior cell to get a diverse high-quality set.
- [WaveFunctionCollapse — Maxim Gumin](https://github.com/mxgmn/WaveFunctionCollapse) — tile-based local-constraint generation; strengths/limits for global structure.
- [Oblige / ObAddon / Obsidian — Doom level generators](https://obsidian-level-maker.github.io/) — practical FPS map gen: prefabs + connection rules + density knobs + validation.
- [SLIGE / SLUMP — classic Doom map generators](https://doomwiki.org/wiki/SLIGE) — constructive room/corridor/item algorithms; build-graph-then-realize.
- STRAFE 64 internal: `tools/strafegen/strafegen.py` (`--selftest`, archetypes, `avoid_footprints`), `engine_playtest_report` telemetry, memories [[strafegen-killbox-archetypes]], [[strafegen-bot-dojo-methodology]], [[strafegen-spawn-in-pillar]].
