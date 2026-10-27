#!/bin/sh
# Build id's bspc (the bot AAS compiler) from ../code/bspc for modern
# macOS/clang, and drop the binary here so strafegen.py finds it.
#
# Portability shims: malloc.h -> stdlib.h, and -D__linux__ to pick the
# little-endian non-PPC byte-order path in q_shared.h (correct on arm64).

set -e
cd "$(dirname "$0")"
# bspc source: set BSPC_SRC (e.g. a clone of https://github.com/TTimo/bspc);
# the old default (../code/bspc) only existed in the original ioquake3 tree
SRC=${BSPC_SRC:-../code/bspc}
if [ ! -f "$SRC/Makefile" ]; then
	echo "bspc source not found at $SRC -- set BSPC_SRC (git clone https://github.com/TTimo/bspc)" >&2
	exit 1
fi
CC=${CC:-clang}

SHIM=$(mktemp -d)
trap 'rm -rf "$SHIM"' EXIT
echo '#include <stdlib.h>' > "$SHIM/malloc.h"

FILES=$(sed -n '/^GAME_OBJS/,/^$/p' "$SRC/Makefile" \
	| grep -oE '[A-Za-z0-9_./]+\.o' | sed "s|^|$SRC/|; s/\.o$/.c/")

"$CC" -std=gnu89 -fcommon -w -O2 \
	-D__linux__ -DLINUX -DBSPC -Dstricmp=strcasecmp \
	-I"$SHIM" -o bspc $FILES -lm -lpthread

echo "built $(pwd)/bspc"
