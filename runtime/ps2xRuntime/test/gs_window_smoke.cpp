// Brings up raylib's NSWindow/GLFW view and attaches the PCSX2 GS child NSView, without a game image.
#include "raylib.h"
#include "runtime/gs/gs_pcsx2_backend.h"

#include <cstdio>
#include <thread>
#include <chrono>
#include <vector>

int main()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(640, 448, "Urban Reign GS window smoke test");
    if (!IsWindowReady())
    {
        std::fprintf(stderr, "raylib window did not open\n");
        return 1;
    }
    GSPcsx2Backend::AttachHostWindow(GetWindowHandle());
    std::fprintf(stderr, "GS window attached: %s\n", GSPcsx2Backend::Active() ? "yes" : "no");

    std::thread gsThread([] {
        GSPcsx2Backend gs;
        std::vector<uint8_t> vram(4u * 1024u * 1024u, 0u);
        GSVSyncRegs regs{};
        gs.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
        for (int frame = 0; frame < 120; ++frame)
        {
            gs.VSync(regs);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    });
    for (int frame = 0; frame < 150 && !WindowShouldClose(); ++frame)
    {
        GSPcsx2Backend::HostTick();
        BeginDrawing();
        ClearBackground(BLACK);
        EndDrawing();
    }
    gsThread.join();
    CloseWindow();
    return GSPcsx2Backend::Active() ? 0 : 2;
}
