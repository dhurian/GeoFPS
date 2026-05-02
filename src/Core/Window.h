#pragma once

#include <glm/vec2.hpp>

struct GLFWwindow;

namespace GeoFPS
{
class Window
{
  public:
    bool Create(int width, int height, const char* title);
    void Destroy();
    bool ShouldClose() const;
    float PollEventsAndGetDeltaTime();
    void SwapBuffers();
    void SetSwapInterval(int interval);
    bool IsKeyPressed(int key) const;
    void SetCursorCaptured(bool captured);
    void RefreshCursorCapture();
    glm::dvec2 ConsumeCursorDelta();
    void ResetCursorDelta();

    [[nodiscard]] int GetWidth() const { return m_Width; }
    [[nodiscard]] int GetHeight() const { return m_Height; }
    [[nodiscard]] GLFWwindow* GetNativeHandle() const { return m_Handle; }

    void OnFramebufferResized(int width, int height);
    void OnCursorPosition(double x, double y);
    void OnWindowFocus(bool focused);

  private:
    void ApplyCursorCapture(bool resetDelta);

    GLFWwindow* m_Handle {nullptr};
    int m_Width {0};
    int m_Height {0};
    double m_LastFrameTime {0.0};
    int m_SwapInterval {1};
    bool m_CursorCaptured {false};
    bool m_CursorCaptureRefreshNeeded {false};
    bool m_HasCursorPosition {false};
    double m_LastCursorX {0.0};
    double m_LastCursorY {0.0};
    glm::dvec2 m_AccumulatedCursorDelta {0.0, 0.0};
};
} // namespace GeoFPS
