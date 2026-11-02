# Windows texture sweep — 2026-10-27

Scope: asset generation/deployment, map packaging, and a live Windows renderer
smoke test after integrating origin/master. This is not an exhaustive engine audit.

## Confirmed bugs fixed

- **Missing photo assets / stale shared pack:** `run.ps1` only emitted procedural
  assets, never ran `build_assets.py`, and suppressed generator failures. This
  checkout had no `skytex/` outputs and a July shared pack. The launcher now finds
  a Python installation with Pillow/NumPy, rebuilds missing or stale photo assets,
  checks generation exit codes, and deploys the resulting shared pack. Subsequent
  launches skip photo rebuilding when all outputs are current.
- **Checkerboard sky in OpenGL 1:** the fallback renderer rejected `q3gl2_sun`,
  invalidating the ethereal sky shader. Reproduced in a Windows screenshot and
  log. GL1 now consumes its common sun parameters and skips GL2-only tone mapping.
  The rebuilt renderer displays the sky and produces no corresponding warning.
- **Incorrect map-pack contents:** misplaced indentation made standalone textures
  conditional on `assets/slide.wav`, while lean packs bundled textures whenever
  that sound existed. Texture bundling now depends on `standalone` alone.
- **Ignored map argument:** `run.ps1 -Map ...` previously did nothing without
  `-Bots`. An explicitly supplied map now launches with zero bots too.

## Validation

- Windows MSVC x64 Release build succeeded after the renderer fix.
- `py -3.13 -m unittest discover -s tools/strafegen -p test_asset_packaging.py`
  passed: lean/standalone with and without slide audio, and missing/current/stale
  generated-photo detection.
- `powershell -ExecutionPolicy Bypass -File scripts/run.ps1 -StageOnly` succeeded
  twice; the second run reported current photo assets. Added `-StageOnly` allows
  deployment checks without starting the game.
- Shared pack contains 71 entries; all explicit `map`/`clampmap` references under
  `textures/strafe64/` resolve inside it. This check does not cover every shader
  directive or every map.
- Generated an unbaked `strafe64_showcase` map and loaded its 2,028 surfaces on
  Intel Iris Xe using both OpenGL 2 and OpenGL 1. Both exited normally. GL2 had no
  missing-image/shader errors; GL1's reproducible sky warning disappeared after
  the fix. Screenshots were visually inspected.

Local evidence (ignored build output):

- `windows-bugsweep-build.log`, `windows-assets.log`, `windows-assets-check.log`
- `engine/build/texture-smoke/gl2.log`, `gl1-before.log`, `gl1-before.jpg`
- `engine/build/texture-smoke/baseoa/qconsole.log`
- `engine/build/texture-smoke/baseoa/screenshots/texture-audit.jpg`
- `engine/build/texture-smoke/baseoa/screenshots/texture-audit-gl1-fixed.jpg`

The smoke tests used a separate home profile, preserving the user's settings.
The normal OpenArena asset folder now has the rebuilt shared pack and showcase
map. Source changes remain uncommitted.

## Limits and remaining observations

- GL2's default post-processing visibly softens this scene. GL1 provides a sharper
  reference, but differs in lighting/features; no aesthetic defaults were changed.
- Tests used an unbaked map, one GPU and short sessions. Baked lightmaps, other
  maps/GPUs, long play sessions, and sound were not validated.
- Existing compiler warnings in font/image code remain. The build has MP3 and
  tracker codecs disabled, while game playlists reference these formats; sound
  was disabled for the renderer tests, so music playback remains unverified.
- No duplicate shared pack was found in this user's default home `baseoa` folder.
  Other profiles with overriding packs can still display stale assets.
