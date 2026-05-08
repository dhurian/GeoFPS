#include "Renderer/Camera.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace GeoFPS
{
void Camera::SetAspectRatio(float aspectRatio)
{
    m_AspectRatio = aspectRatio;
}

void Camera::SetPosition(const glm::vec3& position)
{
    m_Position = position;
}

void Camera::SetYawPitch(float yawDegrees, float pitchDegrees)
{
    m_YawDegrees = yawDegrees;
    m_PitchDegrees = glm::clamp(pitchDegrees, -89.0f, 89.0f);
}

void Camera::SetNearClip(float nearClip)
{
    m_NearClip = glm::clamp(nearClip, 0.1f, m_FarClip - 1.0f);
}

void Camera::SetFarClip(float farClip)
{
    m_FarClip = std::max(farClip, m_NearClip + 1.0f);
}

void Camera::Move(const glm::vec3& delta)
{
    m_Position += delta;
}

glm::vec3 Camera::GetForward() const
{
    const float yaw = glm::radians(m_YawDegrees);
    const float pitch = glm::radians(m_PitchDegrees);
    glm::vec3 forward;
    forward.x = std::cos(yaw) * std::cos(pitch);
    forward.y = std::sin(pitch);
    forward.z = std::sin(yaw) * std::cos(pitch);
    return glm::normalize(forward);
}

glm::vec3 Camera::GetRight() const
{
    return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera::GetUp() const
{
    return glm::normalize(glm::cross(GetRight(), GetForward()));
}

glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(m_Position, m_Position + GetForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetViewMatrixRotationOnly() const
{
    // Build the rotation-only view matrix from the forward direction *at the
    // origin*, never using m_Position.
    //
    // The naive implementation `glm::mat4(glm::mat3(GetViewMatrix()))` calls
    // glm::lookAt(m_Position, m_Position + GetForward(), up).  That looks
    // innocent, but lookAt's first internal step is `target - eye`, computed
    // in glm::vec3 (single-precision float).  At large camera distances from
    // world origin (e.g. ~6 M m in Nepal) the addition `m_Position + forward`
    // rounds to the float resolution at that magnitude — about 0.5 m — and
    // when the subtraction recovers the forward vector it has lost up to
    // 0.5 m / |forward_component|, i.e. up to ~50% of the actual change for
    // a small yaw/pitch step.  The visible effect is that the view matrix
    // *snaps* between discrete rotation states as the user mouses smoothly:
    // the "choppy mouse only in Nepal" / "smooth in Lisbon" mystery.
    //
    // By constructing lookAt with eye = (0,0,0) we keep all the inputs in
    // the unit-magnitude regime, where float precision is ~7 decimal digits
    // and rotation interpolates as smoothly as our yaw/pitch values do.
    return glm::lookAt(glm::vec3(0.0f),
                       GetForward(),
                       glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::GetProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_FieldOfViewDegrees), m_AspectRatio, m_NearClip, m_FarClip);
}
} // namespace GeoFPS
