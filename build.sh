#!/usr/bin/env bash
#
# UI_Vulkan build helper (Linux / macOS).
#
#   ./build.sh            clean Release rebuild (default)
#   ./build.sh inc        incremental build (fast, keeps build dir)
#   ./build.sh debug      clean Debug rebuild
#   ./build.sh clean      explicit clean Release rebuild
#
# Dependencies (glfw / glm) are cached in .deps/ so a clean rebuild
# does NOT re-download them. Delete .deps/ only if deps get corrupted.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"
DEPS="${ROOT}/.deps"
TYPE="Release"
CLEAN=1

case "${1:-clean}" in
  inc|fast)      CLEAN=0 ;;
  debug)        TYPE="Debug" ;;
  release|clean|"") ;;
  -h|--help)
    sed -n '2,12p' "$0"; exit 0 ;;
  *)
    echo "Usage: $0 [clean|inc|debug]   (default: clean Release)"
    exit 1 ;;
esac

# Pick Ninja if available (much faster incremental), else default generator.
GEN_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GEN_ARGS=(-G Ninja)
fi

if [ "$CLEAN" = 1 ]; then
  echo ">> [clean] removing build/"
  rm -rf "$BUILD"
fi

if [ ! -f "$BUILD/CMakeCache.txt" ] || [ "$CLEAN" = 1 ]; then
  echo ">> configure: ${TYPE}  (deps cache: ${DEPS})"
  cmake -S "$ROOT" -B "$BUILD" "${GEN_ARGS[@]}" \
        -DCMAKE_BUILD_TYPE="$TYPE" \
        -DFETCHCONTENT_BASE_DIR="$DEPS"
fi

echo ">> build"
cmake --build "$BUILD" -j

echo
echo ">> done. Executables:"
for e in UI_Vulkan_SDR UI_Vulkan_HDR; do
  for p in "$BUILD/$e" "$BUILD/$TYPE/$e"; do
    [ -x "$p" ] && ls -la "$p"
  done
done
