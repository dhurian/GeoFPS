#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

namespace GeoFPS
{
using namespace ApplicationInternal;
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

    m_AssetShader = std::make_unique<Shader>();
    if (!m_AssetShader->LoadFromFiles("assets/shaders/asset.vert", "assets/shaders/asset.frag"))
    {
        throw std::runtime_error("Failed to load asset shaders");
    }

    SetupImGui();
    m_Window.SetCursorCaptured(true);

    InitializeProject();
    if (!ActivateTerrainDataset(m_ActiveTerrainIndex))
    {
        return false;
    }

    return true;
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
    m_AssetShader.reset();
    m_TerrainShader.reset();
    m_Window.Destroy();
}

void Application::ProcessInput(float deltaTime)
{
    static bool increaseSpeedPressedLastFrame = false;
    static bool decreaseSpeedPressedLastFrame = false;

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

    const bool increaseSpeedPressed =
        m_Window.IsKeyPressed(GLFW_KEY_EQUAL) || m_Window.IsKeyPressed(GLFW_KEY_KP_ADD);
    if (increaseSpeedPressed && !increaseSpeedPressedLastFrame)
    {
        m_FPSController.SetMoveSpeed(m_FPSController.GetMoveSpeed() + 4.0f);
        m_StatusMessage = "Camera speed increased.";
    }
    increaseSpeedPressedLastFrame = increaseSpeedPressed;

    const bool decreaseSpeedPressed =
        m_Window.IsKeyPressed(GLFW_KEY_MINUS) || m_Window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT);
    if (decreaseSpeedPressed && !decreaseSpeedPressedLastFrame)
    {
        m_FPSController.SetMoveSpeed(m_FPSController.GetMoveSpeed() - 4.0f);
        m_StatusMessage = "Camera speed decreased.";
    }
    decreaseSpeedPressedLastFrame = decreaseSpeedPressed;

    m_FPSController.SetEnabled(m_MouseCaptured);
    m_FPSController.Update(deltaTime);
}

void Application::Update(float)
{
    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));
}

void Application::Render()
{
    const SunParameters sun = ComputeSunParameters(m_GeoReference, m_SunSettings);
    glViewport(0, 0, m_Window.GetWidth(), m_Window.GetHeight());
    glClearColor(sun.skyColor.r, sun.skyColor.g, sun.skyColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    BeginImGuiFrame();
    RenderMainMenuBar();

    if (m_TerrainShader && m_TerrainMesh)
    {
        m_TerrainShader->Bind();
        m_TerrainShader->SetMat4("uModel", glm::mat4(1.0f));
        m_TerrainShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_TerrainShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_TerrainShader->SetVec3("uCameraPos", m_Camera.GetPosition());
        ApplySunUniforms(*m_TerrainShader, sun);

        const OverlayEntry* activeOverlay = GetActiveOverlayEntry();
        GeoImageDefinition overlayDefinition {};
        if (activeOverlay != nullptr)
        {
            overlayDefinition = activeOverlay->image;
        }

        GeoConverter converter(m_GeoReference);
        const glm::vec2 overlayOrigin = ToGroundPlane(converter.ToLocal(
            overlayDefinition.topLeft.latitude, overlayDefinition.topLeft.longitude, m_GeoReference.originHeight));
        const glm::vec2 overlayAxisU = ToGroundPlane(converter.ToLocal(
            overlayDefinition.topRight.latitude, overlayDefinition.topRight.longitude, m_GeoReference.originHeight)) -
            overlayOrigin;
        const glm::vec2 overlayAxisV = ToGroundPlane(converter.ToLocal(
            overlayDefinition.bottomLeft.latitude, overlayDefinition.bottomLeft.longitude, m_GeoReference.originHeight)) -
            overlayOrigin;

        const bool overlayReady =
            activeOverlay != nullptr && overlayDefinition.enabled && overlayDefinition.loaded && m_OverlayTexture.IsLoaded();
        m_TerrainShader->SetInt("uUseOverlay", overlayReady ? 1 : 0);
        m_TerrainShader->SetFloat("uOverlayOpacity", overlayDefinition.opacity);
        m_TerrainShader->SetVec2("uOverlayOrigin", overlayOrigin);
        m_TerrainShader->SetVec2("uOverlayAxisU", overlayAxisU);
        m_TerrainShader->SetVec2("uOverlayAxisV", overlayAxisV);
        m_TerrainShader->SetInt("uOverlayTexture", 0);
        m_OverlayTexture.Bind(0);

        m_TerrainMesh->Draw();
    }

    if (m_AssetShader)
    {
        m_AssetShader->Bind();
        m_AssetShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_AssetShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_AssetShader->SetVec3("uCameraPos", m_Camera.GetPosition());
        ApplySunUniforms(*m_AssetShader, sun);

        for (const ImportedAsset& asset : m_ImportedAssets)
        {
            if (!asset.loaded)
            {
                continue;
            }

            glm::mat4 model(1.0f);
            model = glm::translate(model, asset.position);
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, asset.scale);

            m_AssetShader->SetMat4("uModel", model);

            for (const ImportedPrimitiveData& primitive : asset.assetData.primitives)
            {
                if (!primitive.mesh)
                {
                    continue;
                }

                m_AssetShader->SetVec3("uTintColor", asset.tint);
                m_AssetShader->SetInt("uUseBaseColorTexture", primitive.hasBaseColorTexture ? 1 : 0);
                m_AssetShader->SetVec4("uBaseColorFactor", primitive.baseColorFactor);
                m_AssetShader->SetInt("uBaseColorTexture", 0);
                primitive.baseColorTexture.Bind(0);
                primitive.mesh->Draw();
            }
        }
    }

    RenderTerrainDatasetWindow();
    RenderSunWindow();
    RenderAerialOverlayWindow();
    RenderBlenderAssetsWindow();
    RenderEditor();
    RenderCameraHud();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    m_Window.SwapBuffers();
}

void Application::InitializeProject()
{
    TerrainDataset dataset;
    dataset.name = "Terrain 1";
    dataset.path = "assets/data/sample_terrain.csv";
    dataset.settings.gridResolutionX = 64;
    dataset.settings.gridResolutionZ = 64;
    dataset.settings.heightScale = 1.0f;

    OverlayEntry overlay;
    overlay.name = "Overlay 1";
    overlay.image.opacity = 0.85f;
    dataset.overlays.push_back(overlay);

    m_TerrainDatasets.push_back(dataset);
    m_ActiveTerrainIndex = 0;

    ImportedAsset asset;
    asset.name = "Asset 1";
    m_ImportedAssets.push_back(std::move(asset));
    m_ActiveImportedAssetIndex = 0;

    m_StatusMessage = "Project initialized.";
}


} // namespace GeoFPS
