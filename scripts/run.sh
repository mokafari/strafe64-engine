#!/bin/sh
# Launch STRAFE 64 on the locally-built ioquake3 engine with OpenArena's
# free assets. Build first with ./scripts/build.sh.
#
#   ./scripts/run.sh                  windowed, main menu
#   ./scripts/run.sh -f               fullscreen, main menu
#   ./scripts/run.sh -b [n] [map]     bot match: n bots (default 4) on map
#   ./scripts/run.sh -d <map>         dedicated server
#   ./scripts/run.sh -daily           today's ★ surf circuit + daily mutator
#
# Prefix any client mode with -p for the STRAFE 64 PSX low-fi preset:
#   ./scripts/run.sh -p               PSX look, main menu
#   ./scripts/run.sh -p -b 4 surf_daily_20260615
#
# vm_game/vm_cgame/vm_ui 0 load the modded native dylibs from baseoa/
# (requires sv_pure 0): qagame + cgame carry the movement/race mod, and
# ui.dylib is the STRAFE 64 NERV/MAGI reskinned main menu.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine/build/Release"
STRAFEGEN="$ROOT/tools/strafegen"
# OpenArena free assets — bundled in-tree under assets/openarena (gitignored).
# Override with OA=/path/to/openarena ./scripts/run.sh ...
OA="${OA:-$ROOT/assets/openarena}"

if [ ! -x "$ENGINE/ioquake3.app/Contents/MacOS/ioquake3" ]; then
	echo "engine not built — run ./scripts/build.sh first" >&2
	exit 1
fi
if [ ! -d "$OA/baseoa" ]; then
	echo "OpenArena assets not found at $OA (set OA=/path/to/openarena)" >&2
	exit 1
fi

# a stale pid file (from a killed instance) makes ioq3 block on a modal
# "abnormal exit" dialog before any output — clear it
rm -f "$HOME/Library/Application Support/Quake3/baseoa/ioq3.pid"

# sync all three modded dylibs from the same build output. they share
# networked headers (config strings, playerState layout), so a stale
# cgame against a fresh qagame makes the client exit on load — always
# deploy them together, never one at a time.
if [ -f "$ENGINE/baseq3/qagame.dylib" ]; then
	for d in qagame cgame ui; do
		cp "$ENGINE/baseq3/$d.dylib" "$OA/baseoa/$d.dylib" 2>/dev/null
		# Apple Silicon SIGKILLs a dylib with an invalid ad-hoc signature on
		# dlopen — a plain cp can invalidate it, so re-sign each deployed copy
		codesign -f -s - "$OA/baseoa/$d.dylib" 2>/dev/null
	done
fi

# stage the canonical gameplay ruleset so "/exec strafe64" is always
# available in-game (to reset cvars after experimenting). The arena
# presets ship too, so "/exec arena", "/exec sword_arena" and
# "/exec arena_vg" work from any session.
cp "$STRAFEGEN/strafe64.cfg"    "$OA/baseoa/strafe64.cfg"    2>/dev/null
cp "$STRAFEGEN/arena.cfg"       "$OA/baseoa/arena.cfg"       2>/dev/null
cp "$STRAFEGEN/sword_arena.cfg" "$OA/baseoa/sword_arena.cfg" 2>/dev/null
cp "$STRAFEGEN/arena_vg.cfg"    "$OA/baseoa/arena_vg.cfg"    2>/dev/null

# optional leading -p: stage and exec the PSX cvar preset on the client
PSX=""
if [ "$1" = "-p" ]; then
	cp "$STRAFEGEN/psx.cfg" "$OA/baseoa/psx.cfg"
	PSX="+exec psx.cfg"
	shift
fi

COMMON="+set com_basegame baseoa +set fs_basepath $OA +set sv_pure 0 +set vm_game 0"
FULLSCREEN=0
EXTRA=""

case "$1" in
-d)
	shift
	exec "$ENGINE/ioq3ded" $COMMON +map "${1:-aggressor}"
	;;
-f)
	FULLSCREEN=1
	;;
-b)
	BOTS="${2:-4}"
	MAP="${3:-aggressor}"
	EXTRA="+set bot_minplayers $BOTS +set g_spSkill 3 +map $MAP"
	;;
-daily)
	# the unified daily-speedrun run: a date-seeded surf circuit (same worldwide,
	# fresh each day) under the day's rotating mutator (g_mutator 9 resolves it
	# at map load). Generates + deploys today's map, then drops you straight in.
	STAMP="$(date -u +%Y%m%d)"
	MAP="surf_daily_$STAMP"
	mkdir -p "$OA/baseoa/maps"
	python3 "$STRAFEGEN/strafegen.py" --surf --daily \
		--out "$STRAFEGEN/generated" >/dev/null 2>&1
	cp "$STRAFEGEN/generated/$MAP.bsp" "$OA/baseoa/maps/" 2>/dev/null
	[ -f "$STRAFEGEN/generated/$MAP.aas" ] && \
		cp "$STRAFEGEN/generated/$MAP.aas" "$OA/baseoa/maps/" 2>/dev/null
	EXTRA="+set g_mutator 9 +map $MAP"
	;;
esac

exec "$ENGINE/ioquake3.app/Contents/MacOS/ioquake3" $COMMON \
	+set vm_cgame 0 +set vm_ui 0 +set r_fullscreen "$FULLSCREEN" $PSX $EXTRA
