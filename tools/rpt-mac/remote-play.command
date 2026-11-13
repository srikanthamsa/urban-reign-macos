#!/bin/zsh
# Double-click to host the ported game with Steam Remote Play Together.
# The game must already be a Steam shortcut and running from the Steam library.
cd "$(dirname "$0")" || exit 1
clear
echo "Steam Remote Play Together"
echo "--------------------------"
echo

if [ ! -x ./rptmac ]; then
    echo "Building rptmac..."
    make -s || { echo "Build failed (Xcode command line tools needed)."; read -r; exit 1; }
    echo
fi

if ! ./rptmac verify >/dev/null 2>&1; then
    echo "Steam isn't reachable."
    echo "Open Steam, sign in, then run this again."
    echo
    read -r "?Press Enter to close. "
    exit 1
fi

match=$(./rptmac apps "Urban Reign" | head -1)
if [[ -z "$match" ]]; then
    echo "No Steam shortcut named Urban Reign."
    echo "Add one first: Steam > Games > Add a Non-Steam Game > Browse > the Urban Reign app."
    echo
    read -r "?Press Enter to close. "
    exit 1
fi

APPID=$(print -r -- "$match" | awk -F'\t' '{print $1}')
echo "Shortcut: $(print -r -- "$match" | awk -F'\t' '{print $2}')  (appid $APPID)"
echo

MODE=game
if ! pgrep -f "Urban Reign.app/Contents/MacOS/urban" >/dev/null 2>&1; then
    echo "The game doesn't look like it's running."
    echo "Start it from the Steam Library so guests see the game window,"
    echo "then press Enter. Press Enter now to stream your desktop instead."
    read -r
    if pgrep -f "Urban Reign.app/Contents/MacOS/urban" >/dev/null 2>&1; then
        MODE=game
    else
        MODE=desktop
        echo "Using desktop mode."
    fi
fi

read -r "?How many friends are joining? (1-3) [2]: " N
[[ -z "$N" ]] && N=2
(( N < 1 )) && N=1
(( N > 3 )) && N=3

echo
echo "A link appears per friend once the session starts. Copy one per person."
echo "Leave this window open while you play. Press Ctrl-C to end."
echo
./rptmac host $APPID $MODE $N
echo
echo "Session ended."
read -r "?Press Enter to close. "
