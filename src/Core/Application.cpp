#include "Core/Application.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace GeoFPS
{
namespace
{
struct TerrainBounds
{
    double minLatitude {std::numeric_limits<double>::max()};
    double maxLatitude {std::numeric_limits<double>::lowest()};
    double minLongitude {std::numeric_limits<double>::max()};
    double maxLongitude {std::numeric_limits<double>::lowest()};
    double minHeight {std::numeric_limits<double>::max()};
    double maxHeight {std::numeric_limits<double>::lowest()};
};

glm::vec2 ToGroundPlane(const glm::dvec3& localPosition)
{
    return {static_cast<float>(localPosition.x), static_cast<float>(localPosition.z)};
}

TerrainBounds ComputeTerrainBounds(const std::vector<TerrainPoint>& points)
{
    TerrainBounds bounds;
    for (const auto& point : points)
    {
        bounds.minLatitude = std::min(bounds.minLatitude, point.latitude);
        bounds.maxLatitude = std::max(bounds.maxLatitude, point.latitude);
        bounds.minLongitude = std::min(bounds.minLongitude, point.longitude);
        bounds.maxLongitude = std::max(bounds.maxLongitude, point.longitude);
        bounds.minHeight = std::min(bounds.minHeight, point.height);
        bounds.maxHeight = std::max(bounds.maxHeight, point.height);
    }
    return bounds;
}

bool FileExists(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

void LoadProfessionalUiFont()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float fontSize = 17.0f;

#ifdef __APPLE__
    const std::vector<const char*> candidates = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica Neue.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
#elif defined(_WIN32)
    const std::vector<const char*> candidates = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
#else
    const std::vector<const char*> candidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
    };
#endif

    for (const char* path : candidates)
    {
        if (!FileExists(path))
        {
            continue;
        }

        if (io.Fonts->AddFontFromFileTTF(path, fontSize) != nullptr)
        {
            return;
        }
    }

    io.Fonts->AddFontDefault();
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void ApplyProfessionalImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.IndentSpacing = 12.0f;
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.WindowPadding = ImVec2(12.0f, 12.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.93f, 0.96f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.61f, 0.67f, 1.0f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.12f, 0.15f, 0.96f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.15f, 0.18f, 0.90f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.13f, 0.16f, 0.98f);
    colors[ImGuiCol_Border] = ImVec4(0.24f, 0.28f, 0.33f, 0.90f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.29f, 0.35f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.10f, 0.13f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.13f, 0.16f, 1.0f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.12f, 0.15f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.33f, 0.39f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.40f, 0.47f, 1.0f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.46f, 0.53f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.74f, 0.96f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.63f, 0.84f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.74f, 0.96f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.18f, 0.22f, 0.27f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.31f, 0.38f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.32f, 0.39f, 0.47f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.23f, 0.28f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.30f, 0.36f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.35f, 0.42f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.24f, 0.28f, 0.33f, 0.90f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.34f, 0.40f, 0.47f, 0.35f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.47f, 0.63f, 0.84f, 0.60f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.56f, 0.74f, 0.96f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.17f, 0.21f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.31f, 0.38f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.31f, 1.0f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.13f, 0.16f, 1.0f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.18f, 0.22f, 1.0f);
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

    if (m_AssetShader)
    {
        m_AssetShader->Bind();
        m_AssetShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_AssetShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_AssetShader->SetVec3("uCameraPos", m_Camera.GetPosition());

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

ImportedAsset* Application::GetActiveImportedAsset()
{
    if (m_ActiveImportedAssetIndex < 0 || m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        return nullptr;
    }

    return &m_ImportedAssets[static_cast<size_t>(m_ActiveImportedAssetIndex)];
}

const ImportedAsset* Application::GetActiveImportedAsset() const
{
    if (m_ActiveImportedAssetIndex < 0 || m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        return nullptr;
    }

    return &m_ImportedAssets[static_cast<size_t>(m_ActiveImportedAssetIndex)];
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

bool Application::LoadImportedAsset(ImportedAsset& asset)
{
    if (asset.path.empty())
    {
        asset.loaded = false;
        asset.assetData.primitives.clear();
        m_StatusMessage = "Asset path is empty.";
        return false;
    }

    asset.assetData.primitives.clear();

    const size_t extensionOffset = asset.path.find_last_of('.');
    const std::string extension = extensionOffset == std::string::npos ? std::string() : ToLower(asset.path.substr(extensionOffset));
    std::string errorMessage;

    if (extension == ".glb" || extension == ".gltf")
    {
        if (!GltfImporter::Load(asset.path, asset.assetData, errorMessage))
        {
            asset.loaded = false;
            m_StatusMessage = "Failed to load asset: " + (errorMessage.empty() ? asset.path : errorMessage);
            return false;
        }
    }
    else if (extension == ".obj")
    {
        ImportedPrimitiveData primitiveData;
        if (!ObjImporter::Load(asset.path, primitiveData.meshData, errorMessage))
        {
            asset.loaded = false;
            m_StatusMessage = "Failed to load asset: " + (errorMessage.empty() ? asset.path : errorMessage);
            return false;
        }

        primitiveData.materialName = "OBJ Material";
        primitiveData.baseColorFactor = glm::vec4(0.82f, 0.74f, 0.66f, 1.0f);
        asset.assetData.primitives.push_back(std::move(primitiveData));
    }
    else
    {
        asset.loaded = false;
        m_StatusMessage = "Unsupported asset format. Use .glb, .gltf, or .obj.";
        return false;
    }

    for (ImportedPrimitiveData& primitive : asset.assetData.primitives)
    {
        primitive.mesh = std::make_unique<Mesh>(primitive.meshData);
        primitive.hasBaseColorTexture =
            !primitive.baseColorPixels.empty() && primitive.baseColorTexture.LoadFromMemory(primitive.baseColorPixels.data(),
                                                                                            primitive.baseColorWidth,
                                                                                            primitive.baseColorHeight,
                                                                                            primitive.baseColorChannels);
    }

    asset.loaded = true;
    m_StatusMessage = "Loaded asset: " + asset.name;
    return true;
}

bool Application::DeleteImportedAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_ImportedAssets.size()) || m_ImportedAssets.size() <= 1)
    {
        return false;
    }

    m_ImportedAssets.erase(m_ImportedAssets.begin() + index);
    if (m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
    }

    m_StatusMessage = "Deleted imported asset.";
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

    if (ImGui::BeginMenu("Assets"))
    {
        if (ImGui::MenuItem("Add Asset Slot"))
        {
            ImportedAsset asset;
            asset.name = "Asset " + std::to_string(m_ImportedAssets.size() + 1);
            m_ImportedAssets.push_back(std::move(asset));
            m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
            m_StatusMessage = "Added asset slot.";
        }

        if (ImGui::MenuItem("Delete Active Asset", nullptr, false, m_ImportedAssets.size() > 1))
        {
            DeleteImportedAsset(m_ActiveImportedAssetIndex);
        }

        if (ImGui::MenuItem("Reload Active Asset"))
        {
            ImportedAsset* asset = GetActiveImportedAsset();
            if (asset != nullptr)
            {
                LoadImportedAsset(*asset);
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
    LoadProfessionalUiFont();
    ApplyProfessionalImGuiStyle();
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

void Application::RenderMiniMap()
{
    if (m_TerrainPoints.empty())
    {
        return;
    }

    const TerrainBounds bounds = ComputeTerrainBounds(m_TerrainPoints);
    GeoConverter converter(m_GeoReference);
    const glm::vec3 cameraPosition = m_Camera.GetPosition();
    const glm::dvec3 cameraGeo = converter.ToGeographic(
        {static_cast<double>(cameraPosition.x), static_cast<double>(cameraPosition.y), static_cast<double>(cameraPosition.z)});

    ImGui::Separator();
    ImGui::Text("Navigator");
    ImGui::Text("Lat: %.6f", cameraGeo.x);
    ImGui::Text("Lon: %.6f", cameraGeo.y);
    ImGui::Text("Height: %.2f m", cameraGeo.z);

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float mapSize = std::min(std::max(availableWidth, 180.0f), 260.0f);
    const ImVec2 mapTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 mapBottomRight(mapTopLeft.x + mapSize, mapTopLeft.y + mapSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(mapTopLeft, mapBottomRight, IM_COL32(20, 28, 36, 255), 6.0f);
    drawList->AddRect(mapTopLeft, mapBottomRight, IM_COL32(120, 140, 160, 255), 6.0f, 0, 2.0f);

    const double longitudeSpan = std::max(bounds.maxLongitude - bounds.minLongitude, 1e-9);
    const double latitudeSpan = std::max(bounds.maxLatitude - bounds.minLatitude, 1e-9);
    const float cameraX =
        mapTopLeft.x + static_cast<float>((cameraGeo.y - bounds.minLongitude) / longitudeSpan) * mapSize;
    const float cameraY =
        mapBottomRight.y - static_cast<float>((cameraGeo.x - bounds.minLatitude) / latitudeSpan) * mapSize;
    const float metersPerDegreeLongitude =
        static_cast<float>(std::max(std::abs(converter.ToLocal(bounds.minLatitude, bounds.maxLongitude, m_GeoReference.originHeight).x -
                                             converter.ToLocal(bounds.minLatitude, bounds.minLongitude, m_GeoReference.originHeight).x),
                                    1.0));
    const float metersPerDegreeLatitude =
        static_cast<float>(std::max(std::abs(converter.ToLocal(bounds.maxLatitude, bounds.minLongitude, m_GeoReference.originHeight).z -
                                             converter.ToLocal(bounds.minLatitude, bounds.minLongitude, m_GeoReference.originHeight).z),
                                    1.0));
    const float terrainWidthMeters = metersPerDegreeLongitude;
    const float terrainDepthMeters = metersPerDegreeLatitude;
    const float pixelsPerMeterX = mapSize / terrainWidthMeters;
    const float pixelsPerMeterY = mapSize / terrainDepthMeters;
    const float visibleRangeRadiusPixels =
        std::min(m_Camera.GetFarClip() * pixelsPerMeterX, m_Camera.GetFarClip() * pixelsPerMeterY);

    const float centerX = 0.5f * (mapTopLeft.x + mapBottomRight.x);
    const float centerY = 0.5f * (mapTopLeft.y + mapBottomRight.y);
    drawList->AddLine(ImVec2(centerX, mapTopLeft.y), ImVec2(centerX, mapBottomRight.y), IM_COL32(60, 80, 100, 255), 1.0f);
    drawList->AddLine(ImVec2(mapTopLeft.x, centerY), ImVec2(mapBottomRight.x, centerY), IM_COL32(60, 80, 100, 255), 1.0f);

    const glm::vec3 forward = m_Camera.GetForward();
    const glm::vec2 forward2D(forward.x, forward.z);
    const float forwardLength = glm::length(forward2D);
    glm::vec2 direction = forwardLength > 0.0001f ? (forward2D / forwardLength) : glm::vec2(0.0f, -1.0f);
    direction.y *= -1.0f;

    const ImVec2 cameraPoint(cameraX, cameraY);
    drawList->AddCircle(cameraPoint, visibleRangeRadiusPixels, IM_COL32(80, 180, 255, 120), 64, 1.5f);
    const ImVec2 directionPoint(cameraX + (direction.x * 16.0f), cameraY + (direction.y * 16.0f));
    drawList->AddCircleFilled(cameraPoint, 5.0f, IM_COL32(255, 110, 64, 255));
    drawList->AddLine(cameraPoint, directionPoint, IM_COL32(255, 210, 120, 255), 2.0f);

    ImGui::InvisibleButton("##navigator_map", ImVec2(mapSize, mapSize));
    ImGui::Text("North: %.6f", bounds.maxLatitude);
    ImGui::Text("South: %.6f", bounds.minLatitude);
    ImGui::Text("West: %.6f", bounds.minLongitude);
    ImGui::Text("East: %.6f", bounds.maxLongitude);
    ImGui::Text("Terrain height: %.2f m to %.2f m", bounds.minHeight, bounds.maxHeight);
    ImGui::Text("View range: %.0f m to %.0f m", m_Camera.GetNearClip(), m_Camera.GetFarClip());
}

void Application::RenderCameraHud()
{
    if (m_TerrainPoints.empty())
    {
        return;
    }

    GeoConverter converter(m_GeoReference);
    const glm::vec3 cameraPosition = m_Camera.GetPosition();
    const glm::dvec3 cameraGeo = converter.ToGeographic(
        {static_cast<double>(cameraPosition.x), static_cast<double>(cameraPosition.y), static_cast<double>(cameraPosition.z)});

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("Camera HUD", nullptr, flags))
    {
        ImGui::Text("Lat %.6f", cameraGeo.x);
        ImGui::Text("Lon %.6f", cameraGeo.y);
        ImGui::Text("Height %.2f m", cameraGeo.z);
    }
    ImGui::End();
}

void Application::RenderEditor()
{
    ImGui::Begin("GeoFPS Control Panel");
    ImGui::Text("Custom terrain-mapping FPS engine starter");
    ImGui::Separator();

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    OverlayEntry* activeOverlay = GetActiveOverlayEntry();
    ImportedAsset* activeAsset = GetActiveImportedAsset();

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

    float nearClip = m_Camera.GetNearClip();
    if (ImGui::SliderFloat("Near Clip", &nearClip, 0.1f, 100.0f, "%.1f"))
    {
        m_Camera.SetNearClip(nearClip);
    }

    float farClip = m_Camera.GetFarClip();
    if (ImGui::SliderFloat("Far Clip", &farClip, 1000.0f, 200000.0f, "%.0f"))
    {
        m_Camera.SetFarClip(farClip);
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

    ImGui::Separator();
    ImGui::Text("Blender Assets");

    if (ImGui::CollapsingHeader("Imported Meshes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (m_ImportedAssets.empty())
        {
            ImGui::Text("No imported assets available.");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(m_ImportedAssets.size()); ++i)
            {
                ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(i)];
                const bool selected = i == m_ActiveImportedAssetIndex;
                if (ImGui::Selectable(asset.name.c_str(), selected) && !selected)
                {
                    m_ActiveImportedAssetIndex = i;
                    activeAsset = GetActiveImportedAsset();
                }
            }
        }

        if (ImGui::Button("Add Asset Slot"))
        {
            ImportedAsset asset;
            asset.name = "Asset " + std::to_string(m_ImportedAssets.size() + 1);
            m_ImportedAssets.push_back(std::move(asset));
            m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
            activeAsset = GetActiveImportedAsset();
            m_StatusMessage = "Added asset slot.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Active Asset"))
        {
            DeleteImportedAsset(m_ActiveImportedAssetIndex);
            activeAsset = GetActiveImportedAsset();
        }
    }

    if (activeAsset != nullptr)
    {
        char assetNameBuffer[256];
        std::snprintf(assetNameBuffer, sizeof(assetNameBuffer), "%s", activeAsset->name.c_str());
        if (ImGui::InputText("Asset Name", assetNameBuffer, sizeof(assetNameBuffer)))
        {
            activeAsset->name = assetNameBuffer;
        }

        char assetPathBuffer[512];
        std::snprintf(assetPathBuffer, sizeof(assetPathBuffer), "%s", activeAsset->path.c_str());
        if (ImGui::InputText("Asset Path", assetPathBuffer, sizeof(assetPathBuffer)))
        {
            activeAsset->path = assetPathBuffer;
        }

        ImGui::TextWrapped("Preferred Blender export is .glb. .gltf and .obj also load, but .glb carries materials most cleanly.");
        if (ImGui::Button("Load Asset"))
        {
            LoadImportedAsset(*activeAsset);
        }

        ImGui::DragFloat3("Asset Position", glm::value_ptr(activeAsset->position), 0.1f);
        ImGui::DragFloat3("Asset Rotation", glm::value_ptr(activeAsset->rotationDegrees), 0.5f);
        ImGui::DragFloat3("Asset Scale", glm::value_ptr(activeAsset->scale), 0.05f, 0.01f, 1000.0f);
        ImGui::ColorEdit3("Asset Tint", glm::value_ptr(activeAsset->tint));

        ImGui::Text("Asset: %s", activeAsset->loaded ? "loaded" : "not loaded");
        if (activeAsset->loaded)
        {
            size_t vertexCount = 0;
            size_t triangleCount = 0;
            size_t texturedPrimitiveCount = 0;
            for (const ImportedPrimitiveData& primitive : activeAsset->assetData.primitives)
            {
                vertexCount += primitive.meshData.vertices.size();
                triangleCount += primitive.meshData.indices.size() / 3u;
                texturedPrimitiveCount += primitive.hasBaseColorTexture ? 1u : 0u;
            }

            ImGui::Text("Primitives: %zu", activeAsset->assetData.primitives.size());
            ImGui::Text("Vertices: %zu", vertexCount);
            ImGui::Text("Triangles: %zu", triangleCount);
            ImGui::Text("Textured Materials: %zu", texturedPrimitiveCount);
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
    RenderMiniMap();
    ImGui::End();
}
} // namespace GeoFPS
