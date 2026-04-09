#pragma once

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
    bool IsKeyPressed(int key) const;
    void SetCursorCaptured(bool captured);

    [[nodiscard]] int GetWidth() const { return m_Width; }
    [[nodiscard]] int GetHeight() const { return m_Height; }
    [[nodiscard]] GLFWwindow* GetNativeHandle() const { return m_Handle; }

    void OnFramebufferResized(int width, int height);

  private:
    GLFWwindow* m_Handle {nullptr};
    int m_Width {0};
    int m_Height {0};
    double m_LastFrameTime {0.0};
};
} // namespace GeoFPS
