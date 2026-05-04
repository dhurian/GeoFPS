#pragma once

#include "Core/Window.h"
#include "Game/CameraCommand.h"

#include <glm/glm.hpp>

namespace GeoFPS
{
class FPSController
{
  public:
    void AttachWindow(Window* window);
    void SetEnabled(bool enabled);
    [[nodiscard]] CameraCommandFrame BuildFrameCommand(float deltaTime);
    void SetMoveSpeed(float moveSpeed);
    void SetSprintMultiplier(float sprintMultiplier);
    void ResetMouseState();

    [[nodiscard]] float GetMoveSpeed() const { return m_MoveSpeed; }
    [[nodiscard]] float GetSprintMultiplier() const { return m_SprintMultiplier; }
    [[nodiscard]] float GetCurrentSpeed() const { return m_CurrentSpeed; }
    [[nodiscard]] bool IsEnabled() const { return m_Enabled; }

  private:
    Window* m_Window {nullptr};
    bool m_Enabled {true};
    bool m_FirstMouse {true};
    float m_MoveSpeed {12.0f};
    float m_SprintMultiplier {2.0f};
    float m_CurrentSpeed {12.0f};
    float m_MouseSensitivity {0.08f};
};
} // namespace GeoFPS
