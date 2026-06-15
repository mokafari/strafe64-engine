# STRAFE 64 — Visual & Audio Identity

A NERV/MAGI-flavoured terminal HUD, a PlayStation-era low-fi render path,
and a native tracker-module music player. Reference mood: *Evangelion*
MAGI defense screens (amber-on-black, alert-red under pressure) with PSX
3D crunch.

## NERV/MAGI HUD

All in [cg_draw.c](code/cgame/cg_draw.c), mirrored into the ioquake3 build
tree. A single amber→red ops palette (`nerv_amber` / `nerv_orange` /
`nerv_red` / `nerv_green` / `nerv_dim`) ties the readouts together, and
`CG_NervPanel()` draws the dark translucent backing plate with L-shaped
corner brackets — the targeting-reticle frame of the Eva interface.

- **Speedometer** → `[ VEL 847  SYNC 1.42  HOP 5 ]`, amber at nominal,
  escalating to alert-red as the speed-damage multiplier climbs.
- **Race timer** → bracketed `- ELAPSED -` plate up top, amber digits,
  green `TARGET` (ghost best) underneath.
- **Void warning** → `VOID COLLAPSE  <dist>m` alert plate that flashes
  red like a NERV klaxon as the kill plane closes in.

The vertex-color section palettes and the near-black PSX sky live in
[strafegen.py](strafegen/strafegen.py) (`SHADER_SCRIPT`); the rising-void
plane is already NERV-red.

## PSX render path

Phase 1 is a cvar preset; Phase 2 adds a real renderer hook.

- **`strafegen/psx.cfg`** — point-sampled textures (`r_textureMode
  GL_NEAREST`), 16-bit colour banding, no AA, vertex lighting, and it
  selects the fixed-function GL1 renderer with `r_psx 1`.
- **`r_psx`** (GL1 renderer, ioquake3) — enables affine
  (non-perspective-correct) texture mapping, the signature PSX texture
  swim/warp. Gated so the renderer is otherwise untouched. Added via a
  `GL_PERSPECTIVE_CORRECTION_HINT` call in `RB_StageIteratorGeneric`
  (`qglHint` was added to the core QGL proc list).
- **Launch:** `./run-openarena.sh -p` (prefix any client mode) stages and
  execs the preset. e.g. `./run-openarena.sh -p -b 4 strafe64_1337`.

Deeper still (not yet done): screen-space vertex jitter and a low-res
framebuffer upscale for chunky pixels — those want GL2 GLSL / FBO work and
visual iteration.

## Tracker-module music (jungle player)

Native `.it / .xm / .s3m / .mod / .mptm` playback via **libopenmpt**, wired
into ioquake3's codec layer alongside ogg/opus:

- [code/client/snd_codec_mod.c](../ioquake3/code/client/snd_codec_mod.c) —
  reads the module, hands it to libopenmpt, streams interleaved stereo at
  44.1 kHz, loops forever at the song's own loop points. The engine
  resamples to the mixer rate, so any module rate works.
- Build: `USE_CODEC_MOD` (default ON), `cmake/libraries/openmpt.cmake`
  (pkg-config `libopenmpt`). **Install:** `brew install libopenmpt`.
  Homebrew ships it arm64-only, so the macOS build is configured
  arm64-only (`-DCMAKE_OSX_ARCHITECTURES=arm64`; `macos.cmake` now honours
  the override).

**Curated library (2026-06-15):** 21 freely-downloadable tracker modules from
[The Mod Archive](https://modarchive.org) are installed in `baseoa/music/`,
named by lane (see [ART_DIRECTION.md](ART_DIRECTION.md) §10):
`jungle_*` / `dnb_*` (atmospheric jungle + liquid/intelligent DnB, 10),
`breakcore_*` (peak/danger, 7), `ambient_*` (calm, 4). All verified to decode
through libopenmpt. strafegen **auto-assigns** a lane-appropriate track to each
generated map's worldspawn `music` key (`pick_music()` + `MUSIC_JUNGLE/
BREAKCORE/AMBIENT` in `strafegen.py`, picked deterministically from the seed):
- **courses** → jungle, **arenas** → breakcore,
- **void-disabled courses** (`--no-void`, i.e. PRACTICE) → ambient.

The deployed PRACTICE map `strafe64_7.pk3` is built `--no-void`, so practice
rides the calm ambient lane. These are community modules under the Mod
Archive's terms — fine for local/playtest use; check per-module licensing
before any public release.

**Play a jungle tune:** drop tracker files in `baseoa/music/` and
```
music music/your_jungle_tune.it
```
or set a map's worldspawn `music` key to one. `strafegen/make_test_mod.py`
generates a tiny valid test module (`baseoa/music/test_tracker.mod`).
