# Sword (Cleaver) — CC0 asset sources

All assets here are **CC0 1.0 (public domain)** — free for commercial use, no
attribution required. Credits below are courtesy, not obligation.

## Model
- `sword.md3`, `sword.jpg` — converted from **"Katana"** by *Colorado Stark*
  - https://opengameart.org/content/katana-2 (CC0)
  - Source FBX/OBJ + 1024² diffuse/normal/spec TGA.
  - Pipeline: `tools/strafegen/obj2md3.py` (OBJ→MD3, single frame/surface),
    `--uniform-len 28 --center`; diffuse downscaled 1024→128 (N64 scale) via `sips`.

## Sounds (Q3-native: mono, 22050 Hz, 16-bit PCM WAV)
- `swing1/2/3.wav`, `unsheathe.wav` — from the **RPG Sound Pack**
  - https://opengameart.org/content/rpg-sound-pack (CC0)
  - Original `swing.wav`/`swing2.wav`/`swing3.wav` + `sword-unsheathe.wav`,
    converted from 44.1 kHz stereo via `afconvert -d LEI16@22050 -c 1`.

- `hit1.wav`, `hit2.wav`, `heavy.wav` — from **StarNinjas "20 Sword SFX"**
  - https://opengameart.org/content/20-sword-sound-effects-attacks-and-clashes (CC0)
  - `hit*` from sword_clash (blade-bite impact on connect); `heavy` from a
    sword attack (finisher swing). Converted from .ogg via
    `ffmpeg -ac 1 -ar 22050 -sample_fmt s16`.

## Other CC0 options pulled but not wired in (kept for later)
- StarNinjas remaining attack/clash variants (.ogg) — more impact variety:
  https://opengameart.org/content/20-sword-sound-effects-attacks-and-clashes (CC0)
- Low poly Katana by tbbk (alt model, includes .blend):
  https://opengameart.org/content/low-poly-katana-0 (CC0)

## Deploy
Runtime copies live under `assets/openarena/baseoa/` (gitignored):
- `models/weapons2/sword/sword.md3`, `sword.jpg`
- `sound/weapons/sword/swing{1,2,3}.wav`, `unsheathe.wav`

`scripts/run.sh` does not auto-deploy these; re-copy from this dir after changes
(or fold them into a shipping pk3).
