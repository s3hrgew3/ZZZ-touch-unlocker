#!/usr/bin/env bash
# build-wine.sh — cross-compile the Wine payloads (hook DLL + attach injector)
# with MinGW-w64 and stage them in dist/wine together with the POSIX wrapper.
#
#   scripts/build-wine.sh              # Release, dist/wine
#   BUILD_DIR=/tmp/z BUILD_TYPE=Debug scripts/build-wine.sh
#
# Requirements: cmake >= 3.20, ninja (or make), x86_64-w64-mingw32-g++.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-wine}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DIST_DIR="${DIST_DIR:-$ROOT/dist/wine}"
TOOLCHAIN="$ROOT/cmake/mingw-w64-x86_64.cmake"

command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 || {
    echo "error: x86_64-w64-mingw32-g++ not found (apt install mingw-w64)" >&2
    exit 1
}

if command -v ninja >/dev/null 2>&1; then
    GENERATOR=(-G Ninja)
else
    GENERATOR=()
fi

echo "== configure =="
cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DZZZ_BUILD_TESTS=OFF

echo "== build =="
cmake --build "$BUILD_DIR" --parallel

echo "== stage $DIST_DIR =="
mkdir -p "$DIST_DIR"
install -m 0644 "$BUILD_DIR/zzz-touch-hook.dll" "$DIST_DIR/zzz-touch-hook.dll"
install -m 0755 "$BUILD_DIR/zzz-touch-inject.exe" "$DIST_DIR/zzz-touch-inject.exe"
# The native launcher is built too but refuses to run under Wine; keeping it out
# of dist/wine prevents it from being picked up by mistake.
install -m 0755 "$ROOT/wine/zzz-touch-unlocker" "$DIST_DIR/zzz-touch-unlocker"
install -m 0755 "$ROOT/wine/zzz-touch-selftest.sh" "$DIST_DIR/zzz-touch-selftest.sh"
install -m 0755 "$ROOT/scripts/check-wine-touch.sh" "$DIST_DIR/check-wine-touch.sh"
install -m 0644 "$ROOT/wine/touch-probe.c" "$DIST_DIR/touch-probe.c"
install -m 0644 "$ROOT/wine/uinput-tap.c" "$DIST_DIR/uinput-tap.c"
install -m 0644 "$ROOT/wine/README.md" "$DIST_DIR/README.md"

echo "== result =="
ls -l "$DIST_DIR"
echo
echo "Next: wine/zzz-touch-unlocker status   (or: status from $DIST_DIR)"
