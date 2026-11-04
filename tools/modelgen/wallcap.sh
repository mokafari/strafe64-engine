#!/bin/sh
# wallcap.sh -- films a REAL wall run (not the rig) in strafe64_animlab, close up
# from the open side, to generated/animsheets/wallcap.jpg
cd "$(dirname "$0")/../.." || exit 1
R=$PWD; T=$R/generated/iqmtest; S=$T/baseoa/screenshots
rm -f $S/wr_*.jpg
python3 - > $T/baseoa/tr.cfg <<'PY'
print("g_hotFloor 0; g_timeBind 0; cg_draw2D 0; cg_drawGun 0; set cg_skelTrace 1; cg_thirdPerson 1; cg_thirdPersonRange 70; cg_thirdPersonAngle 300; wait 40")
print("setviewpos -1050 464 40 0; wait 5; setviewpos -1050 464 40 0; wait 10; +forward; wait 60; +moveup; wait 2; -moveup; wait 16")
for i in range(12): print(f"screenshotJPEG wr_{i:02d}; wait 5")
print("-forward; wait 20; quit")
PY
perl -e 'alarm 40; exec @ARGV' engine/build/Release/ioquake3.app/Contents/MacOS/ioquake3 +set com_basegame baseoa +set fs_basepath $R/assets/openarena +set fs_homepath $T +set net_port 27971 +set sv_pure 0 +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 800 +set r_customheight 450 +set com_maxfps 125 +devmap strafe64_animlab +wait 200 +exec tr.cfg > $T/wr.log 2>&1
rm -f "$T/baseoa/STRAFE 64.pid"
python3 -I -c "
from PIL import Image;import glob
f=sorted(glob.glob('$S/wr_*.jpg'))
c=Image.new('RGB',(1600,675))
for i,n in enumerate(f[:12]): c.paste(Image.open(n).resize((400,225)),((i%4)*400,(i//4)*225))
c.save('generated/animsheets/wallcap.jpg')"
