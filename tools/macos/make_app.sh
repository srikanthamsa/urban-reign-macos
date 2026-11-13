#!/bin/sh
# Build a self-contained (for this machine) Urban Reign.app from an existing build/macos tree.
# Usage: tools/macos/make_app.sh [build-dir]
#   URBAN_APP_DIR=<dir>   write the bundle there instead of <build-dir>
#   URBAN_APP_NO_GAME=1   do not copy the disc image (release packaging)
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD=${1:-"$ROOT/build/macos"}
BIN="$BUILD/bin"
APP="${URBAN_APP_DIR:-$BUILD}/Urban Reign.app"

if [ ! -x "$BIN/urban" ]; then
    echo "urban not found in $BIN; run python3 build.py build first." >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN/urban" "$APP/Contents/MacOS/urban"
# Dylibs are loaded from the executable's folder on macOS.
find "$BIN" -maxdepth 1 -name '*.dylib' -exec cp {} "$APP/Contents/MacOS/" \;
for item in resources shadercache_pcsx2 mods memcards states; do
    if [ -e "$BIN/$item" ]; then
        cp -R "$BIN/$item" "$APP/Contents/MacOS/"
    fi
done
# Keep the game image discoverable: urban searches game/ next to the executable.
mkdir -p "$APP/Contents/MacOS/game"
if [ -d "$ROOT/game" ] && [ "${URBAN_APP_NO_GAME:-0}" != "1" ]; then
    find "$ROOT/game" -maxdepth 1 -type f \( -name '*.iso' -o -name 'README.md' \) -exec cp {} "$APP/Contents/MacOS/game/" \;
fi
if ! ls "$APP/Contents/MacOS/game"/*.iso >/dev/null 2>&1; then
    echo "note: this bundle has no disc image; the player picks one in the launcher or puts it in game/" >&2
fi

# Optional local icon: drop assets/UrbanReign.icns in the repository root. It is game art,
# so it is not part of the repository.
ICON="$ROOT/assets/UrbanReign.icns"
ICON_KEY=""
if [ -f "$ICON" ]; then
    cp "$ICON" "$APP/Contents/Resources/UrbanReign.icns"
    ICON_KEY="<key>CFBundleIconFile</key><string>UrbanReign</string>"
fi

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>urban</string>
    <key>CFBundleIdentifier</key><string>local.urbanreign.port</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>Urban Reign</string>
    <key>CFBundleDisplayName</key><string>Urban Reign</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.4.1</string>
    <key>CFBundleVersion</key><string>1.4.1</string>
    ${ICON_KEY}
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
PLIST

# Ad-hoc signing is enough for a locally built app; replace "-" with a Developer ID for distribution.
# Homebrew's dylibs arrive read-only and with extended attributes, which codesign refuses.
chmod -R u+w "$APP"
xattr -cr "$APP"
if ! codesign --force --deep --sign - "$APP"; then
    echo "warning: ad-hoc codesign failed; the app may still run locally" >&2
fi

echo "Created: $APP"
