// Minimal macOS device test for libpcsx2gs.dylib: create an NSView, ask PCSX2's GS to open MoltenVK on it, pump the
// run loop for a few seconds, then close. It does not need a game image and proves the Vulkan/Metal surface path.
#import <AppKit/AppKit.h>

#include "pcsx2gs_api.h"

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <string>

int main(int argc, char **argv)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(100, 100, 640, 448);
        NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                                 NSWindowStyleMaskMiniaturizable
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];
        NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 640, 448)];
        [window setContentView:view];
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        const char *path = argc > 1 ? argv[1] : "libpcsx2gs.dylib";
        void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!library)
        {
            std::fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
            return 1;
        }
        auto get_api = reinterpret_cast<Pcsx2GsGetApiFn>(dlsym(library, "pcsx2gs_get_api"));
        if (!get_api)
        {
            std::fprintf(stderr, "no pcsx2gs_get_api\n");
            return 1;
        }
        const Pcsx2GsApi *api = get_api(PCSX2GS_API_VERSION);
        if (!api)
        {
            std::fprintf(stderr, "API version mismatch\n");
            return 1;
        }

        const std::filesystem::path exe = std::filesystem::current_path();
        const std::string resources = (exe / "resources").string();
        const std::string cache = (exe / "shadercache_pcsx2").string();
        Pcsx2GsOpenParams params{};
        params.hwnd = (__bridge void *)view;
        params.width = 640;
        params.height = 448;
        params.scale = 1.0f;
        params.resources = resources.c_str();
        params.cache = cache.c_str();
        params.vsync = 0;
        params.stretch = 0;
        params.options = "";
        if (!api->open(&params))
        {
            std::fprintf(stderr, "GS open failed\n");
            return 2;
        }
        std::fprintf(stderr, "GS open succeeded; presenting blank fields\n");

        Pcsx2GsPrivRegs regs{};
        const auto deadline = [NSDate dateWithTimeIntervalSinceNow:3.0];
        while ([deadline timeIntervalSinceNow] > 0)
        {
            @autoreleasepool
            {
                api->vsync(&regs);
                NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                    untilDate:[NSDate dateWithTimeIntervalSinceNow:0.016]
                                                       inMode:NSDefaultRunLoopMode
                                                      dequeue:YES];
                if (event)
                    [NSApp sendEvent:event];
            }
        }
        api->close();
        dlclose(library);
        [window orderOut:nil];
    }
    return 0;
}
