#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

namespace GeoFPS
{
class Camera;

class FPSController
{
  public:
    void AttachWindow(GLFWwindow* window);
    void AttachCamera(Camera* camera);
    void SetEnabled(bool enabled);
    void Update(float deltaTime);
    void SetMoveSpeed(float moveSpeed);
    void SetSprintMultiplier(float sprintMultiplier);
    void ResetMouseState();

    [[nodiscard]] float GetMoveSpeed() const { return m_MoveSpeed; }
    [[nodiscard]] float GetSprintMultiplier() const { return m_SprintMultiplier; }
    [[nodiscard]] float GetCurrentSpeed() const { return m_CurrentSpeed; }
    [[nodiscard]] bool IsEnabled() const { return m_Enabled; }

  private:
    GLFWwindow* m_Window {nullptr};
    Camera* m_Camera {nullptr};
    bool m_Enabled {true};
    bool m_FirstMouse {true};
    double m_LastMouseX {0.0};
    double m_LastMouseY {0.0};
    float m_SmoothedMouseDeltaX {0.0f};
    float m_SmoothedMouseDeltaY {0.0f};
    float m_MoveSpeed {12.0f};
    float m_SprintMultiplier {2.0f};
    float m_CurrentSpeed {12.0f};
    float m_MouseSensitivity {0.08f};
    float m_MouseSmoothingResponse {36.0f};
};
} // namespace GeoFPS
