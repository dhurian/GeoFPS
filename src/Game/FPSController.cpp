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
    m_SmoothedLookX = 0.0f;
    m_SmoothedLookY = 0.0f;
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

    float rawDeltaX = static_cast<float>(cursorDelta.x) * m_MouseSensitivity;
    float rawDeltaY = static_cast<float>(-cursorDelta.y) * m_MouseSensitivity;

    // Hard-cap the per-frame look delta so that long frame stalls (e.g. large
    // Nepal tile uploads) can never produce a multi-degree camera lurch even
    // before smoothing kicks in. 8 degrees is imperceptible at normal frame
    // rates but prevents violent jumps from 200-500 ms stall frames.
    constexpr float kMaxLookDeltaDegrees = 8.0f;
    rawDeltaX = std::clamp(rawDeltaX, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);
    rawDeltaY = std::clamp(rawDeltaY, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);

    // Exponential smoothing so even the capped delta eases in rather than
    // snapping. Cap the effective delta time at 1/30 s so a stall frame does
    // not inflate the smoothing alpha and cause a step change.
    constexpr float kMaxSmoothingDelta    = 1.0f / 30.0f;
    constexpr float kMouseSmoothingResponse = 20.0f;
    const float smoothingDelta = std::min(frameDelta, kMaxSmoothingDelta);
    const float smoothingAlpha = std::clamp(1.0f - std::exp(-kMouseSmoothingResponse * smoothingDelta), 0.0f, 1.0f);
    m_SmoothedLookX += (rawDeltaX - m_SmoothedLookX) * smoothingAlpha;
    m_SmoothedLookY += (rawDeltaY - m_SmoothedLookY) * smoothingAlpha;

    command.lookDeltaDegrees = {m_SmoothedLookX, m_SmoothedLookY};
    return command;
}
} // namespace GeoFPS
