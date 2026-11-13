#!/bin/sh
# Rebuild the VU microcode AOT library (generated/vu1) after a capture run. The game uses VU0/VU1 microprograms for
# geometry; interpreting them is the main CPU cost. Run this, play until the scenes you care about have appeared, then
# close the game; the script compiles the captures and rebuilds urban.
set -e
ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD:-"$ROOT/build/macos"}
CAP=${1:-"$ROOT/build/vu-capture"}
ISO=${2:-}
mkdir -p "$CAP"

echo "Capturing VU images into $CAP. Play through the scenes, then close the game."
if [ -n "$ISO" ]; then
    PS2X_VU1_CAPTURE="$CAP" PS2X_VU0_CAPTURE="$CAP" "$BUILD/bin/urban" --iso "$ISO"
else
    PS2X_VU1_CAPTURE="$CAP" PS2X_VU0_CAPTURE="$CAP" "$BUILD/bin/urban"
fi

echo "Compiling captured images into generated/vu1"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DURBAN_BUILD_TOOLS=ON -DPS2X_ENABLE_SDL_INPUT=OFF
cmake --build "$BUILD" --target vu1_recomp -- -j 8
mkdir -p "$ROOT/generated/vu1"
"$BUILD/runtime/ps2xTest/vu1_recomp" "$CAP" "$ROOT/generated/vu1"

echo "Rebuilding urban with the AOT VU library"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DURBAN_BUILD_TOOLS=OFF -DPS2X_ENABLE_SDL_INPUT=ON
cmake --build "$BUILD" --target urban -- -j 8
