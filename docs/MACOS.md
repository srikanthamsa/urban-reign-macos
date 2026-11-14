# macOS / Apple Silicon build

This branch builds `urban` natively for arm64 macOS, with PCSX2's GS Vulkan renderer over MoltenVK. It uses raylib
for the window/input/debug UI, SDL3 for controllers, and `sse2neon` for the runtime's SSE intrinsics. Nothing runs
under Rosetta — there is no emulator in the loop.

## Requirements

- Apple Silicon Mac (M1 or newer), macOS 14 or newer.
- Xcode command line tools.
- Homebrew.
- Your own Urban Reign (USA, `SLUS-21209`) disc image in `game/`. The repository contains no game data.

Install the toolchain and libraries:

```sh
./tools/macos/install_deps.sh
```

This installs CMake, Ninja, pkg-config, SDL3, shaderc, MoltenVK, Vulkan headers and libpng. CMake copies
`libMoltenVK.dylib` and `libshaderc_shared.1.dylib` next to the built GS library, so no `DYLD_LIBRARY_PATH` is
needed at run time.

## Build

```sh
python3 build.py
```

The menu runs the same three steps as the other platforms:

1. **Generate the game code** from your own `.iso` (`ps2_recomp`, built natively for arm64).
2. **Build `urban`** into `build/macos/bin/urban`.
3. **Play**.

By hand:

```sh
python3 build.py generate
python3 build.py build
python3 build.py play
```

The first configure downloads raylib, SDL3, imgui and the recompiler dependencies; the first full generated-code build
is the long part. Incremental builds only rebuild what changed.

## App bundle

```sh
./tools/macos/make_app.sh
open "build/macos/Urban Reign.app"
```

The bundle gets the binary, every dylib from `bin/`, `resources`, `shadercache_pcsx2`, `mods`, `memcards`, `states`
and any `game/*.iso`. The image is looked up in `game/` next to the executable, which inside the bundle is
`Contents/MacOS/game/`.

`make_app.sh` signs the bundle ad-hoc and clears the extended attributes Homebrew leaves on its dylibs (codesign
rejects those). A local unsigned/ad-hoc build runs; distributing it needs a real Developer ID signature and
notarization, plus a review of the GPL/asset obligations.

**Optional icon.** Put `assets/UrbanReign.icns` in the repository root before running the script and the bundle gets
it (`CFBundleIconFile`). `assets/` is gitignored in this repository (it is game art); it is yours to supply.

**Distributable ZIP.**

```sh
python3 build.py release      # build/release/urban-reign-macos-arm64.zip
```

This stages a fresh bundle outside the repository (iCloud-synced folders add `com.apple.FinderInfo`, which codesign
refuses), strips the disc image, memory cards, save states and local settings, signs it, and zips it (~26 MB).

## Steam

Add the app as a non-Steam shortcut (Steam → Games → Add a Non-Steam Game → Browse) and launch it from the library,
so the overlay and play time work.

macOS specifics:

- Steam fails to spawn a shortcut whose executable path contains spaces. Use a space-free path, for example a
  symlink such as `~/UrbanReign.app` pointing at the built bundle.
- `~/Downloads` and `~/Desktop` are TCC-protected. A Steam-launched app cannot read a disc image stored there;
  keep the image inside the bundle (`Contents/MacOS/game/`) or in `game/`.
- Launch it from Steam, not from Terminal, or the overlay renderer is never injected.

## Remote Play Together

Steam does not offer Remote Play Together for non-Steam shortcuts. [`rpt-mac`](../tools/rpt-mac) attaches to the
running client through `steamclient.dylib` and drives `IClientRemoteClientManager` directly:

```sh
cd tools/rpt-mac && make
./rptmac apps "Urban Reign"           # appid, name, exe of the shortcut
./rptmac host <appid> game 2          # two guest links, stays alive until Ctrl-C
```

`verify` prints the interface's state and refuses builds whose vtable layout has not been mapped. Guest-side
streaming and input are still unverified (no second Steam account here).

## Remote controllers over UDP

`ps2_host_gamepads` is the only reader behind `controls.ini`'s `SDL-<n>/…` bindings, so a remote source can take over a
device slot without touching the pad emulation. `ps2_remote_pads.cpp` listens on UDP 50505 and maps `player_id N` to
device `N-1` (Pad2 = SDL-1, Pad3 = SDL-2, …). While packets arrive the slot reports connected and its buttons/axes come
from the packet; 1.5 s after the last packet the local device takes the slot back. The keyboard bindings in the same
section keep working, so a player can switch between pad and keys.

Packet: 13 bytes, little endian — `player_id u8`, `button_mask u16`, four `i16` sticks, two `u8` triggers. Sticks are
−32768…32767 with down/right positive, buttons use SDL's standard indices. Absolute state at 120 Hz, so loss needs no
recovery. `tools/remote-pad/` has the sender and its README.

| Variable | Default | Meaning |
|---|---|---|
| `PS2X_REMOTE_PADS` | on | `0` disables the listener |
| `PS2X_REMOTE_PAD_PORT` | `50505` | UDP port |
| `PS2X_REMOTE_PAD_BIND` | `0.0.0.0` | bind address; the tailnet IP ignores LAN traffic |

The listener authenticates nothing; it expects a trusted overlay (Tailscale/ZeroTier) or LAN. Logs to watch:
`[input] remote pads on …`, `[input] remote pad N -> SDL-<n>`, `[input] remote pad N stopped`.

## Renderer

`libpcsx2gs.dylib` is built with PCSX2's Vulkan device, MoltenVK, and an `NSView`/`CAMetalLayer` child over raylib's
window. The child ignores hit testing, so input remains with GLFW. The default `PS2X_PCSX2_VSYNC` is FIFO on macOS;
`0` (immediate) and `2` (mailbox) are available for testing.

The port's own OpenGL GS remains Windows-only. On macOS, leave `[Video] Renderer=pcsx2`; if the dylib is missing the
software GS is the fallback, but it is only a diagnostic path.

## Retail image verification

On an M4 Air with the USA `SLUS-21209` image, the native build boots through the PlayStation 2 logo, the intro
cutscenes, the title screen, the profile menu and the profile name-entry screen. PCSX2's GS reports fields and GIF
traffic continuously (21,600 fields observed) with the image displayed in the child `NSView`. Mission 01's fight runs
at `Game 59 · Screen 59 · 98% · worst frame 31 ms`; the cutscenes reach `Game 60–61 · Screen 61 · 100% · worst frame
20–21 ms`.

## Performance and image quality

The largest CPU cost on Apple Silicon is the VU microcode interpreter: the game's geometry/skinning programs run many
millions of FMACs per second. `generated/vu1` holds those programs compiled ahead of time (the same `vu1_recomp` path
the Windows/Linux ports use). With the captured library on this M4 Air, the intro/cutscenes measure **57-60 game FPS
and 97-100% speed at 3x internal resolution**, against 43 FPS and 80% without it.

The fight loads a *different* VU program from the cutscenes; with the fight program captured it holds 59 FPS. If only
the intro was captured, the fight falls back to the interpreter and measures about 17 FPS / 83% speed.

The default macOS video settings are now:

```ini
InternalResolution=3   # 1920x1344 from the 640x448 source
FXAA=0
TextureFiltering=3
TrilinearFiltering=2
AnisotropicFiltering=8
Sharpening=1
StartMaximized=1
```

`InternalResolution=4` (2560x1792) is sharper but measured ~55 FPS/91%; 3 is the 60-FPS sweet spot on this machine.
Presentation is immediate (`PS2X_PCSX2_VSYNC=0`) to keep input latency at one frame; `1` (FIFO) and `2` (mailbox)
are available but added latency or dropped the field rate.

`generated/vu1` is generated data and is not committed. Capturing is automatic: every VU image the game uploads is
written to `vu_capture/` next to the executable (or in the app bundle). Play through the missions and co-op modes you
care about once, then compile everything in one pass:

```sh
python3 build.py aot
```

This merges the CLI and app capture folders, runs `vu1_recomp`, and rebuilds `urban`; there is no per-mission step.
`./tools/macos/aot_vu.sh` is the same workflow for a single guided capture session (useful for a quick F5/F9 fight
state). Keep `generated/vu1` when you rerun `build.py generate`: the generator only syncs top-level `.cpp` files and
leaves that folder intact.

If a first-run profile save asks to format the memory card, choose **Yes**; it creates the card under
`build/macos/bin/memcards/`.

## Verification without a disc

Two small targets are built to prove the macOS graphics plumbing without game data:

```sh
cmake --build build/macos --target pcsx2_gs_test_mac ps2x_gs_window_smoke
cd build/macos/bin
./pcsx2_gs_test_mac ./libpcsx2gs.dylib       # MoltenVK device + present
./ps2x_gs_window_smoke                       # raylib NSWindow + NSView/CAMetalLayer child + GS presents
```

`ps2x_gs_window_smoke` should print `GS window attached: yes` and PCSX2 should report `60 fields`. To run the whole
no-disc check in one command:

```sh
./tools/macos/check_port.sh
```

## Troubleshooting

| Symptom | Fix |
|---|---|
| `"Urban Reign.app" is damaged` after downloading | `xattr -dr com.apple.quarantine "Urban Reign.app"` (no notarization yet). |
| `[CDIMAGE] cannot open ...iso` | Image missing, or stored in a TCC-protected folder (`~/Downloads`, `~/Desktop`). Copy it into `game/` or the bundle. |
| Steam does not start the shortcut | Path contains spaces; use a space-free symlink to the bundle. |
| No overlay | Launch from the Steam library. |
| Fights at ~17 FPS | Run `python3 build.py aot` after a capture run. |
| codesign: "resource fork, Finder information, or similar detritus not allowed" | The folder is iCloud-synced. Stage the bundle outside it (as `make_release.sh` does) or run `xattr -cr`. |

## Known limits

- The OpenGL native GS backend is not ported; PCSX2/Vulkan is the supported path.
- Debug tools that depend on Windows (`native_profile`, F10 combat capture, hardware watchpoints) are compiled out or
  no-ops on macOS.
- Mods are `.dylib` on macOS; CoopTool and ExampleMod are built by the normal target set.
- 8-player multitap, HD texture packs and guest-side Remote Play Together are implemented but untested here.
- The game still requires a legally owned disc image; no game files are included.

## Licensing

The port keeps the upstream GPL-3.0 licensing. sse2neon is MIT; MoltenVK, shaderc and the other Homebrew libraries
keep their own licenses.
