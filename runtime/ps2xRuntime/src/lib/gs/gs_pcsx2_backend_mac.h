#pragma once

#include <cstdint>

// macOS window plumbing for PCSX2's GS. The GS renders through MoltenVK into a CAMetalLayer on a transparent child
// NSView over raylib's GLFW content view. The child ignores hit testing, so keyboard/mouse input stays with GLFW.
namespace gs_pcsx2_mac
{
    void *AttachHostWindow(void *raylibWindow, uint32_t &width, uint32_t &height, void *&surface);
    void HostTick(void *childView, uint32_t &width, uint32_t &height);
    void SetVisible(void *childView, bool visible);
}
