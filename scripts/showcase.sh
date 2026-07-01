#!/bin/sh
# STRAFE 64 — SHOWCASE launcher.
#
# Boots the engine looking its absolute best and drops you into live
# gameplay: GL2 post-processing (HDR + filmic tonemap + bloom + eye
# adaptation + MSAA + anisotropy), every gameplay effect cranked
# (sword trails, blood, ragdoll, speed lines, flow juice), the sword
# bullet-time ruleset, the cyber-Angelyss hero, and a field of bots to
# cut down. The deliberate opposite of the PSX preset (run.sh -p).
#
#   ./scripts/showcase.sh                 arena, Assassins, windowed (default)
#   ./scripts/showcase.sh -f              ... fullscreen
#   ./scripts/showcase.sh arena [seed]    combat arena: audio-reactive trails +
#                                         full effects vs a squad of Assassins
#   ./scripts/showcase.sh course [seed]   a flowing surf/strafe course
#   ./scripts/showcase.sh kb [seed]       killbox — vertical melee pit
#
#   WEAPON=sword ./scripts/showcase.sh    arena with the katana (default)
#   WEAPON=vectorgun ./scripts/showcase.sh   arena with the speed-scaled rail
#   BOTS=6 ./scripts/showcase.sh          how many Assassins to fill (default 5)
#   SKILL=4 ./scripts/showcase.sh         bot skill 1-5 (default 4)
#   FULLSCREEN=1 ./scripts/showcase.sh    same as -f
#   FORCEMODEL=1 ./scripts/showcase.sh    make EVERYONE the cyber Angelyss
#
# Capture, once you're in (bound by autoexec.cfg):
#   F9 = start/stop 60fps video   F8 = demo record   F10 = screenshot
#
# Build first with ./scripts/build.sh.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine/build/Release"
STRAFEGEN="$ROOT/tools/strafegen"
OA="${OA:-$ROOT/assets/openarena}"
APP="$ENGINE/ioquake3.app/Contents/MacOS/ioquake3"

if [ ! -x "$APP" ]; then
	echo "engine not built — run ./scripts/build.sh first" >&2
	exit 1
fi
if [ ! -d "$OA/baseoa" ]; then
	echo "OpenArena assets not found at $OA (set OA=/path/to/openarena)" >&2
	exit 1
fi

# clear a stale pid (a killed instance leaves a modal "abnormal exit" dialog)
rm -f "$HOME/Library/Application Support/Quake3/baseoa/ioq3.pid"

# --- deploy the modded dylibs (must travel together; re-sign for dlopen) ---
if [ -f "$ENGINE/baseq3/qagame.dylib" ]; then
	for d in qagame cgame ui; do
		cp "$ENGINE/baseq3/$d.dylib" "$OA/baseoa/$d.dylib" 2>/dev/null
		codesign -f -s - "$OA/baseoa/$d.dylib" 2>/dev/null
	done
fi

# --- stage the ruleset + presets so /exec works in-game ---
cp "$STRAFEGEN/strafe64.cfg"   "$OA/baseoa/strafe64.cfg"   2>/dev/null
cp "$STRAFEGEN/showcase.cfg"   "$OA/baseoa/showcase.cfg"   2>/dev/null
cp "$STRAFEGEN/arena.cfg"      "$OA/baseoa/arena.cfg"      2>/dev/null
cp "$STRAFEGEN/sword_arena.cfg" "$OA/baseoa/sword_arena.cfg" 2>/dev/null
cp "$STRAFEGEN/arena_vg.cfg"   "$OA/baseoa/arena_vg.cfg"   2>/dev/null

# --- pick the map mode -------------------------------------------------
FULLSCREEN="${FULLSCREEN:-0}"
if [ "$1" = "-f" ]; then FULLSCREEN=1; shift; fi

MODE="${1:-arena}"
SEED="${2:-1337}"
[ "$SEED" = new ] && SEED=$(jot -r 1 1 99999)

case "$MODE" in
arena)  MAP="strafe64dm_$SEED"; GEN="$SEED --arena" ;;
kb|killbox) MAP="strafe64kb_$SEED"; GEN="$SEED --killbox" ;;
course) MAP="strafe64_${SEED}_x3"; GEN="$SEED --length 3" ;;
*)      # treat an unknown first arg as a literal, already-deployed map name
	MAP="$MODE"; GEN="" ;;
esac

# generate + deploy the map pk3. Regenerate when it's missing, when REGEN=1 is
# set, or when the generator (strafegen.py / strafegen_gfx.py — the graphics
# recipes) is NEWER than the deployed pk3. Without the staleness check, an old
# pre-graphics pk3 sitting in baseoa is reused forever and the new hull/chrome/
# sun never appear ("I wired graphics in but the map looks the same").
if [ -n "$GEN" ]; then
	DEPLOYED="$OA/baseoa/$MAP.pk3"
	if [ ! -f "$DEPLOYED" ] || [ "${REGEN:-0}" = 1 ] \
	   || [ "$STRAFEGEN/strafegen.py" -nt "$DEPLOYED" ] \
	   || [ "$STRAFEGEN/strafegen_gfx.py" -nt "$DEPLOYED" ]; then
		echo "generating $MAP (graphics-current) ..."
		python3 "$STRAFEGEN/strafegen.py" $GEN --pk3 --out "$STRAFEGEN/generated" || exit 1
		cp "$STRAFEGEN/generated/$MAP.pk3" "$OA/baseoa/" || exit 1
		# shared shader+textures now ship in ONE deduped pak — deploy it too,
		# else the lean map pk3 renders with the default grey shader.
		cp "$STRAFEGEN/generated/zzz_strafe64_shader.pk3" "$OA/baseoa/" 2>/dev/null
	fi
fi

BOTS="${BOTS:-5}"
SKILL="${SKILL:-4}"
FORCEMODEL="${FORCEMODEL:-0}"

# --- arena mode: audio-reactive trails + full effects + Assassin bots ---
# Non-arena modes (course/kb) keep the generic showcase preset and random
# bot fill. The arena gets the dedicated arena.cfg preset (trails!), a
# selectable weapon, and a squad of Assassins instead of random bots.
PRESET="showcase.cfg"
PRELATCH=""                       # cvars latched on the command line (pre-map)
BOTFILL="+set bot_minplayers $BOTS"
SQUAD_EXEC=""
WEAPON="${WEAPON:-sword}"
if [ "$MODE" = "arena" ]; then
	PRESET="arena.cfg"
	BOTFILL="+set bot_minplayers 0"   # the Assassin squad below fills the field
	case "$WEAPON" in
	vg|vector|vectorgun)
		PRELATCH="+set g_vectorgun 1"   # speed-scaled rail for everyone
		WBIND='bind 1 "weapon 7"'       # railgun = the vectorgun
		WSWORD=0; WLABEL="VECTORGUN (speed-rail)" ;;
	*)
		PRELATCH="+set g_vectorgun 0"   # pure melee
		WBIND='bind 1 "weapon 11"'      # katana
		WSWORD=1; WLABEL="SWORD (katana)" ;;
	esac
	# Generate the Assassin squad (exec'd after the map loads). Quotes + the
	# non-latched g_botSwordOnly live in the cfg, not the engine command line.
	SQUAD="$OA/baseoa/arena_squad.cfg"
	{
		echo "// generated by showcase.sh — $BOTS Assassins @ skill $SKILL, $WLABEL"
		echo "seta g_botSwordOnly $WSWORD"
		echo "$WBIND"
		echo "wait 80"
		d=300; n=0
		while [ "$n" -lt "$BOTS" ]; do
			echo "addbot Assassin $SKILL free $d"
			d=$((d + 700)); n=$((n + 1))
		done
	} > "$SQUAD"
	SQUAD_EXEC="+exec arena_squad.cfg"
	echo "ARENA :: weapon=$WLABEL  |  $BOTS Assassins"
fi

echo "SHOWCASE :: $MAP  |  $BOTS bots @ skill $SKILL  |  fullscreen=$FULLSCREEN"

# Renderer beauty cvars go on the command line (read at GL init — no vid_restart).
# FULL EYE CANDY (tuned 2026-06-23 for contrast, anti-blowout): HDR + filmic tonemap
# + reflective PBR (cubemap/specular/normal/deluxe) + SSAO + a darker exposure for the
# moody reflective look. r_cameraExposure / r_forceAutoExposure* are CVAR_CHEAT, so we
# launch with +devmap (cheats on) or they'd reset to defaults and the dark look would
# never apply. Bloom kept low (0.18) and auto-exposure clamped (max +0.7 stops) so the
# additive neon datamosh + bright dev floors don't blow out to white.
# CONTRAST: r_forceToneMap 1 pins the filmic curve (min/avg/max in stops) instead
# of letting it auto-flatten — crushed blacks + a hard white point give the scene
# real punch (dark hull monoliths vs blown neon) instead of a flat mid-grey wash.
# To dial back: raise r_cameraExposure toward 0.85 (brighter), raise r_forceToneMapMin
# toward -8 (lift the blacks / less contrast), drop r_pbr/r_cubeMapping 0 (less
# chrome), or r_bloom 0 (no glow). $PRESET applies the live gameplay/effect
# half; $PRELATCH latches the weapon ruleset BEFORE +devmap; $SQUAD_EXEC spawns bots after.
exec "$APP" \
	+set com_basegame baseoa +set fs_basepath "$OA" \
	+set sv_pure 0 +set vm_game 0 +set vm_cgame 0 +set vm_ui 0 \
	+set cl_renderer opengl2 \
	+set r_postProcess 1 +set r_hdr 1 +set r_toneMap 1 \
	+set r_autoExposure 1 +set r_cameraExposure 0.52 \
	+set r_forceAutoExposureMin -1.5 +set r_forceAutoExposureMax 0.4 \
	+set r_forceToneMap 1 +set r_forceToneMapMin -5.5 +set r_forceToneMapAvg -2.1 +set r_forceToneMapMax 0.1 \
	+set r_mapOverBrightBits 1 \
	+set r_bloom 0.18 +set r_bloomBlur 1.1 \
	+set r_dof 1 +set r_dofFocalRange 420 \
	+set cg_dofBulletTime 1 +set cg_dofMax 12 +set cg_dofFocusTrace 1 \
	+set r_pbr 1 +set r_cubeMapping 1 +set r_specularMapping 1 \
	+set r_normalMapping 1 +set r_deluxeMapping 1 \
	+set r_ssao 0 \
	+set r_parallaxMapping 2 \
	+set r_sunShadows 1 +set r_shadowFilter 2 +set r_shadowMapSize 4096 \
	+set r_forceSunAmbientScale 0.45 \
	+set r_ext_multisample 4 \
	+set r_ext_texture_filter_anisotropic 1 +set r_ext_max_anisotropy 16 \
	+set r_textureMode GL_LINEAR_MIPMAP_LINEAR +set r_picmip 0 \
	+set r_vertexLight 0 \
	+set r_fullscreen "$FULLSCREEN" \
	+set bot_enable 1 $BOTFILL +set g_spSkill "$SKILL" \
	+set cg_forceModel "$FORCEMODEL" \
	$PRELATCH \
	+devmap "$MAP" \
	+exec "$PRESET" \
	$SQUAD_EXEC
