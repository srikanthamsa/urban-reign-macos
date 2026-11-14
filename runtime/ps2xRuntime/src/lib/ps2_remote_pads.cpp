#include "ps2_remote_pads.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ps2_remote_pads
{
    namespace
    {
        constexpr int kMaxPads = 16;
        constexpr auto kExpiry = std::chrono::milliseconds(1500);

        struct Pad
        {
            std::atomic<bool> active{false};
            std::atomic<uint16_t> buttons{0};
            std::atomic<int16_t> lx{0}, ly{0}, rx{0}, ry{0};
            std::atomic<uint8_t> lt{0}, rt{0};
            std::atomic<int64_t> seenMs{0};
            std::atomic<bool> announced{false};
        };

        std::array<Pad, kMaxPads> g_pads;
        std::atomic<int> g_port{0};
        std::once_flag g_once;

        int64_t nowMs()
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        }

        bool enabled()
        {
            const char *value = std::getenv("PS2X_REMOTE_PADS");
            return !(value && (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0));
        }

        int configuredPort()
        {
            const char *value = std::getenv("PS2X_REMOTE_PAD_PORT");
            const int port = value ? std::atoi(value) : 50505;
            return port > 0 && port < 65536 ? port : 50505;
        }

        // SDL's button order differs from the mask's bit order in four places.
        constexpr std::array<int, 15> kMaskBit{0, 1, 2, 3, 6, 8, 7, 9, 10, 4, 5, 11, 12, 13, 14};

        void listenLoop(int fd, const char *bindAddress, int port)
        {
            std::cerr << "[input] remote pads on " << (bindAddress && *bindAddress ? bindAddress : "0.0.0.0") << ":"
                      << port << " (13-byte datagrams, player id -> SDL-)"
                      << std::endl;
            for (;;)
            {
                uint8_t buffer[64];
                const ssize_t size = ::recv(fd, buffer, sizeof(buffer), 0);
                if (size != 13)
                    continue;
                const uint8_t player = buffer[0];
                if (player == 0 || player > kMaxPads)
                    continue;
                Pad &pad = g_pads[static_cast<size_t>(player - 1)];
                uint16_t buttons = 0;
                int16_t lx = 0, ly = 0, rx = 0, ry = 0;
                // The wire format is little endian, like every supported host.
                std::memcpy(&buttons, buffer + 1, sizeof(buttons));
                std::memcpy(&lx, buffer + 3, sizeof(lx));
                std::memcpy(&ly, buffer + 5, sizeof(ly));
                std::memcpy(&rx, buffer + 7, sizeof(rx));
                std::memcpy(&ry, buffer + 9, sizeof(ry));
                pad.buttons.store(buttons, std::memory_order_relaxed);
                pad.lx.store(lx, std::memory_order_relaxed);
                pad.ly.store(ly, std::memory_order_relaxed);
                pad.rx.store(rx, std::memory_order_relaxed);
                pad.ry.store(ry, std::memory_order_relaxed);
                pad.lt.store(buffer[11], std::memory_order_relaxed);
                pad.rt.store(buffer[12], std::memory_order_relaxed);
                pad.seenMs.store(nowMs(), std::memory_order_relaxed);
                pad.active.store(true, std::memory_order_release);
                if (!pad.announced.exchange(true))
                    std::cerr << "[input] remote pad " << static_cast<int>(player) << " -> SDL-"
                              << static_cast<int>(player - 1) << std::endl;
            }
        }
    }

    void start()
    {
        std::call_once(g_once, []
        {
            if (!enabled())
                return;
            const int port = configuredPort();
            const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0)
            {
                std::cerr << "[input] remote pads: socket() failed" << std::endl;
                return;
            }
            const char *bindAddress = std::getenv("PS2X_REMOTE_PAD_BIND");
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<uint16_t>(port));
            address.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bindAddress && *bindAddress && ::inet_pton(AF_INET, bindAddress, &address.sin_addr) != 1)
            {
                std::cerr << "[input] remote pads: bad PS2X_REMOTE_PAD_BIND \"" << bindAddress << "\"" << std::endl;
                ::close(fd);
                return;
            }
            if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
            {
                std::cerr << "[input] remote pads: cannot bind UDP " << port << " (" << std::strerror(errno) << ")"
                          << std::endl;
                ::close(fd);
                return;
            }
            g_port.store(port);
            std::thread(listenLoop, fd, bindAddress, port).detach();
        });
    }

    bool active(int device)
    {
        if (device < 0 || device >= kMaxPads)
            return false;
        Pad &pad = g_pads[static_cast<size_t>(device)];
        if (!pad.active.load(std::memory_order_acquire))
            return false;
        if (nowMs() - pad.seenMs.load(std::memory_order_relaxed) <= kExpiry.count())
            return true;
        pad.active.store(false, std::memory_order_release);
        if (pad.announced.exchange(false))
            std::cerr << "[input] remote pad " << device + 1 << " stopped" << std::endl;
        return false;
    }

    bool button(int device, int sdlButton)
    {
        if (!active(device) || sdlButton < 0 || sdlButton >= static_cast<int>(kMaskBit.size()))
            return false;
        const uint16_t mask = g_pads[static_cast<size_t>(device)].buttons.load(std::memory_order_relaxed);
        return (mask & (1u << kMaskBit[static_cast<size_t>(sdlButton)])) != 0;
    }

    float axis(int device, int sdlAxis)
    {
        if (!active(device) || sdlAxis < 0 || sdlAxis > 5)
            return 0.0f;
        const Pad &pad = g_pads[static_cast<size_t>(device)];
        switch (sdlAxis)
        {
        case 0:
            return static_cast<float>(pad.lx.load(std::memory_order_relaxed)) / 32767.0f;
        case 1:
            return static_cast<float>(pad.ly.load(std::memory_order_relaxed)) / 32767.0f;
        case 2:
            return static_cast<float>(pad.rx.load(std::memory_order_relaxed)) / 32767.0f;
        case 3:
            return static_cast<float>(pad.ry.load(std::memory_order_relaxed)) / 32767.0f;
        case 4:
            return static_cast<float>(pad.lt.load(std::memory_order_relaxed)) / 255.0f;
        default:
            return static_cast<float>(pad.rt.load(std::memory_order_relaxed)) / 255.0f;
        }
    }

    int port()
    {
        return g_port.load();
    }
}
