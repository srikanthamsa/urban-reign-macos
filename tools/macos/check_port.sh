#!/bin/sh
# Build and run the macOS checks that need no game image. The actual game build still needs your own ISO.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD=${1:-"$ROOT/build/macos"}

echo "== architecture =="
uname -m
[ "$(uname -m)" = "arm64" ] || { echo "arm64 required" >&2; exit 1; }

echo "== configure/build =="
if [ -n "${URBAN_GENERATED_DIR:-}" ]; then
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DURBAN_GENERATED_DIR="$URBAN_GENERATED_DIR"
else
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64
fi
cmake --build "$BUILD" --target urban pcsx2_gs_test_mac ps2x_gs_window_smoke -- -j 8

echo "== urban --help =="
"$BUILD/bin/urban" --help

echo "== MoltenVK device =="
(
    cd "$BUILD/bin"
    ./pcsx2_gs_test_mac ./libpcsx2gs.dylib
)

echo "== raylib + NSView + MoltenVK =="
(
    cd "$BUILD/bin"
    ./ps2x_gs_window_smoke
)

echo "All no-disc macOS checks passed."
