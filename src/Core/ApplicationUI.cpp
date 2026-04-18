#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>

namespace GeoFPS
{
using namespace ApplicationInternal;
void Application::RenderMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        char worldFilePathBuffer[512];
        std::snprintf(worldFilePathBuffer, sizeof(worldFilePathBuffer), "%s", m_WorldFilePath.c_str());
        if (ImGui::InputText("World File", worldFilePathBuffer, sizeof(worldFilePathBuffer)))
        {
            m_WorldFilePath = worldFilePathBuffer;
        }

        if (ImGui::MenuItem("Save Current World"))
        {
            SaveWorldToFile(m_WorldFilePath);
        }

        if (ImGui::MenuItem("Load World File"))
        {
            LoadWorldFromFile(m_WorldFilePath);
        }

        ImGui::Separator();

        char assetsFilePathBuffer[512];
        std::snprintf(assetsFilePathBuffer, sizeof(assetsFilePathBuffer), "%s", m_BlenderAssetsFilePath.c_str());
        if (ImGui::InputText("Blender Assets File", assetsFilePathBuffer, sizeof(assetsFilePathBuffer)))
        {
            m_BlenderAssetsFilePath = assetsFilePathBuffer;
        }

        if (ImGui::MenuItem("Import Blender Assets File"))
        {
            LoadBlenderAssetsFromFile(m_BlenderAssetsFilePath);
        }

        char worldReadoutFilePathBuffer[512];
        std::snprintf(worldReadoutFilePathBuffer, sizeof(worldReadoutFilePathBuffer), "%s", m_WorldReadoutFilePath.c_str());
        if (ImGui::InputText("World Readout File", worldReadoutFilePathBuffer, sizeof(worldReadoutFilePathBuffer)))
        {
            m_WorldReadoutFilePath = worldReadoutFilePathBuffer;
        }

        if (ImGui::MenuItem("Write Current World Readout"))
        {
            if (WriteCurrentWorldReadout(m_WorldReadoutFilePath))
            {
                m_StatusMessage = "Wrote current world readout: " + m_WorldReadoutFilePath;
            }
            else
            {
                m_StatusMessage = "Failed to write current world readout.";
            }
        }

        ImGui::Separator();

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

    if (ImGui::BeginMenu("Panels"))
    {
        ImGui::MenuItem("Terrain Datasets", nullptr, &m_ShowTerrainDatasetWindow);
        ImGui::MenuItem("Sun Illumination", nullptr, &m_ShowSunWindow);
        ImGui::MenuItem("Aerial Overlay", nullptr, &m_ShowAerialOverlayWindow);
        ImGui::MenuItem("Blender Assets", nullptr, &m_ShowBlenderAssetsWindow);
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
    const float cameraMapU = static_cast<float>((cameraGeo.y - bounds.minLongitude) / longitudeSpan);
    const float cameraMapV = static_cast<float>((cameraGeo.x - bounds.minLatitude) / latitudeSpan);
    const bool cameraInsideBounds = cameraMapU >= 0.0f && cameraMapU <= 1.0f && cameraMapV >= 0.0f && cameraMapV <= 1.0f;
    const float cameraX = mapTopLeft.x + std::clamp(cameraMapU, 0.0f, 1.0f) * mapSize;
    const float cameraY = mapBottomRight.y - std::clamp(cameraMapV, 0.0f, 1.0f) * mapSize;
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
    drawList->AddCircleFilled(cameraPoint, 6.0f, cameraInsideBounds ? IM_COL32(255, 110, 64, 255) : IM_COL32(255, 196, 64, 255));
    drawList->AddLine(cameraPoint, directionPoint, IM_COL32(255, 210, 120, 255), 2.0f);

    ImGui::InvisibleButton("##navigator_map", ImVec2(mapSize, mapSize));
    if (ImGui::IsItemHovered())
    {
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const float mouseU = std::clamp((mousePosition.x - mapTopLeft.x) / mapSize, 0.0f, 1.0f);
        const float mouseV = std::clamp((mapBottomRight.y - mousePosition.y) / mapSize, 0.0f, 1.0f);
        const double targetLongitude = bounds.minLongitude + (static_cast<double>(mouseU) * longitudeSpan);
        const double targetLatitude = bounds.minLatitude + (static_cast<double>(mouseV) * latitudeSpan);
        const float targetHeight = SampleTerrainHeightAt(targetLatitude, targetLongitude) + 2.0f;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const glm::dvec3 localTarget = converter.ToLocal(targetLatitude, targetLongitude, targetHeight);
            m_Camera.SetPosition(glm::vec3(static_cast<float>(localTarget.x),
                                           static_cast<float>(localTarget.y),
                                           static_cast<float>(localTarget.z)));
            m_StatusMessage = "Moved camera from minimap.";
        }

        drawList->AddCircle(ImVec2(mousePosition.x, mousePosition.y), 4.0f, IM_COL32(255, 255, 255, 160), 24, 1.5f);
        ImGui::SetTooltip("Click to move\nLat %.6f\nLon %.6f", targetLatitude, targetLongitude);
    }

    ImGui::Text("North: %.6f", bounds.maxLatitude);
    ImGui::Text("South: %.6f", bounds.minLatitude);
    ImGui::Text("West: %.6f", bounds.minLongitude);
    ImGui::Text("East: %.6f", bounds.maxLongitude);
    ImGui::Text("Terrain height: %.2f m to %.2f m", bounds.minHeight, bounds.maxHeight);
    ImGui::Text("View range: %.0f m to %.0f m", m_Camera.GetNearClip(), m_Camera.GetFarClip());
    ImGui::Text("Camera on map: %s", cameraInsideBounds ? "inside terrain bounds" : "outside terrain bounds");
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

void Application::RenderTerrainDatasetWindow()
{
    if (!m_ShowTerrainDatasetWindow)
    {
        return;
    }

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    if (!ImGui::Begin("Terrain Datasets", &m_ShowTerrainDatasetWindow))
    {
        ImGui::End();
        return;
    }

    if (activeTerrain == nullptr)
    {
        ImGui::Text("No terrain datasets available.");
        ImGui::End();
        return;
    }

    for (int i = 0; i < static_cast<int>(m_TerrainDatasets.size()); ++i)
    {
        TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(i)];
        const bool selected = i == m_ActiveTerrainIndex;
        if (ImGui::Selectable(dataset.name.c_str(), selected) && !selected)
        {
            ActivateTerrainDataset(i);
            activeTerrain = GetActiveTerrainDataset();
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
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Active Terrain"))
    {
        DeleteTerrainDataset(m_ActiveTerrainIndex);
        activeTerrain = GetActiveTerrainDataset();
    }

    ImGui::Separator();
    ImGui::InputDouble("Origin Latitude", &m_GeoReference.originLatitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Longitude", &m_GeoReference.originLongitude, 0.0, 0.0, "%.8f");
    ImGui::InputDouble("Origin Height", &m_GeoReference.originHeight, 0.0, 0.0, "%.3f");
    ImGui::SliderInt("Grid X", &m_TerrainSettings.gridResolutionX, 8, 256);
    ImGui::SliderInt("Grid Z", &m_TerrainSettings.gridResolutionZ, 8, 256);
    ImGui::SliderFloat("Height Scale", &m_TerrainSettings.heightScale, 0.1f, 5.0f);

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

    activeTerrain->geoReference = m_GeoReference;
    activeTerrain->settings = m_TerrainSettings;
    ImGui::End();
}

void Application::RenderSunWindow()
{
    if (!m_ShowSunWindow)
    {
        return;
    }

    if (!ImGui::Begin("Sun Illumination", &m_ShowSunWindow))
    {
        ImGui::End();
        return;
    }

    RenderSunControls();
    ImGui::End();
}

void Application::RenderAerialOverlayWindow()
{
    if (!m_ShowAerialOverlayWindow)
    {
        return;
    }

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    OverlayEntry* activeOverlay = GetActiveOverlayEntry();

    if (!ImGui::Begin("Aerial Overlay", &m_ShowAerialOverlayWindow))
    {
        ImGui::End();
        return;
    }

    if (activeTerrain == nullptr)
    {
        ImGui::Text("No active terrain dataset.");
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("Overlay Library", ImGuiTreeNodeFlags_DefaultOpen))
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

    ImGui::End();
}

void Application::RenderBlenderAssetsWindow()
{
    if (!m_ShowBlenderAssetsWindow)
    {
        return;
    }

    ImportedAsset* activeAsset = GetActiveImportedAsset();
    const size_t selectedAssetCount = GetSelectedImportedAssetCount();
    static glm::vec3 bulkLocalPosition(0.0f);
    static glm::vec3 bulkScale(1.0f);
    static float bulkRotationZ = 0.0f;
    static double bulkLatitude = 0.0;
    static double bulkLongitude = 0.0;
    static double bulkHeight = 0.0;

    if (!ImGui::Begin("Blender Assets", &m_ShowBlenderAssetsWindow))
    {
        ImGui::End();
        return;
    }

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
                ImGui::PushID(i);
                ImGui::Checkbox("##asset_selected", &asset.selected);
                ImGui::SameLine();
                const bool active = i == m_ActiveImportedAssetIndex;
                if (ImGui::Selectable(asset.name.c_str(), active) && !active)
                {
                    m_ActiveImportedAssetIndex = i;
                    activeAsset = GetActiveImportedAsset();
                }
                ImGui::PopID();
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
        ImGui::SameLine();
        if (ImGui::Button("Copy Selected"))
        {
            CopySelectedImportedAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste Copied"))
        {
            PasteCopiedImportedAssets();
            activeAsset = GetActiveImportedAsset();
        }
    }

    if (selectedAssetCount > 0)
    {
        ImGui::Separator();
        ImGui::Text("Selected Assets: %zu", selectedAssetCount);
        ImGui::DragFloat3("Bulk Local Position", glm::value_ptr(bulkLocalPosition), 0.1f);
        if (ImGui::Button("Apply Local Position To Selected"))
        {
            for (ImportedAsset& asset : m_ImportedAssets)
            {
                if (!asset.selected)
                {
                    continue;
                }
                asset.useGeographicPlacement = false;
                asset.position = bulkLocalPosition;
            }
            m_StatusMessage = "Applied local position to selected assets.";
        }

        ImGui::InputDouble("Bulk Latitude", &bulkLatitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bulk Longitude", &bulkLongitude, 0.0, 0.0, "%.8f");
        ImGui::InputDouble("Bulk Height", &bulkHeight, 0.0, 0.0, "%.3f");
        if (ImGui::Button("Apply Geographic Position To Selected"))
        {
            for (ImportedAsset& asset : m_ImportedAssets)
            {
                if (!asset.selected)
                {
                    continue;
                }
                asset.useGeographicPlacement = true;
                asset.latitude = bulkLatitude;
                asset.longitude = bulkLongitude;
                asset.height = bulkHeight;
                UpdateImportedAssetPositionFromGeographic(asset);
            }
            m_StatusMessage = "Applied geographic position to selected assets.";
        }

        ImGui::DragFloat3("Bulk Scale XYZ", glm::value_ptr(bulkScale), 0.05f, 0.01f, 1000.0f);
        if (ImGui::Button("Apply Scale To Selected"))
        {
            for (ImportedAsset& asset : m_ImportedAssets)
            {
                if (!asset.selected)
                {
                    continue;
                }
                asset.scale = bulkScale;
            }
            m_StatusMessage = "Applied scale to selected assets.";
        }

        ImGui::DragFloat("Bulk Rotation Z", &bulkRotationZ, 0.5f);
        if (ImGui::Button("Apply Rotation Z To Selected"))
        {
            for (ImportedAsset& asset : m_ImportedAssets)
            {
                if (!asset.selected)
                {
                    continue;
                }
                asset.rotationDegrees.z = bulkRotationZ;
            }
            m_StatusMessage = "Applied Z rotation to selected assets.";
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

        ImGui::Checkbox("Use Geographic Placement", &activeAsset->useGeographicPlacement);
        if (activeAsset->useGeographicPlacement)
        {
            ImGui::InputDouble("Asset Latitude", &activeAsset->latitude, 0.0, 0.0, "%.8f");
            ImGui::InputDouble("Asset Longitude", &activeAsset->longitude, 0.0, 0.0, "%.8f");
            ImGui::InputDouble("Asset Height", &activeAsset->height, 0.0, 0.0, "%.3f");
            if (ImGui::Button("Apply Geographic Position"))
            {
                UpdateImportedAssetPositionFromGeographic(*activeAsset);
            }
        }

        ImGui::DragFloat3("Asset Position", glm::value_ptr(activeAsset->position), 0.1f);
        ImGui::DragFloat("Asset Rotation Z", &activeAsset->rotationDegrees.z, 0.5f);
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

    ImGui::End();
}

void Application::RenderEditor()
{
    ImGui::Begin("GeoFPS Control Panel");
    ImGui::Text("Custom terrain-mapping FPS engine starter");
    ImGui::Separator();

    if (!m_StatusMessage.empty())
    {
        ImGui::TextWrapped("Status: %s", m_StatusMessage.c_str());
        ImGui::Separator();
    }

    char worldNameBuffer[256];
    std::snprintf(worldNameBuffer, sizeof(worldNameBuffer), "%s", m_WorldName.c_str());
    if (ImGui::InputText("World Name", worldNameBuffer, sizeof(worldNameBuffer)))
    {
        m_WorldName = worldNameBuffer;
    }

    char worldPathBuffer[512];
    std::snprintf(worldPathBuffer, sizeof(worldPathBuffer), "%s", m_WorldFilePath.c_str());
    if (ImGui::InputText("World File Path", worldPathBuffer, sizeof(worldPathBuffer)))
    {
        m_WorldFilePath = worldPathBuffer;
    }

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

    ImGui::Separator();
    const glm::vec3 cameraPos = m_Camera.GetPosition();
    ImGui::Text("Terrain datasets: %zu", m_TerrainDatasets.size());
    ImGui::Text("Imported assets: %zu", m_ImportedAssets.size());
    ImGui::Text("Points loaded: %zu", m_TerrainPoints.size());
    ImGui::Text("Camera: %.2f, %.2f, %.2f", cameraPos.x, cameraPos.y, cameraPos.z);
    ImGui::Text("Move Speed: %.1f (current: %.1f)", m_FPSController.GetMoveSpeed(), m_FPSController.GetCurrentSpeed());
    ImGui::Text("Mouse capture: %s", m_MouseCaptured ? "on" : "off");
    ImGui::Text("Esc releases mouse, Tab recaptures, +/- adjusts speed, Shift sprints");
    RenderMiniMap();
    ImGui::End();
}

void Application::RenderSunControls()
{
    ImGui::Separator();
    ImGui::Text("Sun And Illumination");

    const CityPreset& selectedCityPreset = kCityPresets[static_cast<size_t>(std::clamp(
        m_SelectedCityPresetIndex, 0, static_cast<int>(kCityPresets.size()) - 1))];
    if (ImGui::BeginCombo("Major City", selectedCityPreset.name))
    {
        for (int index = 0; index < static_cast<int>(kCityPresets.size()); ++index)
        {
            const bool isSelected = index == m_SelectedCityPresetIndex;
            if (ImGui::Selectable(kCityPresets[static_cast<size_t>(index)].name, isSelected))
            {
                m_SelectedCityPresetIndex = index;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (m_SelectedCityPresetIndex > 0 && ImGui::Button("Apply City Preset"))
    {
        const CityPreset& city = kCityPresets[static_cast<size_t>(m_SelectedCityPresetIndex)];
        m_GeoReference.originLatitude = city.latitude;
        m_GeoReference.originLongitude = city.longitude;
        m_SunSettings.utcOffsetHours = SuggestedUtcOffsetForCityPreset(m_SelectedCityPresetIndex, m_SunSettings);
        m_StatusMessage = std::string("Applied city preset: ") + city.name;
    }
    if (m_SelectedCityPresetIndex > 0)
    {
        ImGui::Text("Preset lat/lon: %.4f, %.4f", selectedCityPreset.latitude, selectedCityPreset.longitude);
    }

    ImGui::Checkbox("Use Geographic Sun", &m_SunSettings.useGeographicSun);
    if (m_SunSettings.useGeographicSun)
    {
        ImGui::InputInt("Sun Year", &m_SunSettings.year);
        m_SunSettings.year = std::max(m_SunSettings.year, 1900);
        ImGui::SliderInt("Sun Month", &m_SunSettings.month, 1, 12);
        const int maxDay = DaysInMonth(m_SunSettings.year, m_SunSettings.month);
        m_SunSettings.day = std::clamp(m_SunSettings.day, 1, maxDay);
        ImGui::SliderInt("Sun Day", &m_SunSettings.day, 1, maxDay);
        ImGui::SliderFloat("Local Time", &m_SunSettings.localTimeHours, 0.0f, 24.0f, "%.2f h");
        ImGui::SliderFloat("UTC Offset", &m_SunSettings.utcOffsetHours, -12.0f, 14.0f, "%.1f h");
    }
    else
    {
        ImGui::SliderFloat("Sun Azimuth", &m_SunSettings.manualAzimuthDegrees, 0.0f, 360.0f, "%.1f deg");
        ImGui::SliderFloat("Sun Elevation", &m_SunSettings.manualElevationDegrees, -10.0f, 90.0f, "%.1f deg");
    }

    ImGui::SliderFloat("Sun Strength", &m_SunSettings.illuminance, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Ambient Light", &m_SunSettings.ambientStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sky Brightness", &m_SunSettings.skyBrightness, 0.2f, 2.0f, "%.2f");

    const SunParameters sun = ComputeSunParameters(m_GeoReference, m_SunSettings);
    ImGui::Text("Computed Azimuth: %.1f deg", sun.azimuthDegrees);
    ImGui::Text("Computed Elevation: %.1f deg", sun.elevationDegrees);
    ImGui::Text("Solar Intensity: %.2f", sun.intensity);
}

} // namespace GeoFPS
