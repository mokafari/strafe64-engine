#!/bin/sh
# Bakes the player mannequin from the tracked CC0 sources into the game's asset
# tree and regenerates the cgame clip table. Run after changing the clip list.
cd "$(dirname "$0")/../.." || exit 1
S=tools/modelgen/assets_src
mkdir -p assets/openarena/baseoa/models/s64
python3 -I tools/modelgen/glb2iqm.py assets/openarena/baseoa/models/s64/mannequin.iqm --mesh $S/UAL1_Standard.glb \
  --shader textures/strafe64/d_wall --header engine/code/cgame/cg_skelclips.h --anims \
  "$S/UAL1_Standard.glb:Idle_Loop,Walk_Loop,Sprint_Loop,Crouch_Idle_Loop,Crouch_Fwd_Loop,Jump_Loop,Jump_Start,Jump_Land,Roll" \
  "$S/UAL2_Standard.glb:Slide_Loop,Slide_Start,Slide_Exit,NinjaJump_Idle_Loop,NinjaJump_Start,NinjaJump_Land,Sword_Dash,Sword_Regular_A,Sword_Regular_B,Sword_Regular_C,Sword_Block,ClimbUp_1m"
