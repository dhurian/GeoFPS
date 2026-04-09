#include "Core/Application.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace GeoFPS
{
bool Application::Initialize()
{
    if (!m_Window.Create(1600, 900, "GeoFPS"))
    {
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        throw std::runtime_error("glad failed to load OpenGL functions");
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));
    m_Camera.SetPosition({0.0f, 8.0f, 16.0f});

    m_FPSController.AttachWindow(m_Window.GetNativeHandle());
    m_FPSController.AttachCamera(&m_Camera);

    m_TerrainShader = std::make_unique<Shader>();
    if (!m_TerrainShader->LoadFromFiles("assets/shaders/terrain.vert", "assets/shaders/terrain.frag"))
    {
        throw std::runtime_error("Failed to load terrain shaders");
    }

    SetupImGui();
    m_Window.SetCursorCaptured(true);

    m_TerrainSettings.gridResolutionX = 64;
    m_TerrainSettings.gridResolutionZ = 64;
    m_TerrainSettings.heightScale = 1.0;

    return LoadStartupTerrain();
}

void Application::Run()
{
    while (!m_Window.ShouldClose())
    {
        const float deltaTime = m_Window.PollEventsAndGetDeltaTime();
        ProcessInput(deltaTime);
        Update(deltaTime);
        Render();
    }
}

void Application::Shutdown()
{
    ShutdownImGui();
    m_TerrainMesh.reset();
    m_TerrainShader.reset();
    m_Window.Destroy();
}

void Application::ProcessInput(float deltaTime)
{
    if (m_Window.IsKeyPressed(GLFW_KEY_ESCAPE))
    {
        m_MouseCaptured = false;
        m_Window.SetCursorCaptured(false);
    }
    if (m_Window.IsKeyPressed(GLFW_KEY_TAB))
    {
        m_MouseCaptured = true;
        m_Window.SetCursorCaptured(true);
    }

    m_FPSController.SetEnabled(m_MouseCaptured);
    m_FPSController.Update(deltaTime);
}

void Application::Update(float)
{
    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));
}

void Application::Render()
{
    BeginImGuiFrame();
    RenderEditor();

    glViewport(0, 0, m_Window.GetWidth(), m_Window.GetHeight());
    glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_TerrainShader && m_TerrainMesh)
    {
        m_TerrainShader->Bind();
        m_TerrainShader->SetMat4("uModel", glm::mat4(1.0f));
        m_TerrainShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_TerrainShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_TerrainShader->SetVec3("uCameraPos", m_Camera.GetPosition());
        m_TerrainMesh->Draw();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    m_Window.SwapBuffers();
}

bool Application::LoadStartupTerrain()
{
    m_TerrainPoints.clear();
    if (!TerrainImporter::LoadCSV(m_TerrainPath, m_TerrainPoints))
    {
        std::cerr << "Could not load terrain CSV: " << m_TerrainPath << '\n';
        return false;
    }

    if (m_TerrainPoints.empty())
    {
        return false;
    }

    m_GeoReference.originLatitude = m_TerrainPoints.front().latitude;
    m_GeoReference.originLongitude = m_TerrainPoints.front().longitude;
    m_GeoReference.originHeight = m_TerrainPoints.front().height;

    return RebuildTerrain();
}

bool Application::RebuildTerrain()
{
    GeoConverter converter(m_GeoReference);
    TerrainMeshBuilder builder;
    MeshData meshData = builder.BuildFromGeographicPoints(m_TerrainPoints, converter, m_TerrainSettings);

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        return false;
    }

    m_TerrainMesh = std::make_unique<Mesh>(meshData);
    return true;
}

void Application::SetupImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_Window.GetNativeHandle(), true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void Application::ShutdownImGui()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::BeginImGuiFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::RenderEditor()
{
    ImGui::Begin("GeoFPS Control Panel");
    ImGui::Text("Custom terrain-mapping FPS engine starter");
    ImGui::Separator();

    char pathBuffer[512];
    std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", m_TerrainPath.c_str());
    if (ImGui::InputText("Terrain CSV", pathBuffer, sizeof(pathBuffer)))
    {
        m_TerrainPath = pathBuffer;
    }

    ImGui::InputDouble("Origin Latitude", &m_GeoReference.originLatitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Longitude", &m_GeoReference.originLongitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Height", &m_GeoReference.originHeight, 0.0, 0.0, "%.3f");
    ImGui::SliderInt("Grid X", &m_TerrainSettings.gridResolutionX, 8, 256);
    ImGui::SliderInt("Grid Z", &m_TerrainSettings.gridResolutionZ, 8, 256);
    ImGui::SliderFloat("Height Scale", &m_TerrainSettings.heightScale, 0.1f, 5.0f);

    if (ImGui::Button("Reload Terrain"))
    {
        LoadStartupTerrain();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild Mesh"))
    {
        RebuildTerrain();
    }

    ImGui::Separator();
    const glm::vec3 cameraPos = m_Camera.GetPosition();
    ImGui::Text("Points loaded: %zu", m_TerrainPoints.size());
    ImGui::Text("Camera: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
    ImGui::Text("Mouse capture: %s", m_MouseCaptured ? "on" : "off");
    ImGui::Text("Esc releases mouse, Tab recaptures");
    ImGui::End();
}
} // namespace GeoFPS
