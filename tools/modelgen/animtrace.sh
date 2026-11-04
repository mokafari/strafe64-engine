#!/bin/sh
# animtrace.sh TAG -- scripted local-player run through the lab, then animjerk report
cd "$(dirname "$0")/../.." || exit 1
R=$PWD; T=$R/generated/iqmtest; TAG=${1:-trace}
python3 - > $T/baseoa/tr.cfg <<'PY'
print('g_hotFloor 0; g_timeBind 0; cg_draw2D 0; set cg_skelTrace 1; cg_thirdPerson 1; wait 60')
print('+forward; wait 180; +moveup; wait 3; -moveup; wait 90; +right; wait 40; -right; wait 40; +left; wait 40; -left; wait 40')
print('+moveup; wait 30; -moveup; wait 100; +movedown; wait 40; -movedown; wait 60; +attack; wait 90; -attack; wait 40; -forward; wait 80')
print('+forward; wait 60')			# the player's real loop: hop, double jump, land, hop
for i in range(5):
    print('+moveup; wait 3; -moveup; wait 28; +moveup; wait 3; -moveup; wait 55')
print('-forward; wait 40')
print('+back; wait 100; -back; wait 60; +moveleft; wait 60; -moveleft; +moveright; wait 60; -moveright; wait 40')
print('setviewpos -700 800 400 0; wait 90; +forward; wait 120; -forward; wait 60; quit')
PY
perl -e 'alarm 85; exec @ARGV' engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3 +set com_basegame baseoa +set fs_basepath $R/assets/openarena +set fs_homepath $T +set net_port 27971 +set sv_pure 0 +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 360 +set com_maxfps 125 +devmap strafe64_animlab +wait 200 +exec tr.cfg > $T/$TAG.log 2>&1
rm -f "$T/baseoa/STRAFE 64.pid"
python3 -I tools/modelgen/animjerk.py $T/$TAG.log $2
echo '--- spectated bot ---'
cat > $T/baseoa/tr.cfg <<'CFG'
g_hotFloor 0; g_timeBind 0; set g_botSwordOnly 1; cg_draw2D 0; addbot sarge 4; addbot major 4; addbot grunt 4; wait 200; team spectator; wait 40; follownext; wait 30; set cg_skelTrace 1; wait 1500; quit
CFG
perl -e 'alarm 50; exec @ARGV' engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3 +set com_basegame baseoa +set fs_basepath $R/assets/openarena +set fs_homepath $T +set net_port 27971 +set sv_pure 0 +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 +set bot_enable 1 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 360 +set com_maxfps 125 +devmap strafe64_animlab +wait 200 +exec tr.cfg > $T/$TAG.bot.log 2>&1
rm -f "$T/baseoa/STRAFE 64.pid"
python3 -I tools/modelgen/animjerk.py $T/$TAG.bot.log
