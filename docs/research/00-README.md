# STRAFE 64 — Arena & Movement-Combat Research

Research collection to drive tuning of our procedurally generated arenas
(`tools/strafegen/strafegen.py`) and the bot-playtest evaluation loop
(`engine_playtest_report`). Each doc ends with a **TUNING KNOBS → strafegen** table;
doc 05 consolidates everything into one actionable spec.

## Read order
1. **[05 — Generator Tuning Spec (synthesis)](05-strafegen-tuning.md)** — start here: the
   consolidated, actionable targets, validity gate, telemetry fitness, diversity plan, and
   prioritized change list. The other docs are the evidence behind it.
2. [01 — Arena FPS Map Design](01-arena-map-design.md) — topology/loops, scale & dimensions,
   verticality, sightlines, item economy (the match clock), pads/teleporters, spawns.
3. [02 — Movement Tech & Level Geometry](02-movement-tech-and-geometry.md) — what geometry
   affords strafe-jump/bhop, surf, wall-run, wall-jump, double-jump, slide, aerial chains;
   concrete dimensions tied to our physics constants.
4. [03 — Melee, Bullet-Time & Movement-Combat](03-melee-bullettime-combat.md) — designing
   spaces & encounters for fast sword + time-dilation + "speed = power"; dueling geometry,
   slice-gate pacing, anti-turtle.
5. [04 — Procedural Arena Generation & Tuning](04-procedural-arena-generation.md) — PCG
   methods, evaluation metrics, **expressive-range analysis** (the anti-reskin tool),
   MAP-Elites, simulation-based fitness, closed-loop tuning.

## How this gets used
- The geometry/item/spawn targets (docs 01–03, table A/B in doc 05) become **constraints and
  default ranges in strafegen** — column spacing, slab gaps, item layout, spawn clearance,
  slice-gate placement.
- The methods (doc 04, sections D–F in doc 05) become a **validity gate + metrics +
  expressive-range script + telemetry fitness**, so we *measure* whether maps are diverse and
  good rather than eyeballing seeds.
- Target telemetry bands (doc 05 §C) are the **keep / tweak / discard** rubric for the
  overnight arena studio.

## Provenance & caveats
- **03** was produced by a web-research subagent (sources are fetched URLs — spot-check links
  before quoting externally). The other docs (01, 02, 04, 05) were authored from the
  established design/PCG canon and our own repo constants; their "Sources" list canonical
  references (book/papers/wikis) by name + base URL rather than deep links.
- A parallel run intended four agent-written docs, but an API outage dropped three mid-task;
  those three (01/02/04) were authored directly to the same template. Content is grounded in
  real, well-known sources and our actual movement constants — but treat specific external
  numbers as "research leads to verify," not gospel, before hard-coding them.
- All numbers are reconciled to our physics (`tools/strafegen/strafegen.py` header,
  `docs/MOVEMENT.md`): run 320, jump 300, gravity 1000, apex ~46u, DJ ledge ~92u,
  wall-jump 200/250 (×2), slide-min 250, bhop ≤1.10×.

## Related internal docs
- `docs/MAP_DESIGN.md`, `docs/MOVEMENT.md`, `docs/PLAYTEST.md`
- `tools/strafegen/README.md`, `tools/strafegen/FINDINGS.md`
- Memories: strafegen-killbox-archetypes, killbox-momentum-portals, bounce-corner,
  combat-recipe, bot-dojo-methodology, playtest-deploy-path, source-dev-textures,
  sword-arena, spawn-in-pillar.
