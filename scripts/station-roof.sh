#!/bin/sh
# Local station encounter. --bots runs the same level headless with two rivals.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
build="$root/engine/build/Release"
oa="${OA:-$root/assets/openarena}"
profile="${STATION_PROFILE:-$HOME/Library/Application Support/STRAFE64-Station}"
base="$profile/baseoa"
mkdir -p "$base/maps"
map=station_roof
generator=--station
if [ "${1:-}" = --building ]; then
  map=station_blocks
  generator=--station-blocks
  shift
fi
python3 "$root/tools/strafegen/strafegen.py" "$generator" --out "$base/maps"
test -s "$base/maps/$map.aas"
for mod in qagame cgame ui; do
  cp "$build/baseq3/$mod.dylib" "$base/$mod.dylib"
  codesign -f -s - "$base/$mod.dylib" 2>/dev/null
done
mode=${1:-play}
if [ "$mode" = --bots ]; then
  exe="$build/ioq3ded"
  set -- +set net_port 27976 +set g_playtest 1 +set g_playtestTag station +map "$map" +addbot Assassin 4 +addbot Assassin 4
else
  exe="$build/ioquake3.app/Contents/MacOS/ioquake3"
  sed "s/^map station_roof$/map $map/" "$root/tools/strafegen/station_roof.cfg" > "$base/station_roof.cfg"
  set -- +set com_pipefile station.pipe +set r_fullscreen 0 +exec station_roof.cfg
fi
exec "$exe" +set fs_homepath "$profile" +set fs_basepath "$oa" +set com_basegame baseoa \
  +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 +set sv_pure 0 +set bot_enable 1 \
  +set g_timeBind 2 +set g_botSwordOnly 1 +set bot_swordBlock 1 +set g_vectorgun 0 \
  +set cg_killcam 0 +set g_hotFloor 0 +set g_voidRise 0 +set fraglimit 0 +set timelimit 0 "$@"
