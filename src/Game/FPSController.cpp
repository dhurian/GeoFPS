#include "Game/FPSController.h"

#include "Renderer/Camera.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace GeoFPS
{
void FPSController::AttachWindow(GLFWwindow* window)
{
    m_Window = window;
}

void FPSController::AttachCamera(Camera* camera)
{
    m_Camera = camera;
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
    m_SmoothedMouseDeltaX = 0.0f;
    m_SmoothedMouseDeltaY = 0.0f;
}

void FPSController::Update(float deltaTime)
{
    if (m_Window == nullptr || m_Camera == nullptr)
    {
        return;
    }

    const float frameDelta = std::clamp(deltaTime, 0.0f, 0.05f);
    float speed = m_MoveSpeed;
    if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
    {
        speed *= m_SprintMultiplier;
    }
    m_CurrentSpeed = speed;

    glm::vec3 movement(0.0f);
    const glm::vec3 forward = m_Camera->GetForward();
    glm::vec3 forwardFlat(forward.x, 0.0f, forward.z);
    if (glm::dot(forwardFlat, forwardFlat) <= 1e-8f)
    {
        forwardFlat = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else
    {
        forwardFlat = glm::normalize(forwardFlat);
    }
    const glm::vec3 right = m_Camera->GetRight();

    if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS)
    {
        movement += forwardFlat;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS)
    {
        movement -= forwardFlat;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS)
    {
        movement -= right;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS)
    {
        movement += right;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_Q) == GLFW_PRESS)
    {
        movement.y -= 1.0f;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_PRESS)
    {
        movement.y += 1.0f;
    }

    if (glm::length(movement) > 0.0f)
    {
        movement = glm::normalize(movement);
        m_Camera->Move(movement * speed * frameDelta);
    }

    if (!m_Enabled)
    {
        ResetMouseState();
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    if (m_FirstMouse)
    {
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;
        m_SmoothedMouseDeltaX = 0.0f;
        m_SmoothedMouseDeltaY = 0.0f;
        m_FirstMouse = false;
    }

    const float rawDeltaX = static_cast<float>(mouseX - m_LastMouseX) * m_MouseSensitivity;
    const float rawDeltaY = static_cast<float>(m_LastMouseY - mouseY) * m_MouseSensitivity;
    m_LastMouseX = mouseX;
    m_LastMouseY = mouseY;

    const float smoothingAlpha = std::clamp(1.0f - std::exp(-m_MouseSmoothingResponse * frameDelta),
                                            0.0f,
                                            1.0f);
    m_SmoothedMouseDeltaX += (rawDeltaX - m_SmoothedMouseDeltaX) * smoothingAlpha;
    m_SmoothedMouseDeltaY += (rawDeltaY - m_SmoothedMouseDeltaY) * smoothingAlpha;

    m_Camera->SetYawPitch(m_Camera->GetYaw() + m_SmoothedMouseDeltaX,
                          m_Camera->GetPitch() + m_SmoothedMouseDeltaY);
}
} // namespace GeoFPS
