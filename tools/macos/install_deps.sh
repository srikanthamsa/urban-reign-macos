#!/bin/sh
# Build dependencies for the native Apple Silicon (arm64) port.
# Requires Homebrew and the Xcode command line tools. The game image is not installed or downloaded here.
set -e

if [ "$(uname -m)" != "arm64" ]; then
    echo "This port targets Apple Silicon (arm64); this machine is $(uname -m)." >&2
    exit 1
fi
if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required: https://brew.sh/" >&2
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Install the Xcode command line tools first: xcode-select --install" >&2
    exit 1
fi

brew install cmake ninja pkg-config sdl3 shaderc molten-vk vulkan-headers libpng

echo
echo "Dependencies installed. MoltenVK and shaderc are copied beside libpcsx2gs.dylib by CMake."
echo "Next: copy your own Urban Reign (USA, SLUS-21209) .iso into game/ and run ./build.py"
