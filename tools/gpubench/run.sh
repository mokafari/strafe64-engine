#!/bin/bash
# run.sh <label> [+set cvar value ...]  -> $OUT/results/<label>.txt + one median "gpu:" row
#
# Fresh boot per config into an isolated fs_homepath, exec bench.cfg (the
# showcase look), apply the overrides, vid_restart, devmap the bench map with
# god+noclip at spawn (the floor kills), then read r_gpuTimers for 14 s.
# Overrides go over the pipe, NOT the command line: ioq3 keeps only 32 "+"
# commands and silently drops the rest.
#
#   BIN=<ioquake3 binary> (default: this tree's engine/build)
#   OUT=<scratch dir>     (default: /tmp/s64-gpubench)
#   ASSETS=<openarena dir> (default: this tree's assets/openarena; set it from a worktree)
S=$(cd "$(dirname "$0")" && pwd); L=$1; shift
ROOT=$(cd "$S/../.." && pwd)
BIN=${BIN:-$ROOT/engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3}
OUT=${OUT:-/tmp/s64-gpubench}; H=$OUT/home; mkdir -p $H/baseoa
DYL=$(dirname "$BIN")/../../../baseq3
for d in cgame qagame ui; do cmp -s $DYL/$d.dylib $H/baseoa/$d.dylib || { cp $DYL/$d.dylib $H/baseoa/ && codesign -f -s - $H/baseoa/$d.dylib 2>/dev/null; }; done
cp $S/bench.cfg $H/baseoa/
 P=$H/baseoa/bench.pipe; mkdir -p $OUT/results; rm -f $P $H/baseoa/$L.txt
"$BIN" +set com_basegame baseoa +set fs_homepath $H +set fs_basepath ${ASSETS:-$ROOT/assets/openarena} \
 +set com_pipefile bench.pipe +set sv_pure 0 +set vm_cgame 0 +set vm_game 0 +set vm_ui 0 \
 +set r_mode -1 +set r_customwidth 2560 +set r_customheight 1440 +set r_fullscreen 0 +set s_initsound 0 +set r_gpuTimers 10 > $OUT/results/$L.boot.log 2>&1 &
PID=$!
for i in $(seq 60); do [ -p $P ] || [ -e $P ] && break; sleep 0.5; done
sleep 6
EXTRA=""; while [ $# -gt 0 ]; do [ "$1" = "+set" ] && { EXTRA="$EXTRA
set $2 $3"; shift 3; continue; }; shift; done
printf 'exec bench.cfg\n%s\nvid_restart\n' "$EXTRA" > $P; sleep 6
printf 'devmap strafe64_1337\n' > $P
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do sleep 0.5; printf 'god\n' > $P; done; sleep 1; printf 'noclip\n' > $P; sleep 3
printf 'clear\ncom_speeds 0\n' > $P; sleep 14
printf 'com_speeds 0\nserverinfo\ncondump %s.txt\nscreenshotJPEG %s\n' $L $L > $P; sleep 2
printf 'quit\n' > $P; sleep 2; kill $PID 2>/dev/null; wait $PID 2>/dev/null
mv $H/baseoa/$L.txt $OUT/results/ 2>/dev/null
python3 $S/parse.py $OUT/results/$L.txt
