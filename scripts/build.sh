#!/bin/sh
# Build the STRAFE 64 engine + mod (ioquake3, arm64 Release) into engine/build.
#
#   ./scripts/build.sh            configure (if needed) + build
#   ./scripts/build.sh clean      wipe engine/build and rebuild from scratch
#
# Produces engine/build/Release/ : ioquake3.app, ioq3ded, and the modded
# baseq3/{qagame,cgame,ui}.dylib that run.sh deploys.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="$ROOT/engine"
BUILD="$ENGINE/build"

if [ "$1" = "clean" ]; then
	rm -rf "$BUILD"
fi

cmake -S "$ENGINE" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_OSX_ARCHITECTURES=arm64

cmake --build "$BUILD"
echo "built -> $BUILD/Release"
