#!/bin/sh
# ridetrace.sh TAG -- rides a wall to the timer, drops off, repeats: leaving a wall
# is where real play spikes and the script in animtrace.sh never gets there
cd "$(dirname "$0")/../.." || exit 1
R=$PWD; T=$R/generated/iqmtest; TAG=${1:-ride}
python3 - > $T/baseoa/tr.cfg <<'PY'
print('g_hotFloor 0; g_timeBind 0; cg_draw2D 0; set cg_skelTrace 1; cg_thirdPerson 1; wait 60')
for x, y, yaw in ((-880, 464, 0), (880, 464, 180), (-880, 464, 0), (880, 336, 180)):
    print(f'setviewpos {x} {y} 170 {yaw}; wait 3; setviewpos {x} {y} 170 {yaw}; +forward; wait 330; -forward; wait 60')
print('quit')
PY
perl -e 'alarm 40; exec @ARGV' engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3 +set com_basegame baseoa +set fs_basepath $R/assets/openarena +set fs_homepath $T +set net_port 27971 +set sv_pure 0 +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 360 +set com_maxfps 125 +devmap strafe64_animlab +wait 200 +exec tr.cfg > $T/$TAG.log 2>&1
rm -f "$T/baseoa/STRAFE 64.pid"
python3 -I tools/modelgen/animjerk.py $T/$TAG.log
