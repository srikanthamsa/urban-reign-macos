#include "urban/combat_capture.h"

#include "runtime/ps2_display_options.h"
#include "urban/function_hooks.h"
#include "urban/log.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace urban::combatcapture
{
#if defined(_WIN32)
    namespace
    {
        constexpr uint32_t kRamSize = 32u * 1024u * 1024u;
        constexpr uint32_t kFighterPointers = 0x0059D168u;
        std::filesystem::path g_directory;
        std::filesystem::path g_path;
        std::FILE *g_file = nullptr;
        std::array<char, 64u * 1024u> g_buffer{};
        uint64_t g_frame = 0;
        uint64_t g_rows = 0;
        double g_started = 0.0;
        double g_noteUntil = 0.0;
        bool g_keyDown = false;
        bool g_ready = false;

        template <typename T>
        T read(const uint8_t *rdram, uint32_t address)
        {
            T value{};
            std::memcpy(&value, rdram + address, sizeof(value));
            return value;
        }

        void stopCapture(const char *reason)
        {
            if (!g_file)
                return;
            bool failed = std::ferror(g_file) != 0;
            failed = std::fclose(g_file) != 0 || failed;
            g_file = nullptr;
            if (failed)
            {
                URBAN_ERROR(Game, "combat capture: write failed for " << g_path.string());
                ps2_host_display::setOverlayNote("COMBAT CAPTURE: write failed (check disk space)");
            }
            else
            {
                URBAN_INFO(Game, "combat capture saved: " << g_path.string() << " (" << g_frame << " frames, " << g_rows
                                                         << " rows, " << reason << ')');
                const std::string note = "COMBAT SAVED: capturas/" + g_path.filename().string();
                ps2_host_display::setOverlayNote(note.c_str());
            }
            g_noteUntil = log::elapsedSeconds() + 5.0;
        }

        void startCapture()
        {
            std::error_code error;
            std::filesystem::create_directories(g_directory, error);
            if (error)
            {
                URBAN_ERROR(Game, "combat capture: cannot create " << g_directory.string() << ": " << error.message());
                ps2_host_display::setOverlayNote("COMBAT CAPTURE: cannot create output folder");
                g_noteUntil = log::elapsedSeconds() + 5.0;
                return;
            }
            // Exclusive creation preserves captures from earlier sessions and other running copies.
            for (unsigned index = 1; ; ++index)
            {
                g_path = g_directory / ("combate_" + std::to_string(index) + ".csv");
                g_file = _wfopen(g_path.wstring().c_str(), L"wx");
                if (g_file || errno != EEXIST)
                    break;
            }
            if (!g_file)
            {
                URBAN_ERROR(Game, "combat capture: cannot open " << g_path.string());
                ps2_host_display::setOverlayNote("COMBAT CAPTURE: cannot open output file");
                g_noteUntil = log::elapsedSeconds() + 5.0;
                return;
            }
            std::setvbuf(g_file, g_buffer.data(), _IOFBF, g_buffer.size());
            std::fprintf(g_file, "frame,seconds,event,player,other,base,active,cpu,health,motion,motion_type,motion_frame,"
                                 "dodge_command,dodge_direction,dodge_timer,dodge_variant,reaction_flag,last_attacker\n");
            g_frame = 0;
            g_rows = 0;
            g_started = log::elapsedSeconds();
            g_noteUntil = 0.0;
            URBAN_INFO(Game, "combat capture started: " << g_path.string());
            ps2_host_display::setOverlayNote("COMBAT CAPTURE (F10 to stop and save)");
        }

        void handleKey(bool down, bool focused)
        {
            const bool pressed = down && !g_keyDown && focused;
            g_keyDown = down;
            if (pressed && g_ready)
            {
                if (g_file)
                    stopCapture("F10");
                else
                    startCapture();
            }
            if (g_file && std::ferror(g_file))
                stopCapture("write error");
            if (g_noteUntil > 0.0 && log::elapsedSeconds() >= g_noteUntil)
            {
                ps2_host_display::setOverlayNote("");
                g_noteUntil = 0.0;
            }
        }

        void sample(const uint8_t *rdram, const char *event, int player, int other, double seconds)
        {
            if (player < 0 || player >= 8)
                return;
            const uint32_t base = read<uint32_t>(rdram, kFighterPointers + static_cast<uint32_t>(player) * 4u);
            if (base == 0u || base > kRamSize - 0x9750u)
                return;
            std::fprintf(g_file, "%llu,%.6f,%s,%d,%d,0x%08x,%u,%u,%d,%d,%d,%.3f,%u,%d,%d,%d,%d,%d\n",
                         static_cast<unsigned long long>(g_frame), seconds, event, player + 1, other < 0 ? 0 : other + 1, base,
                         static_cast<unsigned>(read<uint16_t>(rdram, base + 0x88E4u)),
                         static_cast<unsigned>(read<uint16_t>(rdram, base + 0x8DE2u)),
                         static_cast<int>(read<int16_t>(rdram, base + 0x8E28u)),
                         read<int32_t>(rdram, base + 0x8D40u), read<int32_t>(rdram, base + 0x8D44u),
                         static_cast<double>(read<float>(rdram, base + 0x54u)),
                         static_cast<unsigned>(read<uint16_t>(rdram, base + 0x89C4u)),
                         static_cast<int>(read<int16_t>(rdram, base + 0x9444u)),
                         read<int32_t>(rdram, base + 0x9448u), read<int32_t>(rdram, base + 0x944Cu),
                         read<int32_t>(rdram, base + 0x9418u),
                         static_cast<int>(read<int16_t>(rdram, base + 0x9384u)));
            ++g_rows;
        }

        void sampleAll(const uint8_t *rdram, const char *event)
        {
            if (!g_file)
                return;
            const double seconds = log::elapsedSeconds() - g_started;
            for (int player = 0; player < 8; ++player)
                sample(rdram, event, player, -1, seconds);
        }
    }

    void install(const std::filesystem::path &directory)
    {
        g_directory = directory / "capturas";
        auto &hooks = FunctionHooks::instance();
        bool ready = hooks.observeEntry(0x128DC0u, "combat_capture_frame", [](uint8_t *, R5900Context *, PS2Runtime *) {
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            handleKey((GetAsyncKeyState(VK_F10) & 0x8000) != 0, foregroundPid == GetCurrentProcessId());
            if (g_file)
                ++g_frame;
        });
        const std::array<std::pair<uint32_t, const char *>, 3> phases{{
            {0x243A18u, "before_player_update"}, {0x247078u, "before_hits"}, {0x253810u, "after_hits"},
        }};
        for (const auto &[address, event] : phases)
        {
            ready = hooks.observeEntry(address, event, [event](uint8_t *rdram, R5900Context *, PS2Runtime *) {
                sampleAll(rdram, event);
            }) && ready;
        }
        const std::array<std::pair<uint32_t, const char *>, 5> events{{
            {0x24D790u, "dodge_check_entry"}, {0x24E880u, "hit_reaction_entry"}, {0x24DF70u, "dodge_reaction_entry"},
            {0x24F750u, "hit_entry"}, {0x254A08u, "motion_reset_entry"},
        }};
        for (const auto &[address, event] : events)
        {
            ready = hooks.observeEntry(address, event, [address, event](uint8_t *rdram, R5900Context *ctx, PS2Runtime *) {
                if (!g_file)
                    return;
                // Hit/reaction functions take attacker in a0 and victim in a1; the others take player in a0.
                const bool reaction = address == 0x24E880u || address == 0x24DF70u || address == 0x24F750u;
                const int player = static_cast<int>(getRegU32(ctx, reaction ? 5 : 4));
                const int other = reaction ? static_cast<int>(getRegU32(ctx, 4)) : -1;
                sample(rdram, event, player, other, log::elapsedSeconds() - g_started);
            }) && ready;
        }
        g_ready = ready;
        if (ready)
            URBAN_INFO(Game, "F10: start/stop combat capture (" << g_directory.string() << ")");
        else
            URBAN_ERROR(Game, "combat capture disabled: missing guest function hooks");
    }

    void finish()
    {
        stopCapture("game exit");
    }
#else
    void install(const std::filesystem::path &) {}
    void finish() {}
#endif
}
