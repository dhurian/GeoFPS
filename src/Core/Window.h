#pragma once

#include <glm/vec2.hpp>
#include <vector>

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
    // Last raw OS-level mouse delta this frame (in physical pixels).
    // Populated by PollCursorPositionDelta() from CGGetLastMouseDelta on
    // macOS, or from glfwGetCursorPos diff on other platforms.  Exposed so
    // the per-frame perf logger can record it independently of the
    // accumulated/consumed delta — letting us see whether jitter is in the
    // OS event delivery (raw delta) or in our own pipeline.
    [[nodiscard]] glm::dvec2 GetLastRawMouseDelta() const { return m_LastRawMouseDelta; }
    // Returns true if the given Unicode codepoint was typed this frame.
    // Uses the OS char callback so it is keyboard-layout-aware: on a
    // Scandinavian keyboard '+' lives at the GLFW_KEY_MINUS physical position,
    // but WasCharTyped('+') still returns true when that key is pressed.
    [[nodiscard]] bool WasCharTyped(unsigned int codepoint) const;
    // Returns true if ANY character typed this frame satisfies the predicate.
    // Lets callers test a whole class of characters (e.g. "any speed-increase
    // key") against the single source of truth for that class, instead of
    // OR-ing individual WasCharTyped() calls that can drift out of sync.
    [[nodiscard]] bool WasCharTypedMatching(bool (*predicate)(unsigned int)) const;
    void OnChar(unsigned int codepoint);

    [[nodiscard]] int GetWidth() const { return m_Width; }
    [[nodiscard]] int GetHeight() const { return m_Height; }
    [[nodiscard]] GLFWwindow* GetNativeHandle() const { return m_Handle; }

    void OnFramebufferResized(int width, int height);
    void OnCursorPosition(double x, double y);
    void OnWindowFocus(bool focused);

  private:
    void ApplyCursorCapture(bool resetDelta);
    // Polls the current cursor position with glfwGetCursorPos and folds the
    // delta-since-last-poll into m_AccumulatedCursorDelta.  Called once per
    // frame from PollEventsAndGetDeltaTime.  This is *frame-cadenced* and
    // robust against macOS NSEvent coalescing: even if many cursor-pos
    // events were batched into a single OS-level delivery, the virtual
    // cursor position is up-to-date and the per-frame delta picks up
    // everything that happened since the previous poll.
    void PollCursorPositionDelta();

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
    // What the OS handed us this frame (raw, pre-accumulator).  Cached for
    // diagnostics; not used for camera control.
    glm::dvec2 m_LastRawMouseDelta {0.0, 0.0};
    // Characters typed this frame (populated by the GLFW char callback,
    // cleared at the start of each PollEventsAndGetDeltaTime call).
    std::vector<unsigned int> m_CharsThisFrame;
};
} // namespace GeoFPS
