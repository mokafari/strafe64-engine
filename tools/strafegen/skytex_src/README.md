# skytex_src — source photos for the photoreal house-style assets

Drop the real source photos here, then run the bake. This directory is **tracked
in git** (unlike `../skytex/`, the bake *output*, which historically was
ignored) so the source images travel with the repo.

## What goes here

| File | What | Notes |
|---|---|---|
| `sky.png` | wide daylight sky photo | sun in the upper area, mountains/horizon line low. Not a 360° panorama — `skybox_from_photo.py` projects it onto the full sphere and mirrors it around the back seamlessly. |
| `concrete.jpg` | flat brutalist concrete | ideally a **seamless/tiling** surface (CC0 PBR albedo maps already tile). Pitting, panel seams, weathering all welcome — that detail IS the look. |

## Bake (on a workstation with Pillow)

```sh
pip install Pillow          # one-time
cd tools/strafegen
./bake_assets.sh            # bakes sky cube + crete tile into ../skytex/
python3 strafegen.py 1337 --pk3   # regenerate a map so it embeds the photos
```

`bake_assets.sh` calls `skybox_from_photo.py` (sky → `skytex/realsky_*.tga`) and
`crete_from_photo.py` (concrete → `skytex/crete.tga`). strafegen auto-detects
those files and overrides the procedural fallback — no flags, no code changes.
Until they exist, every map still renders whole on the procedural Bryce sky +
generated concrete.

## Where to get CC0 (public-domain) photos — safe to commit

- **Concrete:** [ambientCG](https://ambientcg.com/list?type=Material&q=concrete)
  — CC0 PBR materials; grab the *Color/albedo* map (already seamless).
- **Sky:** [Poly Haven skies](https://polyhaven.com/hdris/skies) — CC0; export a
  wide daylight JPG/equirect with a clear sun and a horizon.

Both are CC0 (public domain, no attribution required), so the baked assets are
safe to commit and ship.

## Committing the result

To ship the photoreal look (not just the procedural fallback), commit the bake
output:

```sh
git add tools/strafegen/skytex/realsky_*.tga tools/strafegen/skytex/crete.tga
```

(The `.gitignore` entry for `skytex/` has been relaxed so these baked assets can
be tracked.)
