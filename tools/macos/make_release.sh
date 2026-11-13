#!/bin/sh
# Package a distributable ZIP of Urban Reign.app: no disc image, no memory cards, no save states, no local settings.
# Usage: tools/macos/make_release.sh [build-dir]   (or python3 build.py release)
#
# The bundle is staged outside the repository (iCloud-synced folders add FinderInfo attributes that codesign
# rejects) and moved back as a ZIP.
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
BUILD=${1:-"$ROOT/build/macos"}
OUT="$ROOT/build/release"
NAME=urban-reign-macos-arm64

if [ ! -x "$BUILD/bin/urban" ]; then
    echo "urban not found in $BUILD/bin; run python3 build.py build first." >&2
    exit 1
fi

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/urban-reign-release.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT

URBAN_APP_DIR="$STAGE" URBAN_APP_NO_GAME=1 sh "$ROOT/tools/macos/make_app.sh" "$BUILD"
APP="$STAGE/Urban Reign.app"
CONTENTS="$APP/Contents/MacOS"
rm -rf "$CONTENTS"/memcards/* "$CONTENTS"/states/*

chmod -R u+w "$APP"
xattr -cr "$APP" 2>/dev/null || true
rm -rf "$APP/Contents/_CodeSignature"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

rm -rf "$OUT"
mkdir -p "$OUT"
(cd "$STAGE" && ditto -c -k --norsrc --noextattr --keepParent "Urban Reign.app" "$NAME.zip")
mv "$STAGE/$NAME.zip" "$OUT/$NAME.zip"

echo
echo "Created: $OUT/$NAME.zip ($(du -h "$OUT/$NAME.zip" | cut -f1))"
echo "The ZIP has no game data: players supply their own SLUS-21209 image."
