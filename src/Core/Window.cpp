#include "Core/Window.h"

#include <GLFW/glfw3.h>
#include <iostream>

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
    glfwPollEvents();
    const double now = glfwGetTime();
    const float delta = static_cast<float>(now - m_LastFrameTime);
    m_LastFrameTime = now;
    return delta;
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

void Window::OnCursorPosition(double x, double y)
{
    if (!m_HasCursorPosition)
    {
        m_LastCursorX = x;
        m_LastCursorY = y;
        m_HasCursorPosition = true;
        return;
    }

    m_AccumulatedCursorDelta.x += x - m_LastCursorX;
    m_AccumulatedCursorDelta.y += y - m_LastCursorY;
    m_LastCursorX = x;
    m_LastCursorY = y;
}

void Window::OnWindowFocus(bool focused)
{
    if (focused && m_CursorCaptured)
    {
        m_CursorCaptureRefreshNeeded = true;
    }
}
} // namespace GeoFPS
