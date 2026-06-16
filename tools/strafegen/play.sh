#!/bin/sh
# Generate (if missing), install, and launch a strafegen course on the
# ioquake3 engine with OpenArena assets and the movement-mod dylibs.
#
#   ./play.sh                  run map: strafe64_1337_x3 (the house default)
#   ./play.sh 42               run map, seed 42 (difficulty 1, length 3)
#   ./play.sh new              run map, random seed
#   ./play.sh 42 2 1           run map: seed 42, difficulty 2, length 1
#
#   ./play.sh arena            arena: strafe64dm_1337, with bots support
#   ./play.sh arena 42         arena, seed 42
#   ./play.sh arena new 2      arena, random seed, difficulty 2
#
#   ./play.sh kb               killbox: strafe64kb_1337 (vertical melee arena)
#   BOTS=6 ./play.sh kb 42     killbox seed 42, 6 bots to hack and slash
#
#   BOTS=3 ./play.sh arena     auto-fill the arena with 3 bots
#   FULLSCREEN=1 ./play.sh     fullscreen instead of windowed

ENGINE=/Users/gustav/ioquake3/build/Release
OA=/Users/gustav/openarena-0.8.8
HERE=$(cd "$(dirname "$0")" && pwd)

if [ "$1" = arena ]; then
	SEED=${2:-1337}
	DIFF=${3:-1}
	[ "$SEED" = new ] && SEED=$(jot -r 1 1 99999)
	NAME="strafe64dm_${SEED}"
	[ "$DIFF" != 1 ] && NAME="${NAME}_d${DIFF}"
	GEN_ARGS="$SEED --arena --difficulty $DIFF"
elif [ "$1" = kb ] || [ "$1" = killbox ]; then
	SEED=${2:-1337}
	DIFF=${3:-1}
	[ "$SEED" = new ] && SEED=$(jot -r 1 1 99999)
	NAME="strafe64kb_${SEED}"
	[ "$DIFF" != 1 ] && NAME="${NAME}_d${DIFF}"
	GEN_ARGS="$SEED --killbox --difficulty $DIFF"
else
	SEED=${1:-1337}
	DIFF=${2:-1}
	LEN=${3:-3}
	[ "$SEED" = new ] && SEED=$(jot -r 1 1 99999)
	NAME="strafe64_${SEED}"
	[ "$DIFF" != 1 ] && NAME="${NAME}_d${DIFF}"
	[ "$LEN" != 1 ] && NAME="${NAME}_x${LEN}"
	GEN_ARGS="$SEED --difficulty $DIFF --length $LEN"
fi

if [ ! -f "$OA/baseoa/$NAME.pk3" ]; then
	echo "generating $NAME..."
	python3 "$HERE/strafegen.py" $GEN_ARGS --pk3 --out "$HERE/generated" || exit 1
	cp "$HERE/generated/$NAME.pk3" "$OA/baseoa/" || exit 1
fi

exec "$ENGINE/ioquake3.app/Contents/MacOS/ioquake3" \
	+set com_basegame baseoa +set fs_basepath "$OA" \
	+set sv_pure 0 +set vm_game 0 +set vm_cgame 0 \
	+set bot_enable 1 +set bot_minplayers "${BOTS:-0}" \
	+set r_fullscreen "${FULLSCREEN:-0}" \
	+map "$NAME"
