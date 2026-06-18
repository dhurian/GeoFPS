#include "Core/FramePacer.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <thread>

namespace GeoFPS
{
namespace
{
double NowMs()
{
    return glfwGetTime() * 1000.0;
}
} // namespace

void FramePacer::WaitForNextFrame()
{
    constexpr double kTargetFrameMs = 1000.0 / 60.0; // 16.67 ms (60 Hz)
    constexpr double kBusyWaitWindowMs = 4.0;        // tolerate sleep overshoot up to this
    constexpr double kReanchorIfBehindMs = 50.0;

    if (m_NextTargetMs == 0.0)
    {
        m_NextTargetMs = NowMs(); // first iteration — anchor schedule
    }
    m_NextTargetMs += kTargetFrameMs;

    const double nowMs = NowMs();
    if (nowMs > m_NextTargetMs + kReanchorIfBehindMs)
    {
        // Long stall (debugger, OS hiccup) — give up catching up.
        m_NextTargetMs = nowMs;
        return;
    }

    const double remainingMs = m_NextTargetMs - nowMs;
    if (remainingMs > kBusyWaitWindowMs)
    {
        // Sleep most of the wait; macOS can oversleep by 2-3 ms, so leave the
        // busy-wait window to absorb that.
        std::this_thread::sleep_for(std::chrono::microseconds(
            static_cast<long>((remainingMs - kBusyWaitWindowMs) * 1000.0)));
    }
    while (NowMs() < m_NextTargetMs) { /* busy-wait the last 0–4 ms */ }
}
} // namespace GeoFPS
