# Focused Windows regression checks

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
