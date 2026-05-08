#include "Core/Window.h"

#include <GLFW/glfw3.h>
#include <iostream>

#ifdef __APPLE__
#include "Core/RawMouseMacOS.h"
#endif

namespace GeoFPS
{
namespace
{
void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->OnFramebufferResized(width, height);
    }
}

void CursorPositionCallback(GLFWwindow* window, double x, double y)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self == nullptr)
    {
        return;
    }

    self->OnCursorPosition(x, y);
}

void WindowFocusCallback(GLFWwindow* window, int focused)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->OnWindowFocus(focused == GLFW_TRUE);
    }
}

void CharCallback(GLFWwindow* window, unsigned int codepoint)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr)
    {
        self->OnChar(codepoint);
    }
}
} // namespace

bool Window::Create(int width, int height, const char* title)
{
    if (!glfwInit())
    {
        std::cerr << "glfwInit failed\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    m_Handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (m_Handle == nullptr)
    {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_Handle);
    SetSwapInterval(m_SwapInterval);
    glfwSetWindowUserPointer(m_Handle, this);
    glfwSetFramebufferSizeCallback(m_Handle, FramebufferSizeCallback);
    glfwSetCursorPosCallback(m_Handle, CursorPositionCallback);
    glfwSetWindowFocusCallback(m_Handle, WindowFocusCallback);
    // Install char callback before ImGui's SetupImGui() runs so that ImGui
    // (which chains callbacks in ImGui_ImplGlfw_InitForOpenGL) will forward
    // char events back to us via PrevUserCallbackChar.
    glfwSetCharCallback(m_Handle, CharCallback);
    glfwGetFramebufferSize(m_Handle, &m_Width, &m_Height);
    m_LastFrameTime = glfwGetTime();
    return true;
}

void Window::Destroy()
{
    if (m_Handle != nullptr)
    {
        glfwDestroyWindow(m_Handle);
        m_Handle = nullptr;
    }
    glfwTerminate();
}

bool Window::ShouldClose() const
{
    return m_Handle == nullptr || glfwWindowShouldClose(m_Handle) != 0;
}

float Window::PollEventsAndGetDeltaTime()
{
    // Clear per-frame char buffer BEFORE polling so OnChar accumulates only
    // the events that arrived since the previous PollEventsAndGetDeltaTime.
    m_CharsThisFrame.clear();
    glfwPollEvents();
    // Sample cursor position AFTER glfwPollEvents has drained the OS event
    // queue.  This is the frame-cadenced cursor-delta path that replaces the
    // callback-based accumulation — see OnCursorPosition for context.
    PollCursorPositionDelta();
    const double now = glfwGetTime();
    const float delta = static_cast<float>(now - m_LastFrameTime);
    m_LastFrameTime = now;
    return delta;
}

void Window::PollCursorPositionDelta()
{
    // Reset diagnostic each frame — set below if we sampled.
    m_LastRawMouseDelta = {0.0, 0.0};

    if (m_Handle == nullptr)
    {
        return;
    }

    // Only fold raw motion into the accumulator while the cursor is captured
    // (FPS look mode).  When the cursor is free we want the OS-cursor cadence
    // (so UI windows behave naturally) and we don't read camera deltas anyway.
    if (!m_CursorCaptured)
    {
        // Keep m_LastCursor* in sync so the first frame after capture re-
        // engages doesn't produce a phantom delta.
        glfwGetCursorPos(m_Handle, &m_LastCursorX, &m_LastCursorY);
        m_HasCursorPosition = true;
        // Drain any pending raw delta so it can't snap the camera the moment
        // we re-capture.  CGGetLastMouseDelta returns and resets, so a single
        // discard call is enough.
#ifdef __APPLE__
        double drainX = 0.0, drainY = 0.0;
        TryGetRawMouseDeltaMacOS(drainX, drainY);
        (void)drainX;
        (void)drainY;
#endif
        return;
    }

#ifdef __APPLE__
    // Pre-NSEvent HID delta.  This is the path that actually fixes the
    // "horizontal motion arrives in bursts" problem: NSEvent coalescing and
    // GLFW's virtual-cursor pipeline both sit *above* this layer.
    double dx = 0.0;
    double dy = 0.0;
    if (TryGetRawMouseDeltaMacOS(dx, dy))
    {
        m_AccumulatedCursorDelta.x += dx;
        m_AccumulatedCursorDelta.y += dy;
        m_LastRawMouseDelta = {dx, dy};
        m_HasCursorPosition = true;
        return;
    }
#endif

    // Fallback (non-Apple, or if the macOS path returned false): poll the
    // virtual cursor position via GLFW.  This is still better than the
    // callback-based accumulation we used previously because it reads the
    // latest position regardless of how many cursor-pos events were
    // delivered, but it remains subject to whatever cadence the OS feeds
    // GLFW's virtual cursor.
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(m_Handle, &cx, &cy);
    if (!m_HasCursorPosition)
    {
        m_LastCursorX = cx;
        m_LastCursorY = cy;
        m_HasCursorPosition = true;
        return;
    }
    const double pdx = cx - m_LastCursorX;
    const double pdy = cy - m_LastCursorY;
    m_AccumulatedCursorDelta.x += pdx;
    m_AccumulatedCursorDelta.y += pdy;
    m_LastRawMouseDelta = {pdx, pdy};
    m_LastCursorX = cx;
    m_LastCursorY = cy;
}

bool Window::WasCharTyped(unsigned int codepoint) const
{
    for (unsigned int c : m_CharsThisFrame)
        if (c == codepoint) return true;
    return false;
}

void Window::OnChar(unsigned int codepoint)
{
    m_CharsThisFrame.push_back(codepoint);
}

void Window::SwapBuffers()
{
    glfwSwapBuffers(m_Handle);
}

void Window::SetSwapInterval(int interval)
{
    m_SwapInterval = interval;
    if (m_Handle != nullptr)
    {
        glfwSwapInterval(m_SwapInterval);
    }
}

bool Window::IsKeyPressed(int key) const
{
    return glfwGetKey(m_Handle, key) == GLFW_PRESS;
}

void Window::SetCursorCaptured(bool captured)
{
    if (m_CursorCaptured == captured && !m_CursorCaptureRefreshNeeded)
    {
        return;
    }

    m_CursorCaptured = captured;
    ApplyCursorCapture(true);
}

void Window::RefreshCursorCapture()
{
    if (m_CursorCaptured)
    {
        const int currentMode = glfwGetInputMode(m_Handle, GLFW_CURSOR);
        if (currentMode != GLFW_CURSOR_DISABLED)
        {
            m_CursorCaptureRefreshNeeded = true;
        }
    }

    if (m_CursorCaptureRefreshNeeded)
    {
        ApplyCursorCapture(true);
    }
}

void Window::ApplyCursorCapture(bool resetDelta)
{
    glfwSetInputMode(m_Handle, GLFW_CURSOR, m_CursorCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(m_Handle, GLFW_RAW_MOUSE_MOTION, m_CursorCaptured ? GLFW_TRUE : GLFW_FALSE);
    }
    m_CursorCaptureRefreshNeeded = false;
    if (resetDelta)
    {
        ResetCursorDelta();
    }
}

glm::dvec2 Window::ConsumeCursorDelta()
{
    const glm::dvec2 delta = m_AccumulatedCursorDelta;
    m_AccumulatedCursorDelta = {0.0, 0.0};
    return delta;
}

void Window::ResetCursorDelta()
{
    m_AccumulatedCursorDelta = {0.0, 0.0};
    if (m_Handle == nullptr)
    {
        m_HasCursorPosition = false;
        m_LastCursorX = 0.0;
        m_LastCursorY = 0.0;
        return;
    }

    glfwGetCursorPos(m_Handle, &m_LastCursorX, &m_LastCursorY);
    m_HasCursorPosition = true;
}

void Window::OnFramebufferResized(int width, int height)
{
    m_Width = width;
    m_Height = height;
}

void Window::OnCursorPosition(double /*x*/, double /*y*/)
{
    // Intentionally a no-op for delta tracking.
    //
    // We previously accumulated the cursor delta from this callback, but on
    // macOS the GLFW cursor-pos callback fires at the cadence of NSEvent
    // delivery, which can batch motion across many frames before flushing —
    // producing a "static for N frames, then a big jump" pattern in the
    // camera even when the user is moving the mouse smoothly.
    //
    // The delta is now sampled in PollCursorPositionDelta() once per frame
    // (called from PollEventsAndGetDeltaTime after glfwPollEvents).  That
    // path uses glfwGetCursorPos(), which always returns the latest virtual
    // cursor position regardless of how many events were coalesced into a
    // single delivery, giving smooth per-frame deltas.
    //
    // The callback registration itself is kept so ImGui's GLFW backend can
    // chain through it (we install ours before ImGui_ImplGlfw_InitForOpenGL,
    // which preserves the chain).
}

void Window::OnWindowFocus(bool focused)
{
    if (focused && m_CursorCaptured)
    {
        m_CursorCaptureRefreshNeeded = true;
    }
}
} // namespace GeoFPS
