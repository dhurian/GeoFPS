#pragma once

#include <array>

namespace GeoFPS
{
// Double-buffered OpenGL GL_TIME_ELAPSED query wrapper.  Measures wall-clock
// GPU time for a frame's draw work without stalling the pipeline: a query
// issued on frame N is read back a frame or two later, once its result is
// ready, so the CPU never blocks waiting on the GPU.
//
// Per-frame usage:
//   Poll();   // lazy-inits queries on first call; harvests any ready result
//   Begin();  // glBeginQuery around the frame's GPU work
//   ...draw work...
//   End();    // glEndQuery
//
// Call Shutdown() once before GL context teardown to release the query
// objects.  Available() reports whether GPU timing is supported at all
// (requires GL 3.3); LastFrameMs() is the most recently harvested timing.
class GpuFrameTimer
{
  public:
    void Poll();
    void Begin();
    void End();
    void Shutdown();

    [[nodiscard]] bool Available() const { return m_Available; }
    [[nodiscard]] float LastFrameMs() const { return m_LastFrameMs; }

  private:
    std::array<unsigned int, 2> m_Queries {};
    std::array<bool, 2> m_Pending {};
    int   m_WriteIndex {0};
    bool  m_Initialized {false};
    bool  m_Active {false};
    bool  m_Available {false};
    float m_LastFrameMs {0.0f};
};
} // namespace GeoFPS
