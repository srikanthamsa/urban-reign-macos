#include "gs_pcsx2_backend_mac.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cstdio>

// The overlay only displays the GS image. Returning nil from hitTest keeps every mouse event with raylib's content
// view; the keyboard focus is unaffected because this view never becomes first responder.
@interface UrbanGSView : NSView
@end

@implementation UrbanGSView
- (NSView *)hitTest:(NSPoint)point
{
    (void)point;
    return nil;
}
@end

namespace gs_pcsx2_mac
{
    void *AttachHostWindow(void *raylibWindow, uint32_t &width, uint32_t &height, void *&surface)
    {
        surface = nullptr;
        if (!raylibWindow || ![NSThread isMainThread])
            return nullptr;

        // raylib's GetWindowHandle() is the NSWindow* on macOS (not the GLFWwindow*).
        NSWindow *window = (__bridge NSWindow *)raylibWindow;
        NSView *content = window ? [window contentView] : nil;
        if (!content)
            return nullptr;

        const NSRect bounds = [content bounds];
        width = static_cast<uint32_t>(std::max<CGFloat>(1.0, bounds.size.width));
        height = static_cast<uint32_t>(std::max<CGFloat>(1.0, bounds.size.height));

        UrbanGSView *view = [[UrbanGSView alloc] initWithFrame:bounds];
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        view.wantsLayer = YES;
        view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;

        // Create the CAMetalLayer here, on the main thread. PCSX2's VKSwapChain uses surface_handle directly; doing
        // this in CocoaTools::CreateMetalLayer from the GS thread would need a synchronous main-queue hop.
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.contentsScale = [[[view window] screen] backingScaleFactor];
        view.layer = layer;
        surface = (__bridge void *)layer;

        [content addSubview:view positioned:NSWindowAbove relativeTo:nil];
        return (__bridge void *)view;
    }

    void HostTick(void *childView, uint32_t &width, uint32_t &height)
    {
        UrbanGSView *view = (__bridge UrbanGSView *)childView;
        NSView *content = view.superview;
        if (!content)
            return;
        const NSRect bounds = [content bounds];
        width = static_cast<uint32_t>(std::max<CGFloat>(1.0, bounds.size.width));
        height = static_cast<uint32_t>(std::max<CGFloat>(1.0, bounds.size.height));
        if (!NSEqualRects(view.frame, bounds))
            [view setFrame:bounds];
    }

    void SetVisible(void *childView, bool visible)
    {
        UrbanGSView *view = (__bridge UrbanGSView *)childView;
        if (view)
            [view setHidden:visible ? NO : YES];
    }
}
