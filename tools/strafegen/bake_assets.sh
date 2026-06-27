#!/bin/sh
# bake_assets.sh — turn real photos into the house-style photoreal assets.
#
# The surrealist-brutalist / Bryce look ships with a PROCEDURAL fallback (a
# synthwave-ish sky + a generated concrete tile) so every map renders whole with
# zero external art. This script upgrades that to REAL photos: it bakes a sky
# cube + a concrete tile into tools/strafegen/skytex/, where strafegen picks them
# up automatically (strafegen_palettes._have_baked_sky + strafegen_textures.
# _load_baked) and OVERRIDES the procedural versions on the next generate.
#
# Drop two source photos into tools/strafegen/skytex_src/ first:
#   sky.png       — a wide daylight sky (sun upper area, mountains on horizon)
#   concrete.jpg  — a flat brutalist concrete surface (ideally seamless/tiling)
#
# Then:  ./bake_assets.sh           (regenerate maps afterwards to embed them)
#
# Needs Pillow:  pip install Pillow   (run on a workstation, not a PIL-free box)
#
# Good CC0 sources (public domain, safe to commit — no attribution required):
#   concrete : ambientCG  "Concrete###" PBR sets (grab the Color/albedo map)
#              https://ambientcg.com/list?type=Material&q=concrete
#   sky      : Poly Haven HDRIs / skies (export an equirect or wide JPG)
#              https://polyhaven.com/hdris/skies
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/skytex_src"
SKY="${SKY:-$SRC/sky.png}"
CRETE="${CRETE:-$SRC/concrete.jpg}"

if [ -f "$SKY" ]; then
	echo ">> baking sky cube from $SKY"
	python3 "$HERE/skybox_from_photo.py" --src "$SKY"
else
	echo "!! no sky photo at $SKY — skipping (keeps procedural Bryce sky)"
fi

if [ -f "$CRETE" ]; then
	echo ">> baking concrete tile from $CRETE"
	python3 "$HERE/crete_from_photo.py" --src "$CRETE"
else
	echo "!! no concrete photo at $CRETE — skipping (keeps procedural concrete)"
fi

echo ">> done. baked assets in $HERE/skytex/ — regenerate maps to embed them:"
echo "     python3 strafegen.py 1337 --pk3      (or ./scripts/run.sh ...)"
echo "   commit tools/strafegen/skytex/ to ship the photoreal look."
