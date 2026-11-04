# Focused Windows regression checks

Run the repeatable suite from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1
# Also build/stage the engine, then exercise menu, Nocturne GL2 and GL1 fallback:
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test.ps1 -Smoke
```

The fast suite runs 11 native CTest cases and 49 Python asset/API/smoke checks.
Native results are saved to `engine/build/regressions/results.xml` (JUnit).
Requirements: Visual Studio 2022 C++ tools and Python 3.13 with Pillow/NumPy.
Each timebind case runs in a separate process, with a deterministic real-time
clock and the actual production functions extracted at configure time:

| Case | Behavior protected |
| --- | --- |
| ownership | Pilot release, unrelated spectators, disable/reentry, focus ownership |
| bot-exclusion | Bots and invalid client IDs cannot drive or release the clock |
| movement-intent | Residual velocity stays slow; jumping wakes time; release settles |
| guard-brake | Guard slows a moving pilot; releasing it restores speed |
| focus-recharge | Focus expires, stays exhausted while held, recharges on release |
| clock-recovery | Living pilot recovers an overridden clock; settled state stops writes |
| death-respawn | Killcam owns time during death; respawn resumes and cleanup restores |
| fixed-movement | A preexisting fixed-movement setting survives timebind release |

The other native cases cover menu/map availability, bot rosters, and audio codec
fallback/stream handling. `video.resolution_selection` exercises the production
video-mode enumeration and mapping: missing SDL modes, modern and ultrawide sizes,
legacy indices, invalid stored modes, duplicates, list capacity, and reopening
the menu after resolution changes. Smoke checks require playable local assets and a GPU;
they check map readiness, process survival, engine/shader errors, screenshot
creation, and clean exit. They use isolated profiles and local-only console
control. They do not establish water/lighting visual quality, combat balance,
network multiplayer correctness, or physical input responsiveness.

Nocturne requires the external `nocturne_reservoir.pk3` material pack installed
in `assets/openarena/baseoa/`. It is not included in Git (upstream kept it on a
Mac external drive). The smoke suite deliberately fails on missing shader
images rather than accepting a checkerboard-rendered map as a pass.

Integration verification on Windows, 2026-11-04 (`ethereal-grade` tip `1250438`):

- Release engine build and asset staging passed (existing compiler warnings remain).
- All 10 native cases and 49 Python checks passed.
- Main menu and Marble GL2 runtime/capture/clean-exit checks passed.
- Nocturne runs, but strict smoke fails on missing reservoir material images.
  Restore the external pack before treating its visuals as validated.
- Marble GL1 strict smoke fails: the renderer rejects `stage normalMap` in
  `models/s64/robot`, then reports a shader stage without an image. This is a
  separate upstream fallback-renderer compatibility issue; GL2 loads that shader.

Local logs and screenshots are under `engine/build/polish-regression-*`;
`engine/build/ethereal-tests.log` records the fast suite and `windows-build.log`
records compilation. These runtime defects are not waived by the passing unit
tests. Combat damage, movement collision, and multiplayer remain future coverage.

Build the engine with `scripts/build.ps1` first. These small native harnesses
exercise production code with mocked engine services; they do not replace a
live renderer or gameplay test. Checks remain enabled in Release builds.

With Visual Studio's CMake on PATH, run from the repository root:

```powershell
foreach ($test in 'arena_menu', 'sound_codec', 'timebind') {
    cmake -S "tests/$test" -B "engine/build/test-$test" -A x64
    if ($LASTEXITCODE) { throw "Configure failed: $test" }
    cmake --build "engine/build/test-$test" --config Release
    if ($LASTEXITCODE) { throw "Build failed: $test" }
}
engine/build/test-arena_menu/Release/arena_menu_regression.exe
engine/build/test-sound_codec/Release/codec_regression.exe
engine/build/test-timebind/Release/timebind_regression.exe
```

- `arena_menu`: installed BSP/AAS filtering, empty-install protection, preset
  override ordering, saved opponent count, seven distinct opponents and slots.
  An optional output filename writes the actual menu-generated launch sequence
  for `windows_smoke.py --exec-file`.
- `sound_codec`: supported formats, load/stream fallback, missing-file handling.
- `timebind`: clock ownership, release, reentry, and disabling. CMake extracts the
  two functions directly from current `g_active.c`, and reconfigures when it
  changes; the test does not maintain a second clock implementation.
- [`renderer_font`](renderer_font/README.md): real FreeType, cache reuse/capacity,
  saved atlas naming/reload, font selection, cleanup and oversized-font rejection.

Python checks require the same Python/Pillow/NumPy environment as asset builds:

```powershell
py -3.13 -m unittest discover -s tools/strafegen -p test_asset_packaging.py
py -3.13 -m unittest discover -s tools/strafegen -p test_spawn_decoration.py
py -3.13 tools/strafegen/test_engine_api.py
```

`test_engine_api.py` is an existing standalone runner, not a unittest module;
its top-level exit makes broad `unittest discover -p test_*.py` unsuitable.

Live checks use copied binaries and a separate home profile. They copy only the
installed license setting and never write the player's normal configuration:

```powershell
py -3.13 tools/strafegen/windows_smoke.py --tag menu --menu
py -3.13 tools/strafegen/windows_smoke.py --tag materials --map strafe64_marble --duration 20
py -3.13 tools/strafegen/windows_smoke.py --tag fallback --renderer opengl1 --duration 20
```

`--at SECONDS:COMMAND` schedules console actions; `--set NAME=VALUE` overrides
test settings. Output, screenshots, and console logs stay under ignored
`engine/build/polish-*`. A normal exit and absence of engine errors are required.
Profiles contain a local license setting and should not be published.
