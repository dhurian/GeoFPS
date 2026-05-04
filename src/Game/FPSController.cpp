#include "Game/FPSController.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace GeoFPS
{
void FPSController::AttachWindow(Window* window)
{
    m_Window = window;
}

void FPSController::SetEnabled(bool enabled)
{
    if (m_Enabled != enabled)
    {
        m_FirstMouse = true;
    }
    m_Enabled = enabled;
}

void FPSController::SetMoveSpeed(float moveSpeed)
{
    m_MoveSpeed = std::clamp(moveSpeed, 0.5f, 3000.0f);
}

void FPSController::SetSprintMultiplier(float sprintMultiplier)
{
    m_SprintMultiplier = std::clamp(sprintMultiplier, 1.0f, 20.0f);
}

void FPSController::ResetMouseState()
{
    m_FirstMouse = true;
    if (m_Window != nullptr)
    {
        m_Window->ResetCursorDelta();
    }
}

CameraCommandFrame FPSController::BuildFrameCommand(float deltaTime)
{
    CameraCommandFrame command;
    if (m_Window == nullptr)
    {
        return command;
    }

    const float frameDelta = std::clamp(deltaTime, 0.0f, 0.05f);
    float speed = m_MoveSpeed;
    if (m_Window->IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || m_Window->IsKeyPressed(GLFW_KEY_RIGHT_SHIFT))
    {
        speed *= m_SprintMultiplier;
    }
    m_CurrentSpeed = speed;

    glm::vec3 movementAxes(0.0f);
    if (m_Window->IsKeyPressed(GLFW_KEY_W)) movementAxes.z += 1.0f;
    if (m_Window->IsKeyPressed(GLFW_KEY_S)) movementAxes.z -= 1.0f;
    if (m_Window->IsKeyPressed(GLFW_KEY_A)) movementAxes.x -= 1.0f;
    if (m_Window->IsKeyPressed(GLFW_KEY_D)) movementAxes.x += 1.0f;
    if (m_Window->IsKeyPressed(GLFW_KEY_Q)) movementAxes.y -= 1.0f;
    if (m_Window->IsKeyPressed(GLFW_KEY_E)) movementAxes.y += 1.0f;

    if (glm::length(movementAxes) > 0.0f)
    {
        command.localMoveAxes = movementAxes;
        command.moveDistanceMeters = speed * frameDelta;
    }

    if (!m_Enabled)
    {
        ResetMouseState();
        return command;
    }

    if (m_FirstMouse)
    {
        m_Window->ResetCursorDelta();
        m_FirstMouse = false;
    }

    const glm::dvec2 cursorDelta = m_Window->ConsumeCursorDelta();

    // Hard cap: prevents a single extreme-stall frame from spinning the camera
    // more than 8 degrees in any direction, while leaving normal movement fully
    // responsive at all frame rates.
    constexpr float kMaxLookDeltaDegrees = 8.0f;

    float deltaX = static_cast<float>(cursorDelta.x) * m_MouseSensitivity;
    float deltaY = static_cast<float>(-cursorDelta.y) * m_MouseSensitivity;

    deltaX = std::clamp(deltaX, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);
    deltaY = std::clamp(deltaY, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);

    command.lookDeltaDegrees = {deltaX, deltaY};
    return command;
}
} // namespace GeoFPS
