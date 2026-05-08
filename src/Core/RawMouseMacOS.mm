#include "Core/RawMouseMacOS.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace GeoFPS
{
namespace
{
// Sub-pixel mouse delta accumulator, populated by the local NSEvent monitor.
//
// CGGetLastMouseDelta (the previous implementation) returns integer pixels,
// which at slow mouse speeds produces visible "stair-step" rotation: a true
// motion of 1.5 px/frame is reported as alternating 1, 2, 1, 2, …  An
// NSEvent's deltaX/deltaY is CGFloat and carries the OS's full sub-pixel
// precision (especially for trackpads and high-resolution mice), so summing
// the deltaX of every NSEventTypeMouseMoved /Dragged event we observe gives
// us frame-cadenced sub-pixel deltas that no longer have the integer
// quantization noise.
//
// We use addLocalMonitorForEventsMatchingMask which OBSERVES events without
// removing them from the queue — so GLFW's normal cursor-pos and click event
// processing is unaffected.
double      g_AccumulatedDeltaX = 0.0;
double      g_AccumulatedDeltaY = 0.0;
id          g_Monitor           = nil;

void EnsureMonitor()
{
    if (g_Monitor != nil) { return; }
    NSEventMask mask =
        NSEventMaskMouseMoved      |
        NSEventMaskLeftMouseDragged|
        NSEventMaskRightMouseDragged|
        NSEventMaskOtherMouseDragged;
    g_Monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                       handler:^NSEvent*(NSEvent* ev) {
        g_AccumulatedDeltaX += [ev deltaX];
        g_AccumulatedDeltaY += [ev deltaY];
        return ev; // pass through so GLFW/ImGui still see it
    }];
}
} // namespace

bool TryGetRawMouseDeltaMacOS(double& outDeltaX, double& outDeltaY)
{
    EnsureMonitor();
    outDeltaX = g_AccumulatedDeltaX;
    outDeltaY = g_AccumulatedDeltaY;
    g_AccumulatedDeltaX = 0.0;
    g_AccumulatedDeltaY = 0.0;
    return true;
}
} // namespace GeoFPS
