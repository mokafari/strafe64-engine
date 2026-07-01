#!/bin/sh
# Build q3map2 (the Quake3 BSP/vis/light compiler) for macOS arm64 from
# netradiant-custom, with the two source fixes this codebase needs under
# Apple's toolchain. Produces tools/strafegen/q3map2 (gitignored, ~1.4MB).
#
# Why we build it: strafegen writes vertex-lit BSP directly (no lightmaps).
# Real Layer-3 baked lighting (radiosity, sun/sky, surfacelights, deluxe) needs
# q3map2 — which isn't on Homebrew and doesn't ship a macOS arm64 binary. So we
# build it once. The compiler links against Homebrew LLVM's libc++ via an
# absolute rpath, so `brew install llvm` must stay installed to run it.
#
#   ./tools/strafegen/build-q3map2.sh
#
# The two fixes (q3map2-macos.patch, applied automatically):
#   1. libs/generic/arrayrange.h — `using Span = std::span` relies on CTAD for
#      alias templates (C++20 P1814), unimplemented before clang 19. Replaced
#      with a thin std::span subclass + explicit deduction guides (78 call sites).
#   2. tools/quake3/q3map2/q3map2.h — mesh_t had `operator=(const&) = delete`, so
#      parseMesh_t / entity_t weren't copy-assignable. libc++'s forward_list
#      copy-assign needs value_type copy-ASSIGN (libstdc++ copy-constructs, which
#      is why upstream's Linux CI is fine). Implemented mesh_t copy-assign with
#      the same destroy + construct_at deep-copy pattern as its move-assign.
set -eu          # all vars are assigned in-script, so -u is safe here

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SG="$ROOT/tools/strafegen"
BUILD="${Q3MAP2_BUILD:-$HOME/q3map2-build}"
LLVM=/opt/homebrew/opt/llvm

echo ">> deps (brew)"
brew install llvm glib libxml2 jpeg libpng pkgconf assimp 2>/dev/null || true
[ -x "$LLVM/bin/clang++" ] || { echo "need: brew install llvm"; exit 1; }

echo ">> clone netradiant-custom -> $BUILD"
mkdir -p "$BUILD"; cd "$BUILD"
[ -d netradiant-custom ] || git clone --depth 1 https://github.com/Garux/netradiant-custom.git
cd netradiant-custom

echo ">> apply macOS/libc++ fixes"
git apply --check "$SG/q3map2-macos.patch" 2>/dev/null && git apply "$SG/q3map2-macos.patch" \
  || echo "   (patch already applied or rejected — continuing)"

echo ">> build q3map2 (llvm clang + its libc++)"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:/opt/homebrew/opt/libxml2/lib/pkgconfig:/opt/homebrew/opt/glib/lib/pkgconfig:/opt/homebrew/opt/jpeg/lib/pkgconfig:/opt/homebrew/opt/assimp/lib/pkgconfig"
make binaries-q3map2 -j8 \
  OS=Darwin MACLIBDIR=/opt/homebrew \
  CC="$LLVM/bin/clang" CXX="$LLVM/bin/clang++" \
  CPPFLAGS_JPEG="-I/opt/homebrew/opt/jpeg/include" \
  LIBS_JPEG="-L/opt/homebrew/opt/jpeg/lib -ljpeg" \
  LDFLAGS="-L$LLVM/lib/c++ -Wl,-rpath,$LLVM/lib/c++" \
  DEPEND_ON_MAKEFILE=no DEPENDENCIES_CHECK=off

echo ">> install -> $SG/q3map2"
cp install/q3map2.arm64 "$SG/q3map2"; chmod +x "$SG/q3map2"
"$SG/q3map2" 2>&1 | head -1
echo ">> done."
