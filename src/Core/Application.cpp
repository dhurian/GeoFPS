#include "Core/Application.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace GeoFPS
{
namespace
{
glm::vec2 ToGroundPlane(const glm::dvec3& localPosition)
{
    return {static_cast<float>(localPosition.x), static_cast<float>(localPosition.z)};
}
} // namespace

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
    glViewport(0, 0, m_Window.GetWidth(), m_Window.GetHeight());
    glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
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

    RenderEditor();
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
    m_StatusMessage = "Project initialized.";
}

TerrainDataset* Application::GetActiveTerrainDataset()
{
    if (m_ActiveTerrainIndex < 0 || m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return nullptr;
    }

    return &m_TerrainDatasets[static_cast<size_t>(m_ActiveTerrainIndex)];
}

const TerrainDataset* Application::GetActiveTerrainDataset() const
{
    if (m_ActiveTerrainIndex < 0 || m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return nullptr;
    }

    return &m_TerrainDatasets[static_cast<size_t>(m_ActiveTerrainIndex)];
}

OverlayEntry* Application::GetActiveOverlayEntry()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->activeOverlayIndex < 0 ||
        dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return nullptr;
    }

    return &dataset->overlays[static_cast<size_t>(dataset->activeOverlayIndex)];
}

const OverlayEntry* Application::GetActiveOverlayEntry() const
{
    const TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->activeOverlayIndex < 0 ||
        dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return nullptr;
    }

    return &dataset->overlays[static_cast<size_t>(dataset->activeOverlayIndex)];
}

bool Application::LoadTerrainDataset(TerrainDataset& dataset)
{
    dataset.points.clear();
    if (!TerrainImporter::LoadCSV(dataset.path, dataset.points))
    {
        std::cerr << "Could not load terrain CSV: " << dataset.path << '\n';
        m_StatusMessage = "Failed to load terrain: " + dataset.path;
        return false;
    }

    if (dataset.points.empty())
    {
        m_StatusMessage = "Terrain file contained no valid points.";
        return false;
    }

    dataset.geoReference.originLatitude = dataset.points.front().latitude;
    dataset.geoReference.originLongitude = dataset.points.front().longitude;
    dataset.geoReference.originHeight = dataset.points.front().height;
    dataset.loaded = true;

    for (auto& overlay : dataset.overlays)
    {
        ResetOverlayToTerrainBounds(overlay.image, dataset.points);
    }

    m_StatusMessage = "Loaded terrain: " + dataset.name;
    return true;
}

bool Application::ActivateTerrainDataset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return false;
    }

    m_ActiveTerrainIndex = index;
    TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(index)];
    if (!dataset.loaded && !LoadTerrainDataset(dataset))
    {
        return false;
    }

    LoadActiveTerrainIntoScene();

    if (!LoadActiveOverlayImage())
    {
        m_OverlayTexture.Reset();
    }

    return RebuildTerrain();
}

bool Application::LoadOverlayImage(OverlayEntry& overlay)
{
    if (overlay.image.imagePath.empty())
    {
        overlay.image.loaded = false;
        m_OverlayTexture.Reset();
        m_StatusMessage = "Overlay path is empty.";
        return false;
    }

    overlay.image.loaded = m_OverlayTexture.LoadFromFile(overlay.image.imagePath);
    if (!overlay.image.loaded)
    {
        m_StatusMessage = "Failed to load overlay: " + overlay.image.imagePath;
        return false;
    }

    m_StatusMessage = "Loaded overlay: " + overlay.name;
    return true;
}

bool Application::LoadActiveOverlayImage()
{
    OverlayEntry* overlay = GetActiveOverlayEntry();
    if (overlay == nullptr)
    {
        m_OverlayTexture.Reset();
        return false;
    }

    return LoadOverlayImage(*overlay);
}

bool Application::DeleteTerrainDataset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_TerrainDatasets.size()) || m_TerrainDatasets.size() <= 1)
    {
        return false;
    }

    m_TerrainDatasets.erase(m_TerrainDatasets.begin() + index);
    if (m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        m_ActiveTerrainIndex = static_cast<int>(m_TerrainDatasets.size()) - 1;
    }

    m_StatusMessage = "Deleted terrain dataset.";
    return ActivateTerrainDataset(m_ActiveTerrainIndex);
}

bool Application::DeleteActiveOverlay()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->overlays.size() <= 1)
    {
        return false;
    }

    if (dataset->activeOverlayIndex < 0 || dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return false;
    }

    dataset->overlays.erase(dataset->overlays.begin() + dataset->activeOverlayIndex);
    if (dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        dataset->activeOverlayIndex = static_cast<int>(dataset->overlays.size()) - 1;
    }

    m_OverlayTexture.Reset();
    m_StatusMessage = "Deleted overlay.";
    LoadActiveOverlayImage();
    return true;
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
    m_StatusMessage = "Terrain mesh rebuilt.";
    return true;
}

void Application::ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition,
                                              const std::vector<TerrainPoint>& points) const
{
    if (points.empty())
    {
        return;
    }

    double minLatitude = std::numeric_limits<double>::max();
    double maxLatitude = std::numeric_limits<double>::lowest();
    double minLongitude = std::numeric_limits<double>::max();
    double maxLongitude = std::numeric_limits<double>::lowest();

    for (const auto& point : points)
    {
        minLatitude = std::min(minLatitude, point.latitude);
        maxLatitude = std::max(maxLatitude, point.latitude);
        minLongitude = std::min(minLongitude, point.longitude);
        maxLongitude = std::max(maxLongitude, point.longitude);
    }

    imageDefinition.topLeft = {maxLatitude, minLongitude};
    imageDefinition.topRight = {maxLatitude, maxLongitude};
    imageDefinition.bottomLeft = {minLatitude, minLongitude};
    imageDefinition.bottomRight = {minLatitude, maxLongitude};
}

void Application::ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition) const
{
    ResetOverlayToTerrainBounds(imageDefinition, m_TerrainPoints);
}

void Application::LoadActiveTerrainIntoScene()
{
    const TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr)
    {
        return;
    }

    m_TerrainPoints = dataset->points;
    m_GeoReference = dataset->geoReference;
    m_TerrainSettings = dataset->settings;
}

void Application::RenderMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Reload Active Terrain"))
        {
            TerrainDataset* dataset = GetActiveTerrainDataset();
            if (dataset != nullptr && LoadTerrainDataset(*dataset))
            {
                LoadActiveTerrainIntoScene();
                RebuildTerrain();
            }
        }

        if (ImGui::MenuItem("Reload Active Overlay"))
        {
            LoadActiveOverlayImage();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Terrain"))
    {
        if (ImGui::MenuItem("Add Terrain Dataset"))
        {
            TerrainDataset dataset;
            dataset.name = "Terrain " + std::to_string(m_TerrainDatasets.size() + 1);
            dataset.path = "assets/data/sample_terrain.csv";
            OverlayEntry overlay;
            overlay.name = "Overlay 1";
            dataset.overlays.push_back(overlay);
            m_TerrainDatasets.push_back(dataset);
            ActivateTerrainDataset(static_cast<int>(m_TerrainDatasets.size()) - 1);
        }

        if (ImGui::MenuItem("Delete Active Terrain", nullptr, false, m_TerrainDatasets.size() > 1))
        {
            DeleteTerrainDataset(m_ActiveTerrainIndex);
        }

        if (ImGui::MenuItem("Next Terrain", nullptr, false, m_TerrainDatasets.size() > 1))
        {
            const int nextIndex = (m_ActiveTerrainIndex + 1) % static_cast<int>(m_TerrainDatasets.size());
            ActivateTerrainDataset(nextIndex);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Overlay"))
    {
        if (ImGui::MenuItem("Add Overlay To Active Terrain"))
        {
            TerrainDataset* dataset = GetActiveTerrainDataset();
            if (dataset != nullptr)
            {
                OverlayEntry overlay;
                overlay.name = "Overlay " + std::to_string(dataset->overlays.size() + 1);
                ResetOverlayToTerrainBounds(overlay.image);
                dataset->overlays.push_back(overlay);
                dataset->activeOverlayIndex = static_cast<int>(dataset->overlays.size()) - 1;
                m_OverlayTexture.Reset();
                m_StatusMessage = "Added overlay slot to active terrain.";
            }
        }

        if (ImGui::MenuItem("Delete Active Overlay", nullptr, false, GetActiveTerrainDataset() != nullptr &&
                                                                 GetActiveTerrainDataset()->overlays.size() > 1))
        {
            DeleteActiveOverlay();
        }

        if (ImGui::MenuItem("Next Overlay", nullptr, false, GetActiveTerrainDataset() != nullptr &&
                                                           GetActiveTerrainDataset()->overlays.size() > 1))
        {
            TerrainDataset* dataset = GetActiveTerrainDataset();
            if (dataset != nullptr)
            {
                dataset->activeOverlayIndex = (dataset->activeOverlayIndex + 1) % static_cast<int>(dataset->overlays.size());
                LoadActiveOverlayImage();
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Application::SetupImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_Window.GetNativeHandle(), true);
#ifdef __APPLE__
    ImGui_ImplOpenGL3_Init("#version 150");
#else
    ImGui_ImplOpenGL3_Init("#version 330");
#endif
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

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    OverlayEntry* activeOverlay = GetActiveOverlayEntry();

    if (activeTerrain == nullptr)
    {
        ImGui::Text("No terrain datasets available.");
        ImGui::End();
        return;
    }

    if (!m_StatusMessage.empty())
    {
        ImGui::TextWrapped("Status: %s", m_StatusMessage.c_str());
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("Terrain Datasets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (int i = 0; i < static_cast<int>(m_TerrainDatasets.size()); ++i)
        {
            TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(i)];
            const bool selected = i == m_ActiveTerrainIndex;
            if (ImGui::Selectable(dataset.name.c_str(), selected) && !selected)
            {
                ActivateTerrainDataset(i);
                activeTerrain = GetActiveTerrainDataset();
                activeOverlay = GetActiveOverlayEntry();
            }
        }

        char terrainNameBuffer[256];
        std::snprintf(terrainNameBuffer, sizeof(terrainNameBuffer), "%s", activeTerrain->name.c_str());
        if (ImGui::InputText("Terrain Name", terrainNameBuffer, sizeof(terrainNameBuffer)))
        {
            activeTerrain->name = terrainNameBuffer;
        }

        char pathBuffer[512];
        std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", activeTerrain->path.c_str());
        if (ImGui::InputText("Terrain CSV", pathBuffer, sizeof(pathBuffer)))
        {
            activeTerrain->path = pathBuffer;
        }

        if (ImGui::Button("Load Active Terrain"))
        {
            if (LoadTerrainDataset(*activeTerrain))
            {
                LoadActiveTerrainIntoScene();
                RebuildTerrain();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Terrain Dataset"))
        {
            TerrainDataset dataset;
            dataset.name = "Terrain " + std::to_string(m_TerrainDatasets.size() + 1);
            dataset.path = activeTerrain->path;
            dataset.settings = activeTerrain->settings;
            dataset.overlays.push_back(OverlayEntry {});
            m_TerrainDatasets.push_back(dataset);
            ActivateTerrainDataset(static_cast<int>(m_TerrainDatasets.size()) - 1);
            activeTerrain = GetActiveTerrainDataset();
            activeOverlay = GetActiveOverlayEntry();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Active Terrain"))
        {
            DeleteTerrainDataset(m_ActiveTerrainIndex);
            activeTerrain = GetActiveTerrainDataset();
            activeOverlay = GetActiveOverlayEntry();
        }
    }

    ImGui::InputDouble("Origin Latitude", &m_GeoReference.originLatitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Longitude", &m_GeoReference.originLongitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Height", &m_GeoReference.originHeight, 0.0, 0.0, "%.3f");
    ImGui::SliderInt("Grid X", &m_TerrainSettings.gridResolutionX, 8, 256);
    ImGui::SliderInt("Grid Z", &m_TerrainSettings.gridResolutionZ, 8, 256);
    ImGui::SliderFloat("Height Scale", &m_TerrainSettings.heightScale, 0.1f, 5.0f);

    float moveSpeed = m_FPSController.GetMoveSpeed();
    if (ImGui::SliderFloat("Camera Speed", &moveSpeed, 1.0f, 200.0f, "%.1f"))
    {
        m_FPSController.SetMoveSpeed(moveSpeed);
    }

    float sprintMultiplier = m_FPSController.GetSprintMultiplier();
    if (ImGui::SliderFloat("Sprint Multiplier", &sprintMultiplier, 1.0f, 10.0f, "%.1f"))
    {
        m_FPSController.SetSprintMultiplier(sprintMultiplier);
    }

    if (ImGui::Button("Reload Terrain"))
    {
        if (LoadTerrainDataset(*activeTerrain))
        {
            LoadActiveTerrainIntoScene();
            RebuildTerrain();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild Mesh"))
    {
        activeTerrain->geoReference = m_GeoReference;
        activeTerrain->settings = m_TerrainSettings;
        RebuildTerrain();
    }

    ImGui::Separator();
    ImGui::Text("Aerial Image Overlay");

    if (ImGui::CollapsingHeader("Overlay Library", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (activeTerrain != nullptr)
        {
            for (int i = 0; i < static_cast<int>(activeTerrain->overlays.size()); ++i)
            {
                OverlayEntry& overlay = activeTerrain->overlays[static_cast<size_t>(i)];
                const bool selected = i == activeTerrain->activeOverlayIndex;
                if (ImGui::Selectable(overlay.name.c_str(), selected) && !selected)
                {
                    activeTerrain->activeOverlayIndex = i;
                    LoadActiveOverlayImage();
                    activeOverlay = GetActiveOverlayEntry();
                }
            }

            if (ImGui::Button("Add Overlay Slot"))
            {
                OverlayEntry overlay;
                overlay.name = "Overlay " + std::to_string(activeTerrain->overlays.size() + 1);
                ResetOverlayToTerrainBounds(overlay.image);
                activeTerrain->overlays.push_back(overlay);
                activeTerrain->activeOverlayIndex = static_cast<int>(activeTerrain->overlays.size()) - 1;
                activeOverlay = GetActiveOverlayEntry();
                m_OverlayTexture.Reset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Active Overlay"))
            {
                DeleteActiveOverlay();
                activeOverlay = GetActiveOverlayEntry();
            }
        }
    }

    if (activeOverlay != nullptr)
    {
        char overlayNameBuffer[256];
        std::snprintf(overlayNameBuffer, sizeof(overlayNameBuffer), "%s", activeOverlay->name.c_str());
        if (ImGui::InputText("Overlay Name", overlayNameBuffer, sizeof(overlayNameBuffer)))
        {
            activeOverlay->name = overlayNameBuffer;
        }

        char imagePathBuffer[512];
        std::snprintf(imagePathBuffer, sizeof(imagePathBuffer), "%s", activeOverlay->image.imagePath.c_str());
        if (ImGui::InputText("Image Path", imagePathBuffer, sizeof(imagePathBuffer)))
        {
            activeOverlay->image.imagePath = imagePathBuffer;
        }

        ImGui::Checkbox("Enable Overlay", &activeOverlay->image.enabled);
        ImGui::SliderFloat("Overlay Opacity", &activeOverlay->image.opacity, 0.0f, 1.0f);

        ImGui::InputDouble("Top Left Lat", &activeOverlay->image.topLeft.latitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Top Left Lon", &activeOverlay->image.topLeft.longitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Top Right Lat", &activeOverlay->image.topRight.latitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Top Right Lon", &activeOverlay->image.topRight.longitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bottom Left Lat", &activeOverlay->image.bottomLeft.latitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bottom Left Lon", &activeOverlay->image.bottomLeft.longitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bottom Right Lat", &activeOverlay->image.bottomRight.latitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bottom Right Lon", &activeOverlay->image.bottomRight.longitude, 0.0, 0.0, "%.8f");

        if (ImGui::Button("Load Overlay Image"))
        {
            LoadOverlayImage(*activeOverlay);
        }
        ImGui::SameLine();
        if (ImGui::Button("Fit Overlay To Terrain"))
        {
            ResetOverlayToTerrainBounds(activeOverlay->image);
        }

        ImGui::Text("Overlay: %s", activeOverlay->image.loaded ? "loaded" : "not loaded");
        if (activeOverlay->image.loaded)
        {
            ImGui::Text("Image Size: %d x %d", m_OverlayTexture.GetWidth(), m_OverlayTexture.GetHeight());
        }
    }

    activeTerrain->geoReference = m_GeoReference;
    activeTerrain->settings = m_TerrainSettings;

    ImGui::Separator();
    const glm::vec3 cameraPos = m_Camera.GetPosition();
    ImGui::Text("Points loaded: %zu", m_TerrainPoints.size());
    ImGui::Text("Camera: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
    ImGui::Text("Move Speed: %.1f (current: %.1f)", m_FPSController.GetMoveSpeed(), m_FPSController.GetCurrentSpeed());
    ImGui::Text("Mouse capture: %s", m_MouseCaptured ? "on" : "off");
    ImGui::Text("Esc releases mouse, Tab recaptures, +/- adjusts speed, Shift sprints");
    ImGui::End();
}
} // namespace GeoFPS
