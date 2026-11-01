#!/bin/sh
# Bakes the player mannequin from the tracked CC0 sources into the game's asset
# tree and regenerates the cgame clip table. Run after changing the clip list.
cd "$(dirname "$0")/../.." || exit 1
S=tools/modelgen/assets_src
G=assets/_src/mixamo_glb		# Mixamo clips, kept out of git: see docs/BUILD_ASSETS.md
mkdir -p assets/openarena/baseoa/models/s64
python3 -I tools/modelgen/glb2iqm.py assets/openarena/baseoa/models/s64/mannequin.iqm --mesh $S/UAL1_Standard.glb \
  --shader textures/strafe64/d_wall --header engine/code/cgame/cg_skelclips.h --anims \
  "$S/UAL1_Standard.glb:Idle_Loop,Walk_Loop,Sprint_Loop,Crouch_Idle_Loop,Crouch_Fwd_Loop,Jump_Loop,Jump_Start,Jump_Land,Roll" \
  "$S/UAL2_Standard.glb:Slide_Loop,Slide_Start,Slide_Exit,NinjaJump_Idle_Loop,NinjaJump_Start,NinjaJump_Land,Sword_Dash,Sword_Regular_A,Sword_Regular_B,Sword_Regular_C,Sword_Block,ClimbUp_1m" \
  --tpose "$S/UAL1_Standard.glb:A_TPose" --bind "$G/xbot.glb" --mixamo \
  "$G/wallrun.glb:Wall_Run_L" "$G/wallrun.glb:Wall_Run_R" "$G/flip.glb:Run_Flip" "$G/fallroll.glb:Fall_Roll" "$G/hardland.glb:Hard_Land"

# the robot wears the same skeleton and clips; source kept out of git like the Mixamo clips
if [ -f assets/_src/robot/robot.glb ]; then
python3 tools/modelgen/glb2iqm.py assets/openarena/baseoa/models/s64/robot.iqm --mesh $S/UAL1_Standard.glb \
  --refit assets/_src/robot/robot.glb --shader models/s64/robot --anims \
  "$S/UAL1_Standard.glb:Idle_Loop,Walk_Loop,Sprint_Loop,Crouch_Idle_Loop,Crouch_Fwd_Loop,Jump_Loop,Jump_Start,Jump_Land,Roll" \
  "$S/UAL2_Standard.glb:Slide_Loop,Slide_Start,Slide_Exit,NinjaJump_Idle_Loop,NinjaJump_Start,NinjaJump_Land,Sword_Dash,Sword_Regular_A,Sword_Regular_B,Sword_Regular_C,Sword_Block,ClimbUp_1m" \
  --tpose "$S/UAL1_Standard.glb:A_TPose" --bind "$G/xbot.glb" --mixamo \
  "$G/wallrun.glb:Wall_Run_L" "$G/wallrun.glb:Wall_Run_R" "$G/flip.glb:Run_Flip" "$G/fallroll.glb:Fall_Roll" "$G/hardland.glb:Hard_Land"
python3 -I tools/modelgen/robot_textures.py assets/_src/robot/robot.glb assets/openarena/baseoa/models/s64
fi
