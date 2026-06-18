#pragma once

namespace GeoFPS
{
// Paces the render loop to a fixed 60 Hz beat using an ABSOLUTE schedule.
//
// The Apple OpenGL → Metal swap chain is triple-buffered with vsync at the
// display's adaptive (ProMotion) rate, which makes the interval between
// glfwPollEvents calls irregular — so cursor delta accumulates over uneven
// windows and mouse-look stutters.  Pacing the loop ourselves to 16.67 ms
// gives a steady poll cadence.
//
// The schedule is absolute (the next target is the previous target + 16.67 ms,
// not "now + 16.67 ms"): if one sleep oversleeps, the following target is still
// on the original beat, so the loop self-corrects within a frame instead of
// drifting.  Call WaitForNextFrame() once per loop iteration after the frame's
// work is done.
class FramePacer
{
  public:
    // Block until the next 60 Hz beat (sleep most of the wait, busy-wait the
    // last few ms to land within ~50 µs of target).  Re-anchors if the loop
    // has fallen hopelessly behind (e.g. paused in a debugger).
    void WaitForNextFrame();

    // The current absolute target, in glfwGetTime-milliseconds, for diagnostics
    // (the performance log records the per-frame schedule slip).
    [[nodiscard]] double nextTargetMs() const { return m_NextTargetMs; }

  private:
    double m_NextTargetMs {0.0};
};
} // namespace GeoFPS
