# rpt-mac — Remote Play Together for any app on macOS

Steam only shows "Remote Play Together" for games it recognises. Non-Steam shortcuts (a recompiled game, an
emulator, a modded build) never get the invite button, and there is no setting for it.

`rptmac` attaches to the running Steam client through `steamclient.dylib` and drives
`IClientRemoteClientManager` — the same interface the overlay uses — to create a remote-play group, claim a guest
slot and print the connect link Steam would have shown you.

```
rptmac apps [name-filter]                      # non-Steam shortcuts: appid, name, exe
rptmac host <appid> [game|desktop] [guests]    # create the session, print one link per guest, stay alive
rptmac end  <appid>                            # ask Steam to disband the group
rptmac verify                                  # interface state + build sanity check
```

The host process must stay alive while friends play: it keeps the session and re-applies the per-guest input flags.
Ctrl-C ends it.

## Requirements

- Apple Silicon Mac with Steam installed and signed in (indices are mapped against the arm64 client build
  `1794424036`).
- Remote Play Together enabled: Steam → Settings → Remote Play → *Enable Remote Play*.
- Xcode command line tools (for `clang`).

## Build

```sh
make
```

## Usage

1. Add your app to Steam: **Games → Add a Non-Steam Game → Browse**, then launch it from the library.
2. Find the appid Steam assigned to the shortcut:

   ```sh
   ./rptmac apps "Your Game"
   2511043820	Urban Reign	/Users/you/UrbanReign.app/Contents/MacOS/urban
   ```

3. With the game running, start the session:

   ```sh
   ./rptmac host 2511043820 game 2
   ```

   `game` streams the game window, `desktop` streams the whole desktop (useful when Steam cannot resolve the
   window; appid `480` works for desktop alone). The last argument is the number of guests, 1–4.

4. Send one `https://s.team/remoteplay/connect/...` link per friend.

`remote-play.command` does the same thing in a double-clickable window for an app named "Urban Reign".

Desktop mode streams everything on screen — close private windows first.

## How it works

1. `dlopen` the Steam client's `steamclient.dylib` (Steam must be running).
2. `CreateInterface("SteamClient019")` → `CreateSteamPipe` → `ConnectToGlobalUser`.
3. `CreateInterface("CLIENTENGINE_INTERFACE_VERSION005")` → vtable slot 56 → `IClientRemoteClientManager`.
4. `BCreateRemotePlayGroup(appid)`, poll `GetLocalRemotePlayTogetherGroupID(appid)`.
5. `GetAvailableRemotePlayTogetherGuestID()` → fill a `RemotePlayPlayer { steam_id, guest_id, group_id, ... }` →
   `BCreateRemotePlayInviteAndSession(player, appid)`.
6. The invite callback (id `1100028`) carries a payload with the connect URL at a fixed offset. On macOS it is
   `https://s.team/remoteplay/connect/…` (Windows puts a `steam://` URL at a different offset).
7. While hosting, `SetPerUser{Keyboard,Mouse,Controller}InputEnabled(player, 1)` is re-applied every 3 s, because
   Steam resets those flags.

The interface is undocumented and its vtable layout moves between Steam builds. The indices are derived from the
shipped binary rather than guessed:

| Method | Slot |
|---|---|
| `BHasRemotePlayInviteAndSession` | 121 |
| `BCreateRemotePlayGroup` | 122 |
| `GetLocalRemotePlayTogetherGroupID` | 123 |
| `GetAvailableRemotePlayTogetherGuestID` | 125 |
| `BCreateRemotePlayInviteAndSession` | 126 |
| `SetStreamingDesktopToRemotePlayTogetherEnabled` | 130 |
| `SetPerUser{Keyboard,Mouse,Controller}InputEnabled` | 132 / 133 / 134 |
| `DisbandRemotePlayTogetherGroup` | 142 |

`verify` calls two cheap, well-understood slots and refuses to continue when the values do not look like the mapped
build. That is deliberate: calling an unmapped slot once killed a Steam process during development
(`GetStreamingPINSize` asserts on a cross-process pipe).

### After a Steam update

```sh
./rptmac dump > /tmp/vt.txt
lipo -thin arm64 "$HOME/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib" \
    -output /tmp/sc_arm64.dylib
./tools/derive_indices.py /tmp/sc_arm64.dylib /tmp/vt.txt
```

The script finds each method-name string, locates the code that references it, and reports which vtable slot
contains that code. Update the enum in `rptmac.c` if it reports drift.

## Limits

- Steam caps Remote Play Together at 4 players (host + 3 guests).
- The overlay still shows no Remote Play Together panel for non-Steam apps; the printed link is the entry point.
- `DisbandRemotePlayTogetherGroup` is accepted but inert on macOS: the group lives until Steam restarts or the host
  app exits. Quit Steam if a session must disappear immediately.
- Host side, group creation and link generation are verified on macOS. Guest joining, streaming quality and input
  hand-off are implemented but unverified (no second Steam account was available).
- Uses Steam internals that Valve may change at any time. It does not bypass DRM or ownership checks; use it with
  apps you own, for people you would invite to your couch.

## Credits

Approach and Windows implementation: [remoteplayforever](https://github.com/KayZedd/remoteplayforever) and
[RemotePlayWhatever](https://github.com/m4dEngi/RemotePlayWhatever). This is the macOS port.

GPL-3.0-or-later, like the rest of this repository.
