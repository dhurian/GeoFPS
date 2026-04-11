#include "Game/FPSController.h"

#include "Renderer/Camera.h"
#include <GLFW/glfw3.h>
#include <algorithm>

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
    m_MoveSpeed = std::max(moveSpeed, 0.5f);
}

void FPSController::SetSprintMultiplier(float sprintMultiplier)
{
    m_SprintMultiplier = std::max(sprintMultiplier, 1.0f);
}

void FPSController::Update(float deltaTime)
{
    if (!m_Enabled || m_Window == nullptr || m_Camera == nullptr)
    {
        return;
    }

    float speed = m_MoveSpeed;
    if (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
    {
        speed *= m_SprintMultiplier;
    }
    m_CurrentSpeed = speed;

    glm::vec3 movement(0.0f);
    const glm::vec3 forwardFlat = glm::normalize(glm::vec3(m_Camera->GetForward().x, 0.0f, m_Camera->GetForward().z));
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
        m_Camera->Move(movement * speed * deltaTime);
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    if (m_FirstMouse)
    {
        m_LastMouseX = mouseX;
        m_LastMouseY = mouseY;
        m_FirstMouse = false;
    }

    const float deltaX = static_cast<float>(mouseX - m_LastMouseX) * m_MouseSensitivity;
    const float deltaY = static_cast<float>(m_LastMouseY - mouseY) * m_MouseSensitivity;
    m_LastMouseX = mouseX;
    m_LastMouseY = mouseY;

    m_Camera->SetYawPitch(m_Camera->GetYaw() + deltaX, m_Camera->GetPitch() + deltaY);
}
} // namespace GeoFPS
