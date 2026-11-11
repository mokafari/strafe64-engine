# Authored bot traversal prototype

Nocturne Flow supplies twelve directed movement edges: four gallery jumps, four
wall kicks and four cover air-jump chains. Each BSP `info_bot_route` entity stores a player
origin at `origin`, a `contact` point and an `exit` landing point. `spawnflags 1`
means wall kick; 2 means an air-jump chain; zero means jump. `speed` sets the
required approach speed (180 by default; 260 for the cover chains). The generator exports matching route IDs to
`tools/strafegen/generated/nocturne_flow/routes.svg` and `routes.json`.

## View and exercise the graph

Restart the game after updating the matching game/client DLLs and map package.
`exec nocturne_flow_routes` starts a normal-speed spectator session with five bots.
The map remains separate from classic Nocturne.

- `bot_traversal 1`: default; take nearby edges that advance the current AAS goal.
- `bot_traversal 0`: use the previous bot movement helpers only.
- `bot_traversal 2`: practice selection; allow nearby edges without goal-progress filtering.
- `bot_traversalDebug 1`: world-space arrows, node crosses and a counter panel.
- `bot_traversalDebug 2`: also log sampled controller state for diagnosis.
- `bot_traversalDebug 0`: remove graph entities from snapshots and hide the panel.
- `bot_traversalReport 1`: print route names, attempts, completions, failures and active bots.

Graph colors: cyan means untried, yellow means active, green means at least one
completion, red means failures without a completion. Counters retain mixed results;
green is not a reliability guarantee. World lines obey depth testing. The SVG gives
an unobstructed overhead view with route IDs and hover names over map footprints.
Counters reset on map load/restart and are currently session-local.

For repeatable execution tests, use `devmap strafe64_nocturne_flow`, add a bot,
and run `bot_route_test <bot client slot> <route ID>`. This cheat-only command
explicitly places the bot at the entry and clears movement state. It does not
validate natural route selection. Execution after setup uses ordinary inputs and
collision. `status` lists bot slots. Use one live game process at a time on this
machine; concurrent client smokes exhausted available Windows virtual memory.

## Controller and boundaries

Normal play never teleports bots or changes their velocity. Nearby candidates need
a clear entry approach and must reduce distance to the current enemy or navigation goal. Airborne bots can acquire an edge and
land at its entry, so existing bunny-hopping does not starve selection.
The controller uses authoritative player state every frame, builds approach speed,
issues a ground jump, releases jump, probes the wall and issues a fresh wall-kick
press, or an early air jump for cover chains, then steers/brakes toward the exit. Other movement helpers yield while an
edge is active. Completion requires an observed jump (and a wall kick for kick
edges), a grounded exit within 64 units, and landing height within 8 units.

Timeouts and cooldowns use world milliseconds: 4.5 seconds to execute, 12 seconds
before retrying a failure, 4 seconds after success. Death, disconnect, movement
mode changes and disabling traversal release control. Ordinary AAS navigation and
existing movement helpers resume after completion or failure.

This is an opportunistic layer over AAS, not new baked AAS reachability or a global
shortest-path solver. It does not yet plan chains across the whole map, learn from
player recordings, discover routes automatically, avoid occupied edges, or compare
exposure and danger. Wall-run sequences remain future
controller types. The twelve author-defined routes need combat playtesting and
success-rate measurement across varied approach velocities.

The debug entity type requires matching qagame/cgame builds; older mod DLLs and
recordings are not a compatibility target for this prototype.

## Regression coverage

The native movement harness executes the shared production route controller and
steering through real pmove at normal and 0.04x step sizes. Cases cover jump landing, air-jump cover clearance,
wall-kick confirmation, held-jump release, timeout, death and a missing wall. The
Python layout check verifies supported, clear entries/exits and actual wall faces
for kick contacts. Full suite: 195 native cases and 54 Python checks. Synthetic
collision tests complement, rather than replace, compiled-map execution checks.

## Live evidence (2026-11-09)

The final five-bot natural-selection sample recorded 10 attempts at 27 seconds:
9 completed, 1 active, 0 failed at that snapshot. These were the four cover
connections; this is a short sample, not a general reliability estimate or proof
that bots choose gallery/wall routes frequently. A later kick of the test bots
cancels any active route and records a failure; that is fixture teardown.

Evidence lives under `engine/build/polish-traversal-final-1794233262575923700/`:
`home/baseoa/natural.txt` separates natural selection from the subsequent
cheat-positioned execution checks. `traversal.txt` and the screenshots retain the
combined counters. Map build, engine build and full-suite logs are respectively
`traversal-map-build.log`, `traversal-build.log`, and `traversal-tests.log` under
`engine/build/`.

All twelve individually positioned route checks completed in the final run.
`route-execution-results.json` records each result separately. This validates the
three route types on all authored mirrored connections from their entry fixtures;
it does not establish completion from every natural approach. Additional failures
in the combined counters include natural acquisition attempts and fixture teardown.
