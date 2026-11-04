#pragma once

#include <filesystem>

namespace urban::combatcapture
{
    // F10 starts/stops a combat trace in <directory>/capturas. Samples and events run on the game thread.
    void install(const std::filesystem::path &directory);
    // Called after runtime.run() has joined the game thread, to finish an open capture on exit.
    void finish();
}
