#include "Game/FPSController.h"

#include <GLFW/glfw3.h>
#include <algorithm>

namespace GeoFPS
{
// ────────────────────────────────────────────────────────────────────────────
//  FPSController — keyboard movement + mouse-look → CameraCommandFrame
//
//  Design intent (read this before changing anything below):
//
//    Movement (WASD/QE/Shift) is *velocity*-based: distance per frame =
//    speed × frameDelta.  Frame-time variance therefore self-corrects —
//    long frames cover proportionally more ground.
//
//    Mouse-look is *event*-based: the OS hands us a per-frame integer pixel
//    delta which we multiply by sensitivity to get degrees of rotation.
//    With Application::Run()'s 60 Hz frame pacer the inter-poll interval is
//    locked at 16.67 ms ± 1 ms, so the cursor delta IS frame-time-normalised
//    by construction; no per-frame scaling factor is required.
//
//    No smoothing filter is applied here.  Earlier versions ran a 4-tap
//    moving average to mask integer-pixel quantisation noise at slow mouse
//    speeds — but that introduced cross-axis bleed: 1-3 frames of horizontal
//    motion would leak forward into a subsequent purely-vertical sweep,
//    producing visible "yaw drift while pitching" the user reads as the
//    world translating sideways.  The right place to defeat low-DPI
//    quantisation is the OS mouse driver / hardware DPI setting; doing it
//    in application-side filtering creates more problems than it solves.
//
//    The hard look-delta cap remains as protection against focus-regain
//    spikes (where a captured-cursor toggle can deliver a multi-frame burst
//    of motion in a single frame).  It does NOT engage in normal play.
// ────────────────────────────────────────────────────────────────────────────

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

    // ── Keyboard movement ───────────────────────────────────────────────────
    const float frameDelta = std::clamp(deltaTime, 0.0f, 0.05f);
    float speed = m_MoveSpeed;
    if (m_Window->IsKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
        m_Window->IsKeyPressed(GLFW_KEY_RIGHT_SHIFT))
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
        command.localMoveAxes      = movementAxes;
        command.moveDistanceMeters = speed * frameDelta;
    }

    // ── Mouse look ──────────────────────────────────────────────────────────
    // When mouse-look is disabled (e.g. menu open) we drop any accumulated
    // cursor delta so it can't snap the camera the moment the user
    // re-engages.
    if (!m_Enabled)
    {
        ResetMouseState();
        return command;
    }

    // First-frame priming: prevents the first poll after capture/focus from
    // delivering a giant delta computed against an uninitialised baseline.
    if (m_FirstMouse)
    {
        m_Window->ResetCursorDelta();
        m_FirstMouse = false;
    }

    const glm::dvec2 cursorDelta = m_Window->ConsumeCursorDelta();

    // Raw integer-pixel delta → degrees of rotation.
    // X (horizontal mouse) drives yaw; Y (vertical mouse, screen-down
    // is positive Y in window coords) drives pitch — negated because the
    // FPS convention is "mouse up = look up = pitch +".
    constexpr float kMaxLookDeltaDegrees = 8.0f; // focus-regain spike guard
    float deltaX = static_cast<float>( cursorDelta.x) * m_MouseSensitivity;
    float deltaY = static_cast<float>(-cursorDelta.y) * m_MouseSensitivity;
    deltaX = std::clamp(deltaX, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);
    deltaY = std::clamp(deltaY, -kMaxLookDeltaDegrees, kMaxLookDeltaDegrees);

    command.lookDeltaDegrees = {deltaX, deltaY};
    return command;
}
} // namespace GeoFPS
