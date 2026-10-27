# Building STRAFE 64 maps and assets on any OS

Everything strafegen needs to rebuild the shipped maps is in the repo. The only
derived, untracked folder is `tools/strafegen/skytex/`, which one command
rebuilds from tracked sources.

## 1. Requirements

| Need | macOS | Linux | Windows |
|---|---|---|---|
| Python 3.10+ with Pillow and NumPy | `pip install pillow numpy` | same | same |
| Engine (CMake) | `./scripts/build.sh` | CMake + Ninja/Make, same CMakeLists | CMake + MSVC or MinGW |
| q3map2 (only for `--bake` lightmaps) | `tools/strafegen/build-q3map2.sh` | build [netradiant-custom](https://github.com/Garux/netradiant-custom) (CMake), set `Q3MAP2=/path/to/q3map2` | same, set `Q3MAP2=C:\path\q3map2.exe` |
| bspc (bot navigation `.aas`) | prebuilt arm64 binary in `tools/strafegen/bspc` | `git clone https://github.com/TTimo/bspc`, then `BSPC_SRC=/path/to/bspc CC=gcc tools/strafegen/build-bspc.sh` (or its own Makefile) and set `BSPC=/path/to/bspc` | build TTimo/bspc, set `BSPC=C:\path\bspc.exe` |

strafegen looks for each tool in this order:
- q3map2: `$Q3MAP2`, then `tools/strafegen/q3map2`, then `PATH`.
- bspc: `$BSPC`, then `tools/strafegen/bspc`, then `PATH`.

Without q3map2 you can still build every map unbaked (vertex-lit). Without
bspc the maps work, but bots can't navigate them.

## 2. Rebuild the photo assets

```bash
cd tools/strafegen
python3 build_assets.py
```

This turns `assets_src/` into `skytex/`:

| Source (tracked) | Built into `skytex/` | How |
|---|---|---|
| `assets_src/materials/*.png`: 14 Codex-generated textures | `m/<name>.jpg` (2048 albedo) and `m/<name>_nh.png` (normal + height) | `build_materials.py`: organic stone gets a seamless feather-blend tile, grids a 2x2 mirror tile, and the relief keeps structure only |
| `assets_src/skies/ethsky.png`, `ethnight.png`: Codex photo skies | `ethsky_*`, `ethnight_*` cube faces | `ethsky_from_pano.py`: 180° mirror wrap, photo cropped at the sun or moon, which is composited back round |
| `assets_src/concrete/crete_eth_source.png` | `crete_eth.tga` | Board-formed concrete, mirror tile, normalised tone |
| `assets_src/concrete/crete.png`, `realsky_*.png` | `crete.tga`, `realsky_*.tga` | Lossless copies of the concrete theme's photo assets |

Run it again after editing a source or a build parameter. Output is
deterministic for a given Pillow and NumPy version.

## 3. Build the maps

```bash
cd tools/strafegen
python3 strafegen.py --showcase --bake --pk3            # strafe64_showcase
python3 strafegen.py --showcase --night --bake --pk3    # strafe64_showcase_night
python3 strafegen.py --marble --bake --pk3              # strafe64_marble (+ --night)
python3 strafegen.py --granite --bake --pk3             # strafe64_granite (+ --night)
python3 regen_shipped.py ../../generated/ship           # every shipped course map
python3 strafegen.py --emit-shared ../../generated/ship # the shared shader/texture pak
```

Keep output paths short. bspc crashes on long absolute paths;
`compile_aas` works around this by running bspc with relative paths.

## 4. Deploy

Copy the map pk3s and `zzz_strafe64_shader.pk3` into `baseoa/` in **both**
the base path and every home path the engine reads. A stale shared pak in the
home path wins over a new one in the base path. The shared pak must be
current: baked maps use its `*_lm` and `*_bk` shader variants.
