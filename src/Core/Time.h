#pragma once

#include <chrono>

namespace GeoFPS
{
// Monotonic wall-clock milliseconds from a single steady_clock source, shared by
// every subsystem (frame pacing, perf logging, the GPU upload budget).
//
// This replaces the five local NowMs() copies that used to disagree on their
// clock: some read std::chrono::steady_clock, others glfwGetTime().  Because
// those two clocks have different epochs, the terrain upload budget — which
// subtracts a frame-start timestamp taken in one file from a "now" taken in
// another — was computing a garbage elapsed time and collapsing to ~1 chunk per
// frame.  One definition keeps all cross-subsystem time comparisons in the same
// epoch.
inline double NowMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}
} // namespace GeoFPS
