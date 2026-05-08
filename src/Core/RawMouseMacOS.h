#pragma once

namespace GeoFPS
{
// macOS-specific raw mouse delta source.
//
// On macOS, GLFW's "raw" mouse motion is still routed through NSEvent, which
// is subject to OS-level coalescing.  In practice this produces a "static for
// many frames, then a big jump" pattern in mouse-look that survives even when
// you poll glfwGetCursorPos every frame, because the underlying virtual
// cursor position only updates when an NSEvent is delivered.
//
// CoreGraphics exposes an HID-level delta accumulator that is updated by the
// kernel from raw HID device input, before NSEvent / pointer acceleration /
// cursor clamping happen.  Reading it once per frame gives a smooth,
// frame-cadenced delta.
//
// Returns true if a delta was read.  On non-macOS builds the function is a
// no-op that returns false; callers fall back to glfwGetCursorPos.
bool TryGetRawMouseDeltaMacOS(double& outDeltaX, double& outDeltaY);
} // namespace GeoFPS
