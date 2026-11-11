# STRAFE 64 development rules

These rules apply throughout this repository. Read [the studio workflow](docs/STUDIO_WORKFLOW.md)
when planning a feature, reviewing a cross-system change, or preparing a playtest.

## Project and direction

- This is the native C/ioquake3 STRAFE 64 engine and mod. Work in this tree;
  the separate Godot project is not its source of truth.
- Preserve the game's movement, katana combat, parries, projectile deflection,
  timebind, and LATTICE identity. Scope changes to the requested mode; race and
  arena rules need not share every setting.
- Follow `docs/ART_DIRECTION.md`, `docs/MOVEMENT.md`, and `docs/MAP_DESIGN.md`.
  Keep traversal, threats, opponents, and HUD readable at speed. Preserve the
  distinct world, mech/menu, live HUD, and results-screen visual registers.
- Treat `docs/STATE.md` and historical test reports as orientation. Verify
  current behavior, cvar defaults, dependencies, and coverage against source,
  active presets, and fresh observations before making claims.

## Working agreement

- Begin with Git status and relevant source/design sections. Preserve existing
  staged, unstaged, and untracked work; never reset unrelated changes.
- State the intended player-visible result and practical acceptance checks.
  For a small fix, a short explanation is enough; honor an explicit plan-first request.
- Carry authorized work through implementation and appropriate verification.
  Do not request permission for every edit. Ask when a missing decision changes
  scope materially, or when a destructive/external action needs authorization.
- Use one agent by default. Delegate only when the user or an applicable workflow
  explicitly requests it; assign disjoint write ownership and integrate centrally.
- Keep scope focused. Reuse the engine and tooling already present. Record
  meaningful design/architecture decisions in the relevant existing doc.
- Commit, push, publish, and contact external parties only when authorized.
  Report whether delivery is uncommitted, committed locally, or pushed.

## Source ownership and engineering

| Concern | Starting point |
| --- | --- |
| Shared movement and networked definitions | `engine/code/game/bg_*` |
| Authoritative rules, combat, clocks, bots | `engine/code/game/` |
| Prediction, presentation, HUD, effects | `engine/code/cgame/` |
| Menus | `engine/code/q3_ui/` |
| Engine timing, client/server transport | `engine/code/client/`, `engine/code/server/`, `engine/code/qcommon/` |
| Rendering and shaders | `engine/code/renderergl1/`, `engine/code/renderergl2/` |
| Maps, assets, presets, runtime tooling | `tools/strafegen/` |
| Build/staging and regression harnesses | `scripts/`, `tests/` |

- Match surrounding C/Python/PowerShell style and existing ownership patterns.
  Explain non-obvious timing, units, ownership, and protocol assumptions.
- Keep gameplay authority in the server/game layer and prediction consistent
  with shared movement. Do not hide a gameplay defect with a rendering workaround.
- For timebind, distinguish real time, simulation time, and presentation time.
  Check focus, death/respawn, spectators, bots, disable/reentry, and clock cleanup
  when affected. Preserve intended clock ownership during merges.
- Shared state/header changes require rebuilding and staging qagame, cgame,
  and ui together. Inspect all producers/consumers of changed layouts or flags.
- Use existing cvars and cfg presets for tuning where appropriate; document
  units, bounds, defaults, and overrides. Do not externalize protocol constants
  or add a new configuration framework just to make every number configurable.
- Keep per-frame work bounded. Measure performance under a reproducible map,
  bot count, renderer, resolution, and preset before claiming improvement.
- Edit asset/generator sources, then regenerate and stage derived outputs.
  Verify pack contents, material dependencies, and BSP/AAS availability.
  Preserve third-party attribution; do not assume local asset packs are in Git.

## Windows verification

Run from the repository root, selecting the steps relevant to the change:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run.ps1 -StageOnly
# Builds/stages and exercises live smoke checks as well:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Smoke
```

- Use `tests/README.md` for focused tests and smoke prerequisites. Do not use
  broad Python unittest discovery: `test_engine_api.py` is a standalone runner.
- Run live game tests serially. Use isolated smoke profiles; avoid changing the
  player's configuration. Before staging, ensure the relevant game process has
  released DLLs. Identify owned processes before stopping them or clearing markers.
- Gameplay fixes need focused behavioral regression coverage where practical;
  prefer production logic over duplicated test implementations. Use fixed seeds
  and controlled clocks. Never suppress a failure merely to make checks green.
- Player-visible changes need a launch and observation of the affected scenario.
  Retain logs and captures under ignored `engine/build/` paths. For visual changes,
  inspect the captures, including affected HUD/menu sizes and renderer variants.
- Separate compilation, fixture tests, live smoke, visual review, natural bot
  play, and human playtesting. A controlled route pass does not prove natural bot
  selection; a screenshot does not prove feel or multiplayer correctness.
- If assets, tools, hardware, or permissions block a check, report exactly what
  passed and what remains unverified. Documentation-only edits need link/path
  and diff checks, not an engine rebuild.
- Finish with the outcome, checks actually run, remaining defects/limitations,
  evidence paths when useful, and local versus remote delivery state.
