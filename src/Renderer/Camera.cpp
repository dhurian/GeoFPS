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

glm::mat4 Camera::GetProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_FieldOfViewDegrees), m_AspectRatio, m_NearClip, m_FarClip);
}
} // namespace GeoFPS
