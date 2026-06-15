#!/bin/sh
# Build id's bspc (the bot AAS compiler) from ../code/bspc for modern
# macOS/clang, and drop the binary here so strafegen.py finds it.
#
# Portability shims: malloc.h -> stdlib.h, and -D__linux__ to pick the
# little-endian non-PPC byte-order path in q_shared.h (correct on arm64).

set -e
cd "$(dirname "$0")"
SRC=../code/bspc

SHIM=$(mktemp -d)
trap 'rm -rf "$SHIM"' EXIT
echo '#include <stdlib.h>' > "$SHIM/malloc.h"

FILES=$(sed -n '/^GAME_OBJS/,/^$/p' "$SRC/Makefile" \
	| grep -oE '[A-Za-z0-9_./]+\.o' | sed "s|^|$SRC/|; s/\.o$/.c/")

clang -std=gnu89 -fcommon -w -O2 \
	-D__linux__ -DLINUX -DBSPC -Dstricmp=strcasecmp \
	-I"$SHIM" -o bspc $FILES -lm -lpthread

echo "built $(pwd)/bspc"
