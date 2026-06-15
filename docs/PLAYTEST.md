# STRAFE 64 — Bot Playtest System (Phase 1)

Continuously iterate on the game with **bots playing it**. Headless
`ioq3ded` instances run the maps with bots (who now use the full moveset),
the server records per-bot run metrics, and a harness aggregates them into a
report. Report-only — humans/agents decide the tuning changes.

## Run it

```sh
strafegen/playtest.py                                   # default sweep
strafegen/playtest.py --maps strafe64_1337 --skills 3,5 --runs 2 --dur 30
strafegen/playtest.py --bots 4 --parallel 6
```

Flags: `--maps a,b` · `--skills 2,4` · `--runs N` · `--dur SECONDS` ·
`--bots N` · `--parallel N` · `--keep` (keep the tmp batch dir).
The harness deploys the latest dylibs from the build, fans out the jobs, and
prints the report. Needs the engine built (`ioq3ded`).

## What bots can / can't test

Bots won't tell you if it's *fun*, but they nail the objective half:
**solvability** (can the course be completed / where do bots fall, stall,
get stuck), **difficulty calibration** (skill→completion gradient),
**pressure tuning** (does the void catch slow bots, spare fast ones),
**moveset exercise** (does a map ever trigger wallrun/double-jump), and
**regression** (re-run after a tuning change, compare). Humans then spend
their time on feel, not bug-finding.

## How it works

- **Telemetry** — `code/game/g_playtest.c`, gated by `g_playtest 1`. The
  server samples every alive bot each frame (`G_PlaytestSample` from
  `G_RunFrame`) — speed, airtime, `STAT_BHOP_STREAK`, `STAT_WALLRUN`,
  wall/double-jumps, distance-to-void, stuck time — and writes one JSON line
  per run on finish (`G_PlaytestFinish` ← race trigger), death
  (`G_PlaytestDeath` ← `player_die`, carries the means-of-death name), or
  level end (`G_PlaytestFlush` ← `G_ShutdownGame`). Output:
  `playtest/<g_playtestTag>.jsonl` under the run's homepath. No cgame needed
  — all metrics come from server-side player state.
- **Harness** — `strafegen/playtest.py`. One isolated `fs_homepath` + UDP
  port + tag per job; sends `quit` over stdin for a clean flush; reads the
  JSONL back and aggregates.

## The report

Per (map, skill): completion %, death-cause histogram (incl. the void's
share), flow uptime, airborne %, wallrun %, avg-max speed, best bhop chain,
moveset-usage %, best finish time (a **par-time candidate**), closest void
approach (tension), and a stuck warning. Metrics outside their **target
band** are flagged `<-- OUT OF BAND`:

| Metric | Band | Why |
|---|---|---|
| completion @ skill | 40–85% | not trivial, not impossible |
| flow uptime | ≥45% | course supports momentum |
| void's death share | 25–70% | the floor is a real but not sole killer |

(Bands live in `BANDS` at the top of `playtest.py`.)

## The Dojo (`strafegen/dojo.py`)

A reflective game-feel research loop layered on the harness. It adapts the
crypto-autoresearch *discipline* — **sanity gate**, provenance journal,
discrete outcome classes, no-regression acceptance, termination-as-deliverable
— to a *continuous* metric: distance to a **target dossier** of "the gameplay
we want".

Four scenarios isolate one archetype each — `strafegen --dojo
speed|flow|ztrick|arena|all` (movement dojos are fixed straight section
recipes; arena reuses the velodrome). Run the loop:

```sh
strafegen/dojo.py --dur 25 --label baseline      # battery + report + journal
strafegen/dojo.py --label air-accel-1.3          # tag a candidate config
```

- **Target dossier** (`dojo_dossier.json`) — bands per archetype metric
  (e.g. FLOW: flow ≥60%, wallrun ≥5%, stalls ≤500ms). Seeded on first run;
  edit against the baseline.
- **Sanity gate** — a scenario's numbers are trusted only if bots actually
  played it (not inert/stuck). A `BROKEN` verdict means *fix the scenario
  before believing its metrics* — a NULL from a broken dojo poisons the loop.
- **Classify** per archetype: `IN_DOSSIER / PARTIAL / OUT / BROKEN`.
- **Journal** (`dojo_runs.jsonl`) — one line per iteration with git hash +
  label, so every config and its metric vector is auditable.

Phase 1 is human-in-the-loop: it recommends, you change one knob, re-run, and
the no-regression rule rejects changes that rob one archetype to help another.

### Baseline findings (the loop working as intended)
- **speed / flow / ztrick → BROKEN** (gate-failed): bots are inert on the
  *linear* movement courses (avg speed ~2, stuck the whole run). Bot nav can't
  traverse them — the prerequisite to fix before movement auto-tuning means
  anything.
- **arena → plays** (avg 249 ups, 84% airborne) but **0 frags** — bots roam,
  don't fight. Combat engagement is the other prerequisite.

## Roadmap

- **Now:** unblock the dojo — fix bot navigation on linear courses and bot
  combat in the arena, so the movement/combat archetypes produce real metrics.
- **Phase 2:** failure heatmaps, auto par/medal calibration, richer proxies.
- **Phase 3 (opt-in):** close the loop — evolutionary tuner / daemon proposes-
  tests-selects param changes against the dossier fitness.

## Known (and these are *findings*, not bugs in the harness)

Bots don't navigate the linear race courses (`strafe64_*`) well — they stall.
The arena (`strafe64dm_*`) plays but under-exercises top speed and wall-run.
Both are exactly the kind of signal this system exists to surface.
