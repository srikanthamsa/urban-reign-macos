# thirdparty/

| folder | what | from | license |
|---|---|---|---|
| `pcsx2/` | PCSX2's GS (hardware renderer), `common/` and the libraries they use (fmt, imgui, cpuinfo, D3D12MA, xbyak, fast_float, WIL, xxhash) | [PCSX2/pcsx2](https://github.com/PCSX2/pcsx2) commit `2954ebe` (2026-10-11) | GPL-3.0 (`pcsx2/COPYING.GPLv3`); each library's license is in its folder |
| `pcsx2-gs/` | ours: builds `pcsx2/` into `pcsx2gs.dll` with a C interface (`include/pcsx2gs_api.h`), the host stubs, the shaders/font and the shader cache. See `docs/GS_PCSX2.md` | this project | GPL-3.0 |
| `directx-headers/` | `include/` of DirectX-Headers (`directx/d3d12.h` and friends, needed by PCSX2's DX12 code) | [microsoft/DirectX-Headers](https://github.com/microsoft/DirectX-Headers) commit `461ef51` | MIT (`directx-headers/LICENSE`) |
| `sse2neon/` | SSE/SSE2/SSE4.1 to NEON translation header for the arm64 runtime | [DLTcollab/sse2neon](https://github.com/DLTcollab/sse2neon) | MIT (`sse2neon/LICENSE`) |
| `pcsx2/3rdparty/vixl/` | ARM64 assembler used by PCSX2's ARM64 software-rasterizer code generators | [PCSX2/pcsx2](https://github.com/PCSX2/pcsx2) commit `2954ebe` (`3rdparty/vixl`) | BSD-3-Clause (see `vixl/LICENCE`) |

`pcsx2/` holds only the files the build compiles or includes: the sources listed in `pcsx2-gs/CMakeLists.txt` and
the headers from Ninja's dependency log of a full build (`ninja -t deps`), plus the license files and the whole
`3rdparty/cpuinfo` (it is built with its own CMakeLists). The folder layout is PCSX2's, so a newer PCSX2 can be
copied over the same paths. `pcsx2-gs/tools/make_loaders.py` and `extract_config.py` read a full PCSX2 checkout.

For the Linux build (`docs/LINUX.md`) the same commit's Vulkan renderer was added: `pcsx2/GS/Renderers/Vulkan/`,
`common/Linux/LnxHostSys.cpp` and `LnxThreads.cpp`, `3rdparty/vulkan/` (Vulkan headers and VulkanMemoryAllocator, with
their licenses), `pcsx2/BuildVersion.h` and `common/CocoaTools.h` (included by the Vulkan files; the version values are
in `pcsx2-gs/compat/HostStubs.cpp`). `common/Linux/LnxMisc.cpp` is replaced by `pcsx2-gs/compat/LnxMisc.cpp` (no
dbus/XInput2).

For the macOS build (`docs/MACOS.md`) the same commit's ARM64 GS files were added: `GSVector4_arm64.h`,
`GSVector4i_arm64.h`, the `.arm64.cpp/.h` software-rasterizer code generators, `3rdparty/vixl`, `common/CocoaTools.mm`,
`common/Darwin/DarwinMisc.cpp`, `common/Darwin/DarwinThreads.cpp` and `common/Linux/LnxHostSys.cpp` (which contains the
Darwin JIT-write-protect path). `sse2neon` provides the runtime's SSE intrinsics on arm64.

One local change in a PCSX2 file: `pcsx2/GS/Renderers/Vulkan/VKShaderCache.cpp` also tries `libshaderc.so.1` when
`libshaderc_shared.so.1` does not load (Debian/Ubuntu's name for shaderc's shared library); marked "Urban Reign port".
