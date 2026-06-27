#!/usr/bin/env bash
# bake_map.sh — q3map2 baked-lighting pipeline for a strafegen .map.
#
# strafegen writes vertex-lit BSP directly (no compile). This is the OPTIONAL
# alternative: take the Radiant .map source strafegen emits (--map) and run it
# through the locally-built q3map2 with Lunaran's lun3dm5 recipe (3-bounce
# radiosity) to get REAL lightmaps + a volumetric lightgrid. See
# docs/q3-map-study.md trick #7.
#
# HOW IT WORKS
#   * stages a throwaway q3map2 game dir from the shared zzz shader pk3
#   * injects compile-only q3map_sun / q3map_skylight into the sky shader
#     (the engine ignores q3map_* keywords, so the Bryce sky is unchanged)
#   * routes the world brushes (surf/wall) to LIGHTMAPPED shader variants
#     (surf_lm/wall_lm in zbaked.shader) — our identity shaders are
#     surfaceparm nolightmap, so q3map2 has nothing to bake without this
#   * runs -meta / -vis / -light and packages the baked bsp + zbaked.shader
#
# LIMITATION: baking is from the .map, which does NOT carry the _glow_tex neon
# accent routing (that happens at BSP-emit time in BspWriter). So baked maps
# lose the neon accents — this is a moody-lighting alternative, not a drop-in.
#
# USAGE: bake_map.sh <map.map> <zzz_strafe64_shader.pk3> <out.pk3> [sun_intensity] [skylight]
set -euo pipefail

MAP="${1:?map source}"; ZZZ="${2:?zzz shader pk3}"; OUT="${3:?output pk3}"
SUN="${4:-320}"; SKY="${5:-170}"
NAME="$(basename "${MAP%.map}")"
Q3="$(dirname "$0")/q3map2"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
GAME="$WORK/baseoa"; mkdir -p "$GAME/maps"

# 1. stage shaders + textures from the shared pk3
unzip -o -q "$ZZZ" -d "$GAME"

# 2. inject compile-only sun + sky fill into the sky shader
python3 - "$GAME/scripts/strafe64.shader" "$SUN" "$SKY" <<'PY'
import sys
p, sun, sky = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(p).read()
inject = (f"\tsurfaceparm sky\n"
          f"\tq3map_sun 1 0.92 0.78 {sun} 0 38\n"
          f"\tq3map_skylight {sky} 3\n"
          f"\tq3map_lightmapSampleSize 8\n")
assert "\tsurfaceparm sky\n" in s, "sky shader not found"
open(p, "w").write(s.replace("\tsurfaceparm sky\n", inject, 1))
PY

# 3. lightmapped variants of the vertex-lit identity surfaces
cat > "$GAME/scripts/zbaked.shader" <<'SHADER'
textures/strafe64/surf_lm
{
	{ map $lightmap
	  rgbGen identity }
	{ map textures/strafe64/d_floor.tga
	  blendFunc GL_DST_COLOR GL_ZERO
	  rgbGen identity }
}
textures/strafe64/wall_lm
{
	{ map $lightmap
	  rgbGen identity }
	{ map textures/strafe64/d_wall.tga
	  blendFunc GL_DST_COLOR GL_ZERO
	  rgbGen identity }
}
SHADER

# 4. route world brushes to the lightmapped variants
sed -e 's#strafe64/surf #strafe64/surf_lm #g' \
    -e 's#strafe64/wall #strafe64/wall_lm #g' \
    "$MAP" > "$GAME/maps/$NAME.map"

# 5. compile — Lunaran's lun3dm5 recipe: -bounce 3 -bouncescale 2 -super 2,
#    plus -dirty (free ambient-occlusion) and a global -scale lift.
#    NB: no -vis pass. strafegen courses are OPEN (floating platforms in the
#    void, not a sealed hull), so vis would leak — the lump ends up empty ("all
#    visible") regardless, and -vis can Bus-error portalizing unsealed geometry.
M="$GAME/maps/$NAME.map"
"$Q3" -fs_basepath "$WORK" -fs_game baseoa -meta "$M"
"$Q3" -fs_basepath "$WORK" -fs_game baseoa -light \
      -bounce 3 -bouncescale 2 -super 2 -samplesize 8 -scale 1.7 -dirty -fast "$M"

# 6. package baked bsp + the lightmapped shader (textures come from the shared pk3)
PKG="$WORK/pkg"; mkdir -p "$PKG/maps" "$PKG/scripts" "$PKG/textures/strafe64"
cp "$GAME/maps/$NAME.bsp" "$PKG/maps/"
cp "$GAME/scripts/zbaked.shader" "$PKG/scripts/"
cp "$GAME/textures/strafe64/d_floor.tga" "$GAME/textures/strafe64/d_wall.tga" \
   "$PKG/textures/strafe64/"
( cd "$PKG" && zip -qr "$OUT" . )
echo "baked -> $OUT"
python3 - "$PKG/maps/$NAME.bsp" <<'PY'
import sys, struct
d = open(sys.argv[1], "rb").read()
L = lambda i: struct.unpack_from("<2i", d, 8 + i*8)
print(f"  surfaces={L(13)[1]//104} lightmaps={L(14)[1]//(128*128*3)} "
      f"lightgrid={L(15)[1]} bytes={len(d)}")
PY
