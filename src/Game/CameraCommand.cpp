#include "Game/CameraCommand.h"

#include <algorithm>
#include <cmath>

namespace GeoFPS
{
namespace
{
constexpr float kSnapSpeed = 12.0f;

[[nodiscard]] bool HasLookDelta(const glm::vec2& delta)
{
    return std::abs(delta.x) > 0.0001f || std::abs(delta.y) > 0.0001f;
}

[[nodiscard]] glm::vec3 CameraLocalMoveToWorld(const Camera& camera, const glm::vec3& localAxes)
{
    glm::vec3 forward = camera.GetForward();
    glm::vec3 forwardFlat(forward.x, 0.0f, forward.z);
    if (glm::dot(forwardFlat, forwardFlat) <= 1e-8f)
    {
        forwardFlat = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        forwardFlat = glm::normalize(forwardFlat);
    }

    const glm::vec3 right = camera.GetRight();
    glm::vec3 movement = (right * localAxes.x) +
                         (glm::vec3(0.0f, 1.0f, 0.0f) * localAxes.y) +
                         (forwardFlat * localAxes.z);
    if (glm::dot(movement, movement) > 1e-8f)
    {
        movement = glm::normalize(movement);
    }
    return movement;
}
} // namespace

void CameraCommandFrame::Clear()
{
    *this = CameraCommandFrame {};
}

glm::vec2 ApplyCameraCommandFrame(Camera& camera,
                                  CameraCommandFrame& command,
                                  CameraSnapState& snapState,
                                  float deltaTime)
{
    glm::vec2 appliedLookDelta{0.0f};

    if (command.hasTeleport)
    {
        camera.SetPosition(command.teleportPosition);
    }

    if (command.hasSnapTarget)
    {
        snapState.targetYaw = command.snapTargetYaw;
        snapState.targetPitch = std::clamp(command.snapTargetPitch, -89.0f, 89.0f);
        snapState.active = true;
    }
    else if (command.cancelSnap)
    {
        snapState.active = false;
    }

    const bool snapStartedThisFrame = command.hasSnapTarget;
    if (!snapStartedThisFrame && HasLookDelta(command.lookDeltaDegrees))
    {
        snapState.active = false;
        camera.SetYawPitch(camera.GetYaw() + command.lookDeltaDegrees.x,
                           camera.GetPitch() + command.lookDeltaDegrees.y);
        appliedLookDelta = command.lookDeltaDegrees;
    }

    if (snapState.active)
    {
        const float alpha = std::clamp(kSnapSpeed * deltaTime, 0.0f, 1.0f);
        float dyaw = snapState.targetYaw - camera.GetYaw();
        while (dyaw > 180.0f) dyaw -= 360.0f;
        while (dyaw < -180.0f) dyaw += 360.0f;

        const float newYaw = camera.GetYaw() + dyaw * alpha;
        const float newPitch = camera.GetPitch() + (snapState.targetPitch - camera.GetPitch()) * alpha;
        camera.SetYawPitch(newYaw, newPitch);

        if (std::abs(dyaw) < 0.1f && std::abs(snapState.targetPitch - newPitch) < 0.1f)
        {
            camera.SetYawPitch(snapState.targetYaw, snapState.targetPitch);
            snapState.active = false;
        }
    }

    if (command.moveDistanceMeters > 0.0f && glm::dot(command.localMoveAxes, command.localMoveAxes) > 1e-8f)
    {
        camera.Move(CameraLocalMoveToWorld(camera, command.localMoveAxes) * command.moveDistanceMeters);
    }

    command.Clear();
    return appliedLookDelta;
}
} // namespace GeoFPS
