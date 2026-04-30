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
    glfwSwapInterval(1);
    glfwSetWindowUserPointer(m_Handle, this);
    glfwSetFramebufferSizeCallback(m_Handle, FramebufferSizeCallback);
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

bool Window::IsKeyPressed(int key) const
{
    return glfwGetKey(m_Handle, key) == GLFW_PRESS;
}

void Window::SetCursorCaptured(bool captured)
{
    glfwSetInputMode(m_Handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
    {
        glfwSetInputMode(m_Handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
}

void Window::OnFramebufferResized(int width, int height)
{
    m_Width = width;
    m_Height = height;
}
} // namespace GeoFPS
