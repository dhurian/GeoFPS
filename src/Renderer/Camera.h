#pragma once

#include <glm/glm.hpp>

namespace GeoFPS
{
class Camera
{
  public:
    void SetAspectRatio(float aspectRatio);
    void SetPosition(const glm::vec3& position);
    void SetYawPitch(float yawDegrees, float pitchDegrees);
    void SetNearClip(float nearClip);
    void SetFarClip(float farClip);

    void Move(const glm::vec3& delta);

    [[nodiscard]] glm::vec3 GetPosition() const { return m_Position; }
    [[nodiscard]] glm::vec3 GetForward() const;
    [[nodiscard]] glm::vec3 GetRight() const;
    [[nodiscard]] glm::vec3 GetUp() const;
    [[nodiscard]] glm::mat4 GetViewMatrix() const;
    [[nodiscard]] glm::mat4 GetProjectionMatrix() const;

    [[nodiscard]] float GetYaw() const { return m_YawDegrees; }
    [[nodiscard]] float GetPitch() const { return m_PitchDegrees; }
    [[nodiscard]] float GetNearClip() const { return m_NearClip; }
    [[nodiscard]] float GetFarClip() const { return m_FarClip; }

  private:
    glm::vec3 m_Position {0.0f, 2.0f, 5.0f};
    float m_YawDegrees {-90.0f};
    float m_PitchDegrees {-12.0f};
    float m_FieldOfViewDegrees {60.0f};
    float m_AspectRatio {16.0f / 9.0f};
    float m_NearClip {1.0f};
    float m_FarClip {50000.0f};
};
} // namespace GeoFPS
