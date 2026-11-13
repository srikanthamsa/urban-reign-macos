# macOS / Apple Silicon build

This branch builds `urban` natively for arm64 macOS, with PCSX2's GS Vulkan renderer over MoltenVK. It uses raylib
for the window/input/debug UI, SDL3 for controllers, and `sse2neon` for the runtime's SSE intrinsics. Nothing is
emulated through Rosetta except the (separate) PCSX2 application if you choose to use it as a reference.

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

To make a locally signed app bundle:

```sh
./tools/macos/make_app.sh
open "build/macos/Urban Reign.app"
```

The image is looked up in `game/` next to the executable; inside the bundle that is
`Contents/MacOS/game/`. The script copies any `game/*.iso` there.

## Renderer

`libpcsx2gs.dylib` is built with PCSX2's Vulkan device, MoltenVK, and an `NSView`/`CAMetalLayer` child over raylib's
window. The child ignores hit testing, so input remains with GLFW. The default `PS2X_PCSX2_VSYNC` is FIFO on macOS;
`0` (immediate) and `2` (mailbox) are available for testing.

The port's own OpenGL GS remains Windows-only. On macOS, leave `[Video] Renderer=pcsx2`; if the dylib is missing the
software GS is the fallback, but it is only a diagnostic path.

## Retail image verification

On an M4 Air with the USA `SLUS-21209` image, the native build boots through the PlayStation 2 logo, the intro
cutscenes, the title screen, the profile menu and the profile name-entry screen. PCSX2's GS reports fields and GIF
traffic continuously (21,600 fields observed) with the image displayed in the child `NSView`.

## Performance and image quality

The largest CPU cost on Apple Silicon is the VU microcode interpreter: the game's geometry/skinning programs run many
millions of FMACs per second. `generated/vu1` holds those programs compiled ahead of time (the same `vu1_recomp` path
the Windows/Linux ports use). With the captured library on this M4 Air, the intro/cutscenes measure **57-60 game FPS
and 97-100% speed at 3x internal resolution**, against 43 FPS and 80% without it. Inside **Mission 01's actual fight**
the overlay reads `Game 59 FPS | Screen 59 | Speed 98% | worst frame 31 ms`.

The fight loads a *different* VU program from the cutscenes. If only the intro was captured, the fight falls back to the
interpreter and measures about 17 FPS / 83% speed. Capture while the first fight is on screen, or save a fight state
with F5 and load it with F9 during the capture run.

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

`generated/vu1` is generated data and is not committed. Capturing is now automatic: every VU image the game uploads is
written to `vu_capture/` next to the executable (or in the app bundle). Play through the missions and co-op modes you
care about once, then compile everything in one pass:

```sh
python3 build.py aot
```

This merges the CLI and app capture folders, runs `vu1_recomp`, and rebuilds `urban`; there is no per-mission step.
`./tools/macos/aot_vu.sh` is the same workflow for a single guided capture session (useful for a quick F5/F9 fight
state). Keep `generated/vu1` when you rerun `build.py generate`: the generator only
syncs top-level `.cpp` files and leaves that folder intact.

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

## Known limits

- The OpenGL native GS backend is not ported; PCSX2/Vulkan is the supported path.
- Debug tools that depend on Windows (`native_profile`, F10 combat capture, hardware watchpoints) are compiled out or
  no-ops on macOS.
- Mods are `.dylib` on macOS; CoopTool and ExampleMod are built by the normal target set.
- `tools/macos/make_app.sh` wraps the build into `Urban Reign.app` and attempts an ad-hoc code signature. A local
  unsigned/ad-hoc build runs; distributing it needs a real Developer ID signature/notarization and a review of the
  GPL/asset obligations.
- The game still requires a legally owned disc image; no game files are included.

## Licensing

The port keeps the upstream GPL-3.0 licensing. sse2neon is MIT; MoltenVK, shaderc and the other Homebrew libraries
keep their own licenses.
