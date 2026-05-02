#include "Core/Application.h"
#include "Core/ApplicationInternal.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/NativeFileDialog.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationInternal;
using namespace ApplicationUIInternal;
namespace
{
constexpr float kMinimumVisibleProfileWorldThicknessMeters = 3.0f;

glm::dvec3 TerrainCoordinateToLocal(const TerrainDataset& dataset, double latitude, double longitude, double height)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {latitude, height, longitude};
    }

    return GeoConverter(dataset.geoReference).ToLocal(latitude, longitude, height);
}

glm::dvec3 ProfileLocalToTerrainCoordinate(const TerrainDataset* dataset,
                                           const GeoConverter& converter,
                                           const glm::dvec3& localPosition)
{
    if (dataset != nullptr && dataset->settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {localPosition.x, localPosition.z, localPosition.y};
    }

    return converter.ToGeographic(localPosition);
}

const char* TerrainCoordinateModeLabel(TerrainCoordinateMode mode)
{
    if (mode == TerrainCoordinateMode::LocalMeters)
    {
        return "Local meters X/Z/height";
    }
    if (mode == TerrainCoordinateMode::Projected)
    {
        return "Projected CRS meters";
    }
    return "Geographic lat/lon/height";
}

bool TerrainDatasetHasCoverage(const TerrainDataset& dataset)
{
    return dataset.visible && dataset.loaded && dataset.bounds.valid;
}

bool TerrainDatasetContainsCoordinate(const TerrainDataset& dataset, double latitude, double longitude)
{
    if (!dataset.bounds.valid)
    {
        return false;
    }

    return latitude >= dataset.bounds.minLatitude && latitude <= dataset.bounds.maxLatitude &&
           longitude >= dataset.bounds.minLongitude && longitude <= dataset.bounds.maxLongitude;
}
} // namespace

void Application::SetupImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (m_Diagnostics.platformViewportsEnabled)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    LoadProfessionalUiFont();
    ApplyProfessionalImGuiStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
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
    TerrainBounds bounds;
    std::vector<std::pair<const TerrainDataset*, TerrainBounds>> atlasTerrains;
    atlasTerrains.reserve(m_TerrainDatasets.size());
    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        if (!TerrainDatasetHasCoverage(dataset))
        {
            continue;
        }

        TerrainBounds terrainBounds = ToTerrainBounds(dataset.bounds);
        bounds.minLatitude = std::min(bounds.minLatitude, terrainBounds.minLatitude);
        bounds.maxLatitude = std::max(bounds.maxLatitude, terrainBounds.maxLatitude);
        bounds.minLongitude = std::min(bounds.minLongitude, terrainBounds.minLongitude);
        bounds.maxLongitude = std::max(bounds.maxLongitude, terrainBounds.maxLongitude);
        bounds.minHeight = std::min(bounds.minHeight, terrainBounds.minHeight);
        bounds.maxHeight = std::max(bounds.maxHeight, terrainBounds.maxHeight);
        atlasTerrains.emplace_back(&dataset, terrainBounds);
    }

    if (atlasTerrains.empty())
    {
        ImGui::Text("Load visible terrain datasets to use the world atlas.");
        return;
    }

    const TerrainDataset* activeTerrainForAtlas = GetActiveTerrainDataset();
    const bool atlasUsesLocalMeters = activeTerrainForAtlas != nullptr &&
                                      activeTerrainForAtlas->settings.coordinateMode == TerrainCoordinateMode::LocalMeters;
    GeoConverter converter(m_GeoReference);
    const glm::vec3 cameraPosition = m_Camera.GetPosition();
    const glm::dvec3 cameraGeo = atlasUsesLocalMeters ?
                                     glm::dvec3(cameraPosition.x, cameraPosition.z, cameraPosition.y) :
                                     converter.ToGeographic({static_cast<double>(cameraPosition.x),
                                                             static_cast<double>(cameraPosition.y),
                                                             static_cast<double>(cameraPosition.z)});

    ImGui::Separator();
    ImGui::Text("World Atlas");
    ImGui::Text("%s: %.6f", atlasUsesLocalMeters ? "X" : "Lat", cameraGeo.x);
    ImGui::Text("%s: %.6f", atlasUsesLocalMeters ? "Z" : "Lon", cameraGeo.y);
    ImGui::Text("Height: %.2f m", cameraGeo.z);

    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float mapSize = std::min(std::max(availableWidth, 220.0f), 420.0f);
    const ImVec2 mapTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 mapBottomRight(mapTopLeft.x + mapSize, mapTopLeft.y + mapSize);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(mapTopLeft, mapBottomRight, IM_COL32(20, 28, 36, 255), 6.0f);
    drawList->AddRect(mapTopLeft, mapBottomRight, IM_COL32(120, 140, 160, 255), 6.0f, 0, 2.0f);

    const double rawLongitudeSpan = std::max(bounds.maxLongitude - bounds.minLongitude, 1e-9);
    const double rawLatitudeSpan = std::max(bounds.maxLatitude - bounds.minLatitude, 1e-9);
    const double atlasCenterLongitude = 0.5 * (bounds.minLongitude + bounds.maxLongitude);
    const double atlasCenterLatitude = 0.5 * (bounds.minLatitude + bounds.maxLatitude);
    double displayLongitudeSpan = rawLongitudeSpan;
    double displayLatitudeSpan = rawLatitudeSpan;
    if (displayLongitudeSpan > displayLatitudeSpan)
    {
        displayLatitudeSpan = displayLongitudeSpan;
    }
    else
    {
        displayLongitudeSpan = displayLatitudeSpan;
    }
    constexpr double kAtlasPaddingFactor = 1.18;
    displayLongitudeSpan *= kAtlasPaddingFactor;
    displayLatitudeSpan *= kAtlasPaddingFactor;
    const double displayMinLongitude = atlasCenterLongitude - (displayLongitudeSpan * 0.5);
    const double displayMinLatitude = atlasCenterLatitude - (displayLatitudeSpan * 0.5);

    const auto mapPointFromGeo = [&](double latitude, double longitude) {
        const float u = static_cast<float>((longitude - displayMinLongitude) / displayLongitudeSpan);
        const float v = static_cast<float>((latitude - displayMinLatitude) / displayLatitudeSpan);
        return ImVec2(mapTopLeft.x + (std::clamp(u, 0.0f, 1.0f) * mapSize),
                      mapBottomRight.y - (std::clamp(v, 0.0f, 1.0f) * mapSize));
    };

    const float cameraMapU = static_cast<float>((cameraGeo.y - displayMinLongitude) / displayLongitudeSpan);
    const float cameraMapV = static_cast<float>((cameraGeo.x - displayMinLatitude) / displayLatitudeSpan);
    const bool cameraInsideBounds = cameraMapU >= 0.0f && cameraMapU <= 1.0f && cameraMapV >= 0.0f && cameraMapV <= 1.0f;
    const ImVec2 cameraPoint = mapPointFromGeo(cameraGeo.x, cameraGeo.y);
    const float metersPerDegreeLongitude = atlasUsesLocalMeters ?
                                               static_cast<float>(std::max(bounds.maxLongitude - bounds.minLongitude, 1.0)) :
                                               static_cast<float>(std::max(
                                                   std::abs(converter.ToLocal(bounds.minLatitude,
                                                                              bounds.maxLongitude,
                                                                              m_GeoReference.originHeight).x -
                                                            converter.ToLocal(bounds.minLatitude,
                                                                              bounds.minLongitude,
                                                                              m_GeoReference.originHeight).x),
                                                   1.0));
    const float metersPerDegreeLatitude = atlasUsesLocalMeters ?
                                              static_cast<float>(std::max(bounds.maxLatitude - bounds.minLatitude, 1.0)) :
                                              static_cast<float>(std::max(
                                                  std::abs(converter.ToLocal(bounds.maxLatitude,
                                                                             bounds.minLongitude,
                                                                             m_GeoReference.originHeight).z -
                                                           converter.ToLocal(bounds.minLatitude,
                                                                             bounds.minLongitude,
                                                                             m_GeoReference.originHeight).z),
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

    for (size_t index = 0; index < atlasTerrains.size(); ++index)
    {
        const TerrainDataset* dataset = atlasTerrains[index].first;
        const TerrainBounds& terrainBounds = atlasTerrains[index].second;
        const bool active = dataset == GetActiveTerrainDataset();
        const ImVec2 terrainTopLeft = mapPointFromGeo(terrainBounds.maxLatitude, terrainBounds.minLongitude);
        const ImVec2 terrainBottomRight = mapPointFromGeo(terrainBounds.minLatitude, terrainBounds.maxLongitude);
        const ImU32 fillColor = active ? IM_COL32(80, 170, 255, 70) :
                                       (index % 2 == 0 ? IM_COL32(90, 210, 150, 46) : IM_COL32(255, 198, 90, 46));
        const ImU32 outlineColor = active ? IM_COL32(115, 205, 255, 230) :
                                          (index % 2 == 0 ? IM_COL32(120, 230, 170, 190) : IM_COL32(255, 218, 120, 190));
        drawList->AddRectFilled(terrainTopLeft, terrainBottomRight, fillColor, 3.0f);
        drawList->AddRect(terrainTopLeft, terrainBottomRight, outlineColor, 3.0f, 0, active ? 2.0f : 1.2f);

        const ImVec2 labelPosition(terrainTopLeft.x + 5.0f, terrainTopLeft.y + 4.0f);
        if (labelPosition.x < mapBottomRight.x - 24.0f && labelPosition.y < mapBottomRight.y - 16.0f)
        {
            drawList->AddText(labelPosition, IM_COL32(232, 240, 248, 220), dataset->name.c_str());
        }
    }

    const glm::vec3 forward = m_Camera.GetForward();
    const glm::vec2 forward2D(forward.x, forward.z);
    const float forwardLength = glm::length(forward2D);
    glm::vec2 direction = forwardLength > 0.0001f ? (forward2D / forwardLength) : glm::vec2(0.0f, -1.0f);
    direction.y *= -1.0f;

    drawList->AddCircle(cameraPoint, visibleRangeRadiusPixels, IM_COL32(80, 180, 255, 120), 64, 1.5f);
    const ImVec2 directionPoint(cameraPoint.x + (direction.x * 16.0f), cameraPoint.y + (direction.y * 16.0f));
    drawList->AddCircleFilled(cameraPoint, 6.0f, cameraInsideBounds ? IM_COL32(255, 110, 64, 255) : IM_COL32(255, 196, 64, 255));
    drawList->AddLine(cameraPoint, directionPoint, IM_COL32(255, 210, 120, 255), 2.0f);

    ImGui::InvisibleButton("##navigator_map", ImVec2(mapSize, mapSize));
    if (ImGui::IsItemHovered())
    {
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const float mouseU = std::clamp((mousePosition.x - mapTopLeft.x) / mapSize, 0.0f, 1.0f);
        const float mouseV = std::clamp((mapBottomRight.y - mousePosition.y) / mapSize, 0.0f, 1.0f);
        const double targetLongitude = displayMinLongitude + (static_cast<double>(mouseU) * displayLongitudeSpan);
        const double targetLatitude = displayMinLatitude + (static_cast<double>(mouseV) * displayLatitudeSpan);

        const TerrainDataset* targetTerrain = nullptr;
        for (const auto& [dataset, terrainBounds] : atlasTerrains)
        {
            if (targetLatitude >= terrainBounds.minLatitude && targetLatitude <= terrainBounds.maxLatitude &&
                targetLongitude >= terrainBounds.minLongitude && targetLongitude <= terrainBounds.maxLongitude)
            {
                targetTerrain = dataset;
                break;
            }
        }
        const GeoReference& targetReference = targetTerrain != nullptr ? targetTerrain->geoReference : m_GeoReference;
        GeoConverter targetConverter(targetReference);
        float targetHeight = static_cast<float>(targetReference.originHeight);
        if (targetTerrain != nullptr && TerrainDatasetContainsCoordinate(*targetTerrain, targetLatitude, targetLongitude))
        {
            targetHeight = SampleTerrainHeightAt(*targetTerrain, targetLatitude, targetLongitude);
        }
        targetHeight += 2.0f;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const glm::dvec3 localTarget =
                targetTerrain != nullptr && targetTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters ?
                    glm::dvec3(targetLatitude, targetHeight, targetLongitude) :
                    targetConverter.ToLocal(targetLatitude, targetLongitude, targetHeight);
            QueueCameraTeleport(glm::vec3(static_cast<float>(localTarget.x),
                                           static_cast<float>(localTarget.y),
                                           static_cast<float>(localTarget.z)));
            m_StatusMessage = targetTerrain != nullptr ? "Moved camera from atlas: " + targetTerrain->name :
                                                         "Moved camera from atlas.";
        }

        drawList->AddCircle(ImVec2(mousePosition.x, mousePosition.y), 4.0f, IM_COL32(255, 255, 255, 160), 24, 1.5f);
        ImGui::SetTooltip("Click to move\n%s\n%s %.6f\n%s %.6f\nHeight %.2f m",
                          targetTerrain != nullptr ? targetTerrain->name.c_str() : "No terrain under cursor",
                          targetTerrain != nullptr && targetTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters ? "X" : "Lat",
                          targetLatitude,
                          targetTerrain != nullptr && targetTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters ? "Z" : "Lon",
                          targetLongitude,
                          targetHeight);
    }

    ImGui::Text("Visible terrainsets: %zu", atlasTerrains.size());
    ImGui::Text("Atlas fit: comfortable %.0f%% padding", (kAtlasPaddingFactor - 1.0) * 100.0);
    ImGui::Text("%s max: %.6f", atlasUsesLocalMeters ? "X" : "North", bounds.maxLatitude);
    ImGui::Text("%s min: %.6f", atlasUsesLocalMeters ? "X" : "South", bounds.minLatitude);
    ImGui::Text("%s min: %.6f", atlasUsesLocalMeters ? "Z" : "West", bounds.minLongitude);
    ImGui::Text("%s max: %.6f", atlasUsesLocalMeters ? "Z" : "East", bounds.maxLongitude);
    ImGui::Text("Atlas height: %.2f m to %.2f m", bounds.minHeight, bounds.maxHeight);
    ImGui::Text("View range: %.0f m to %.0f m", m_Camera.GetNearClip(), m_Camera.GetFarClip());
    ImGui::Text("Camera on atlas: %s", cameraInsideBounds ? "inside atlas bounds" : "outside atlas bounds");
}

void Application::RenderMiniMapWindow()
{
    if (!m_ShowMiniMapWindow)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(360.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain Minimap", &m_ShowMiniMapWindow))
    {
        ImGui::End();
        return;
    }

    RenderMiniMap();
    ImGui::End();
}

void Application::RenderCameraHud()
{
    // Stats overlay is always evaluated so it persists regardless of active tab.
    RenderDiagnosticsOverlay();

    if (m_TerrainPoints.empty())
    {
        return;
    }

    GeoConverter converter(m_GeoReference);
    const glm::vec3 cameraPosition = m_Camera.GetPosition();
    const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    const bool localMetersMode = activeTerrain != nullptr &&
                                 activeTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters;
    const glm::dvec3 cameraGeo = localMetersMode ?
                                     glm::dvec3(cameraPosition.x, cameraPosition.z, cameraPosition.y) :
                                     converter.ToGeographic({static_cast<double>(cameraPosition.x),
                                                             static_cast<double>(cameraPosition.y),
                                                             static_cast<double>(cameraPosition.z)});

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("Camera HUD", nullptr, flags))
    {
        ImGui::Text("%s %.6f", localMetersMode ? "X" : "Lat", cameraGeo.x);
        ImGui::Text("%s %.6f", localMetersMode ? "Z" : "Lon", cameraGeo.y);
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
        ImGui::PushID(i);
        ImGui::Checkbox("##terrain_visible", &dataset.visible);
        ImGui::SameLine();
        if (ImGui::Selectable(dataset.name.c_str(), selected) && !selected)
        {
            ActivateTerrainDataset(i);
            activeTerrain = GetActiveTerrainDataset();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(dataset.loaded && (dataset.hasTileManifest || dataset.mesh != nullptr) ? "loaded" : "not loaded");
        ImGui::PopID();
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
    ImGui::SameLine();
    if (ImGui::Button("Browse##terrain_csv"))
    {
        const std::string selectedPath = OpenNativeFileDialog("Load Terrain CSV", {{"CSV", {".csv"}}, {"Text", {".txt"}}});
        if (!selectedPath.empty())
        {
            activeTerrain->path = selectedPath;
        }
        else
        {
            m_StatusMessage = "No terrain CSV selected.";
        }
    }
    if (!activeTerrain->path.empty() && !PathExists(activeTerrain->path))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f), "Terrain file does not exist.");
    }

    // ── Tile manifest ─────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Checkbox("Use Tile Manifest", &activeTerrain->hasTileManifest);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Load terrain from a JSON manifest file that references\n"
                          "multiple CSV tiles instead of a single CSV file.");
    if (activeTerrain->hasTileManifest)
    {
        char manifestBuffer[512];
        std::snprintf(manifestBuffer, sizeof(manifestBuffer),
                      "%s", activeTerrain->tileManifestPath.c_str());
        ImGui::SetNextItemWidth(-60.0f);
        if (ImGui::InputText("##manifest_path", manifestBuffer, sizeof(manifestBuffer)))
            activeTerrain->tileManifestPath = manifestBuffer;
        ImGui::SameLine();
        if (ImGui::Button("Browse##manifest"))
        {
            const std::string sel = OpenNativeFileDialog(
                "Load Tile Manifest", {{"JSON Manifest", {".json"}}});
            if (!sel.empty())
                activeTerrain->tileManifestPath = sel;
        }
        ImGui::TextDisabled("Manifest (JSON)");
        if (!activeTerrain->tileManifestPath.empty() &&
            !PathExists(activeTerrain->tileManifestPath))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f),
                               "Manifest file does not exist.");
        }
    }
    ImGui::Spacing();

    if (ImGui::Button("Load Active Terrain"))
    {
        StartTerrainBuildJob(m_ActiveTerrainIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Terrain Dataset"))
    {
        TerrainDataset dataset;
        dataset.name = "Terrain " + std::to_string(m_TerrainDatasets.size() + 1);
        dataset.path = activeTerrain->path;
        dataset.settings = activeTerrain->settings;
        dataset.overlays.push_back(OverlayEntry {});
        m_TerrainDatasets.push_back(std::move(dataset));
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
    if (ImGui::Button("Draft 128"))
    {
        m_TerrainSettings.gridResolutionX = 128;
        m_TerrainSettings.gridResolutionZ = 128;
    }
    ImGui::SameLine();
    if (ImGui::Button("Balanced 256"))
    {
        m_TerrainSettings.gridResolutionX = 256;
        m_TerrainSettings.gridResolutionZ = 256;
    }
    ImGui::SameLine();
    if (ImGui::Button("High 384"))
    {
        m_TerrainSettings.gridResolutionX = 384;
        m_TerrainSettings.gridResolutionZ = 384;
    }
    ImGui::SameLine();
    if (ImGui::Button("Max 512"))
    {
        m_TerrainSettings.gridResolutionX = 512;
        m_TerrainSettings.gridResolutionZ = 512;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ultra 1024"))
    {
        m_TerrainSettings.gridResolutionX = 1024;
        m_TerrainSettings.gridResolutionZ = 1024;
    }
    ImGui::SliderInt("Grid X", &m_TerrainSettings.gridResolutionX, 8, 1024);
    ImGui::SliderInt("Grid Z", &m_TerrainSettings.gridResolutionZ, 8, 1024);
    ImGui::SliderFloat("Height Scale", &m_TerrainSettings.heightScale, 0.1f, 5.0f);
    ImGui::SliderInt("Smoothing Passes", &m_TerrainSettings.smoothingPasses, 0, 4);
    ImGui::SliderInt("Import Sample Step", &m_TerrainSettings.importSampleStep, 1, 32);
    ImGui::SliderInt("Chunk Resolution", &m_TerrainSettings.chunkResolution, 16, 128);
    ImGui::Checkbox("Color By Height", &m_TerrainSettings.colorByHeight);
    ImGui::Checkbox("Auto Height Color Range", &m_TerrainSettings.autoHeightColorRange);
    if (!m_TerrainSettings.autoHeightColorRange)
    {
        ImGui::InputFloat("Height Color Min", &m_TerrainSettings.heightColorMin, 0.0f, 0.0f, "%.2f");
        ImGui::InputFloat("Height Color Max", &m_TerrainSettings.heightColorMax, 0.0f, 0.0f, "%.2f");
    }
    ImGui::ColorEdit3("Low Height Color", &m_TerrainSettings.lowHeightColor.x);
    ImGui::ColorEdit3("Mid Height Color", &m_TerrainSettings.midHeightColor.x);
    ImGui::ColorEdit3("High Height Color", &m_TerrainSettings.highHeightColor.x);
    if (ImGui::BeginCombo("Coordinate Mode", TerrainCoordinateModeLabel(m_TerrainSettings.coordinateMode)))
    {
        if (ImGui::Selectable("Geographic lat/lon/height", m_TerrainSettings.coordinateMode == TerrainCoordinateMode::Geographic))
        {
            m_TerrainSettings.coordinateMode = TerrainCoordinateMode::Geographic;
            m_TerrainSettings.crs = GeoConverter::ParseCrs("EPSG:4326");
        }
        if (ImGui::Selectable("Local meters X/Z/height", m_TerrainSettings.coordinateMode == TerrainCoordinateMode::LocalMeters))
        {
            m_TerrainSettings.coordinateMode = TerrainCoordinateMode::LocalMeters;
            m_TerrainSettings.crs = GeoConverter::ParseCrs("LOCAL_METERS");
        }
                if (ImGui::Selectable("Projected CRS meters", m_TerrainSettings.coordinateMode == TerrainCoordinateMode::Projected))
                {
                    m_TerrainSettings.coordinateMode = TerrainCoordinateMode::Projected;
                    if (m_TerrainSettings.crs.kind == CrsKind::GeographicWgs84)
                    {
                        m_TerrainSettings.crs = GeoConverter::ParseCrs("EPSG:3857");
                    }
                }
        ImGui::EndCombo();
    }
    char crsBuffer[128];
    std::snprintf(crsBuffer, sizeof(crsBuffer), "%s", m_TerrainSettings.crs.id.c_str());
    if (ImGui::InputText("CRS", crsBuffer, sizeof(crsBuffer)))
    {
        m_TerrainSettings.crs = GeoConverter::ParseCrs(crsBuffer);
    }

    if (ImGui::Button("Reload Terrain"))
    {
        StartTerrainBuildJob(m_ActiveTerrainIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild Mesh"))
    {
        activeTerrain->geoReference = m_GeoReference;
        activeTerrain->settings = m_TerrainSettings;
        RebuildTerrain();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Visible Terrains"))
    {
        int loadedCount = 0;
        int overlayCount = 0;
        for (TerrainDataset& dataset : m_TerrainDatasets)
        {
            if (!dataset.visible)
            {
                continue;
            }
            if (dataset.loaded)
            {
                ++loadedCount;
                for (OverlayEntry& overlay : dataset.overlays)
                {
                    if (overlay.image.enabled && LoadOverlayImage(overlay))
                    {
                        ++overlayCount;
                    }
                }
            }
            else if (StartTerrainBuildJob(static_cast<int>(&dataset - m_TerrainDatasets.data())))
            {
                ++loadedCount;
            }
        }
        m_StatusMessage = "Loaded or queued visible terrains: " + std::to_string(loadedCount) +
                          "  overlays: " + std::to_string(overlayCount);
    }

    activeTerrain->geoReference = m_GeoReference;
    activeTerrain->settings = m_TerrainSettings;

    if (ImGui::CollapsingHeader("Terrain Metadata", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (activeTerrain->loaded && !activeTerrain->points.empty())
        {
            const TerrainBounds bounds = ToTerrainBounds(activeTerrain->bounds);
            ImGui::Text("Points: %zu", activeTerrain->points.size());
            ImGui::Text("Latitude: %.8f to %.8f", bounds.minLatitude, bounds.maxLatitude);
            ImGui::Text("Longitude: %.8f to %.8f", bounds.minLongitude, bounds.maxLongitude);
            ImGui::Text("Height: %.2f m to %.2f m", bounds.minHeight, bounds.maxHeight);
            ImGui::Text("Grid: %d x %d  Height scale: %.2f",
                        activeTerrain->settings.gridResolutionX,
                        activeTerrain->settings.gridResolutionZ,
                        activeTerrain->settings.heightScale);
            ImGui::Text("Import step: %d  Chunks: %zu  CRS: %s",
                        activeTerrain->settings.importSampleStep,
                        activeTerrain->chunks.size(),
                        activeTerrain->settings.crs.id.c_str());
            ImGui::TextWrapped("Coordinate mode: %s.", TerrainCoordinateModeLabel(activeTerrain->settings.coordinateMode));
        }
        else
        {
            ImGui::Text("Load the active terrain to inspect metadata.");
        }
    }
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
            ImGui::PushID(i);
            ImGui::Checkbox("##overlay_enabled", &overlay.image.enabled);
            ImGui::SameLine();
            if (ImGui::Selectable(overlay.name.c_str(), selected) && !selected)
            {
                activeTerrain->activeOverlayIndex = i;
                LoadActiveOverlayImage();
                activeOverlay = GetActiveOverlayEntry();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(overlay.image.loaded && overlay.texture.IsLoaded() ? "loaded" : "not loaded");
            ImGui::PopID();
        }

        if (ImGui::Button("Add Overlay Slot"))
        {
            OverlayEntry overlay;
            overlay.name = "Overlay " + std::to_string(activeTerrain->overlays.size() + 1);
            ResetOverlayToTerrainBounds(overlay.image);
            activeTerrain->overlays.push_back(std::move(overlay));
            activeTerrain->activeOverlayIndex = static_cast<int>(activeTerrain->overlays.size()) - 1;
            activeOverlay = GetActiveOverlayEntry();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Active Overlay"))
        {
            DeleteActiveOverlay();
            activeOverlay = GetActiveOverlayEntry();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Enabled Overlays"))
        {
            int loadedCount = 0;
            for (OverlayEntry& overlay : activeTerrain->overlays)
            {
                if (overlay.image.enabled && LoadOverlayImage(overlay))
                {
                    ++loadedCount;
                }
            }
            m_StatusMessage = "Loaded enabled overlays: " + std::to_string(loadedCount);
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
        ImGui::SameLine();
        if (ImGui::Button("Browse##overlay_image"))
        {
            const std::string selectedPath = OpenNativeFileDialog("Load Aerial Overlay Image",
                                                                  {{"Images", {".png", ".jpg", ".jpeg", ".bmp", ".tga"}}});
            if (!selectedPath.empty())
            {
                activeOverlay->image.imagePath = selectedPath;
            }
            else
            {
                m_StatusMessage = "No overlay image selected.";
            }
        }
        if (!activeOverlay->image.imagePath.empty() && !PathExists(activeOverlay->image.imagePath))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f), "Overlay image file does not exist.");
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
            ImGui::Text("Image Size: %d x %d", activeOverlay->texture.GetWidth(), activeOverlay->texture.GetHeight());
        }
    }

    ImGui::End();
}

void Application::RenderTerrainProfilesWindow()
{
    if (!m_ShowTerrainProfilesWindow)
    {
        return;
    }

    if (m_TerrainProfiles.empty())
    {
        TerrainProfile profile;
        profile.name = "Profile 1";
        m_TerrainProfiles.push_back(profile);
        m_ActiveTerrainProfileIndex = 0;
    }

    m_ActiveTerrainProfileIndex = std::clamp(m_ActiveTerrainProfileIndex, 0, static_cast<int>(m_TerrainProfiles.size()) - 1);

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain Profiles", &m_ShowTerrainProfilesWindow))
    {
        ImGui::End();
        return;
    }

    TerrainProfile& activeProfile = m_TerrainProfiles[static_cast<size_t>(m_ActiveTerrainProfileIndex)];
    EnsureTerrainProfileHasTerrainSelection(activeProfile);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float maxDetailWidth = std::max(280.0f, availableWidth * 0.62f);
    const float detailWidth = availableWidth > 760.0f ? std::clamp(m_ProfileDetailsWidth, 280.0f, maxDetailWidth) : 0.0f;
    const float mapPaneWidth = detailWidth > 0.0f ? availableWidth - detailWidth - ImGui::GetStyle().ItemSpacing.x : availableWidth;

    if (detailWidth > 0.0f)
    {
        ImGui::BeginChild("##profile_workspace",
                          ImVec2(mapPaneWidth, 0.0f),
                          false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        RenderTerrainProfileMap(activeProfile);
        RenderTerrainProfileGraph(activeProfile);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::Button("##profile_details_splitter", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive())
        {
            m_ProfileDetailsWidth = std::clamp(m_ProfileDetailsWidth - ImGui::GetIO().MouseDelta.x, 280.0f, maxDetailWidth);
        }
        ImGui::SameLine();
        ImGui::BeginChild("##profile_details",
                          ImVec2(detailWidth, 0.0f),
                          true,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        RenderTerrainProfileDetails(activeProfile);
        ImGui::EndChild();
    }
    else
    {
        ImGui::BeginChild("##profile_single_column",
                          ImVec2(0.0f, 0.0f),
                          false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        RenderTerrainProfileMap(activeProfile);
        RenderTerrainProfileGraph(activeProfile);
        RenderTerrainProfileDetails(activeProfile);
        ImGui::EndChild();
    }

    ImGui::End();
}

void Application::RenderWorldTerrainProfiles()
{
    if (!m_LineShader)
    {
        return;
    }

    m_LineShader->Bind();
    m_LineShader->SetMat4("uView", GetRenderViewMatrix());
    m_LineShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());

    if (m_ProfileLineVao == 0)
    {
        glGenVertexArrays(1, &m_ProfileLineVao);
        glGenBuffers(1, &m_ProfileLineVbo);
        glBindVertexArray(m_ProfileLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_ProfileLineVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    }
    else
    {
        glBindVertexArray(m_ProfileLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_ProfileLineVbo);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);

    std::vector<glm::vec3> lineVertices;
    std::vector<glm::vec3> ribbonVertices;
    for (const TerrainProfile& profile : m_TerrainProfiles)
    {
        if (!profile.visible || !profile.showInWorld || profile.vertices.size() < 2)
        {
            continue;
        }

        for (const TerrainDataset& dataset : m_TerrainDatasets)
        {
            if (!TerrainDatasetHasCoverage(dataset) || !TerrainProfileIncludesTerrain(profile, dataset))
            {
                continue;
            }

            if (!dataset.bounds.valid)
            {
                continue;
            }

            const TerrainBounds bounds = ToTerrainBounds(dataset.bounds);
            GeoConverter converter(dataset.geoReference);
            bool touchesTerrain = false;
            for (const TerrainProfileVertex& vertex : profile.vertices)
            {
                const TerrainProfileVertex geoVertex =
                    ProfileVertexAsGeographic(profile, vertex, converter, dataset.settings.coordinateMode);
                if (geoVertex.latitude >= bounds.minLatitude && geoVertex.latitude <= bounds.maxLatitude &&
                    geoVertex.longitude >= bounds.minLongitude && geoVertex.longitude <= bounds.maxLongitude)
                {
                    touchesTerrain = true;
                    break;
                }
            }
            if (!touchesTerrain)
            {
                continue;
            }

            std::vector<TerrainProfileSample> samples = profile.samples;
            if (!dataset.hasTileManifest && dataset.heightGrid.IsValid())
            {
                const double worldSampleSpacingMeters = std::clamp(static_cast<double>(profile.sampleSpacingMeters), 0.5, 3.0);
                samples = SampleTerrainProfile(profile.vertices,
                                               dataset.heightGrid,
                                               converter,
                                               worldSampleSpacingMeters,
                                               profile.useLocalCoordinates,
                                               dataset.settings.coordinateMode);
            }
            if (samples.size() < 2)
            {
                continue;
            }

            lineVertices.clear();
            lineVertices.reserve(samples.size());
            std::vector<bool> validLineVertices;
            validLineVertices.reserve(samples.size());
            const glm::dvec3 terrainTranslation = GetDatasetWorldTranslation(dataset);
            for (const TerrainProfileSample& sample : samples)
            {
                const glm::dvec3 local =
                    TerrainCoordinateToLocal(dataset, sample.latitude, sample.longitude, dataset.geoReference.originHeight);
                const float localHeight = sample.valid ?
                                              SampleRenderedTerrainLocalHeightAt(dataset, sample.latitude, sample.longitude) +
                                                  profile.worldGroundOffsetMeters :
                                              0.0f;
                const glm::dvec3 worldPosition(local.x + terrainTranslation.x,
                                                static_cast<double>(localHeight) + terrainTranslation.y,
                                                local.z + terrainTranslation.z);
                lineVertices.emplace_back(ToRenderRelative(worldPosition));
                validLineVertices.push_back(sample.valid);
            }

            ribbonVertices.clear();
            ribbonVertices.reserve((lineVertices.size() - 1) * 6u);
            const float worldThicknessMeters =
                std::clamp(profile.worldThicknessMeters, kMinimumVisibleProfileWorldThicknessMeters, 250.0f);
            const glm::vec3 cameraPosition(0.0f);
            for (size_t index = 0; index + 1 < lineVertices.size(); ++index)
            {
                if (!validLineVertices[index] || !validLineVertices[index + 1])
                {
                    continue;
                }
                AppendProfileRibbonSegment(ribbonVertices, lineVertices[index], lineVertices[index + 1], cameraPosition, worldThicknessMeters);
            }
            if (ribbonVertices.empty())
            {
                continue;
            }

            m_LineShader->SetVec4("uColor", profile.color);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(ribbonVertices.size() * sizeof(glm::vec3)),
                         ribbonVertices.data(),
                         GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(ribbonVertices.size()));
        }
    }

    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Application::RenderTerrainProfileDetails(TerrainProfile& activeProfile)
{
    ImGui::SliderFloat("Options Width", &m_ProfileDetailsWidth, 280.0f, 720.0f, "%.0f px");

    if (ImGui::CollapsingHeader("Profiles", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Profiles");
    for (int index = 0; index < static_cast<int>(m_TerrainProfiles.size()); ++index)
    {
        TerrainProfile& profile = m_TerrainProfiles[static_cast<size_t>(index)];
        ImGui::PushID(index);
        ImGui::Checkbox("##profile_visible", &profile.visible);
        ImGui::SameLine();
        const bool selected = index == m_ActiveTerrainProfileIndex;
        if (ImGui::Selectable(profile.name.c_str(), selected))
        {
            m_ActiveTerrainProfileIndex = index;
            m_SelectedProfileVertexIndex = -1;
            m_SelectedProfileSampleIndex = -1;
            m_HoveredProfileSampleIndex = -1;
        }
        ImGui::PopID();
    }

        if (ImGui::Button("New Profile"))
        {
            TerrainProfile profile;
            profile.name = "Profile " + std::to_string(m_TerrainProfiles.size() + 1);
            m_TerrainProfiles.push_back(std::move(profile));
            m_ActiveTerrainProfileIndex = static_cast<int>(m_TerrainProfiles.size()) - 1;
            m_SelectedProfileVertexIndex = -1;
            m_SelectedProfileSampleIndex = -1;
            m_HoveredProfileSampleIndex = -1;
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Profile") && m_TerrainProfiles.size() > 1)
        {
            m_TerrainProfiles.erase(m_TerrainProfiles.begin() + m_ActiveTerrainProfileIndex);
            m_ActiveTerrainProfileIndex = std::clamp(m_ActiveTerrainProfileIndex, 0, static_cast<int>(m_TerrainProfiles.size()) - 1);
            m_SelectedProfileVertexIndex = -1;
            m_SelectedProfileSampleIndex = -1;
            m_HoveredProfileSampleIndex = -1;
            return;
        }

        char profileNameBuffer[256];
        std::snprintf(profileNameBuffer, sizeof(profileNameBuffer), "%s", activeProfile.name.c_str());
        if (ImGui::InputText("Profile Name", profileNameBuffer, sizeof(profileNameBuffer)))
        {
            activeProfile.name = profileNameBuffer;
        }

        bool useLocalCoordinates = activeProfile.useLocalCoordinates;
        if (ImGui::Checkbox("Use Local XYZ Coordinates", &useLocalCoordinates))
        {
            const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(activeProfile);
            GeoConverter converter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
            const TerrainCoordinateMode coordinateMode = profileTerrain != nullptr ?
                                                             profileTerrain->settings.coordinateMode :
                                                             TerrainCoordinateMode::Geographic;
            if (useLocalCoordinates && !activeProfile.useLocalCoordinates)
            {
                for (TerrainProfileVertex& vertex : activeProfile.vertices)
                {
                    vertex.localPosition = profileTerrain != nullptr ?
                                               TerrainCoordinateToLocal(*profileTerrain, vertex.latitude, vertex.longitude, 0.0) :
                                               converter.ToLocal(vertex.latitude, vertex.longitude, 0.0);
                }
            }
            else if (!useLocalCoordinates && activeProfile.useLocalCoordinates)
            {
                for (TerrainProfileVertex& vertex : activeProfile.vertices)
                {
                    const glm::dvec3 geographic =
                        coordinateMode == TerrainCoordinateMode::LocalMeters ?
                            glm::dvec3(vertex.localPosition.x, vertex.localPosition.z, vertex.localPosition.y) :
                            converter.ToGeographic(vertex.localPosition);
                    vertex.latitude = geographic.x;
                    vertex.longitude = geographic.y;
                }
            }
            activeProfile.useLocalCoordinates = useLocalCoordinates;
            RebuildTerrainProfileSamples(activeProfile);
        }
    }

    if (ImGui::CollapsingHeader("Drawing Style", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::ColorEdit4("Line Color", glm::value_ptr(activeProfile.color));
        ImGui::SliderFloat("Line Thickness", &activeProfile.thickness, 1.0f, 12.0f, "%.1f px");
        ImGui::SliderFloat("3D World Thickness (m)", &activeProfile.worldThicknessMeters, 1.0f, 250.0f, "%.1f m");
        ImGui::SliderFloat("3D Height Above Terrain (m)", &activeProfile.worldGroundOffsetMeters, 0.0f, 500.0f, "%.1f m");
        ImGui::Checkbox("Show Height Labels", &m_ShowProfileHeightLabels);
        ImGui::Checkbox("Show Sample Points", &m_ShowProfileSamples);
    }

    if (ImGui::CollapsingHeader("Terrains And World", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Use Profile On Terrains");
        for (TerrainDataset& dataset : m_TerrainDatasets)
        {
            ImGui::PushID(dataset.name.c_str());
            bool included = TerrainProfileIncludesTerrain(activeProfile, dataset);
            if (ImGui::Checkbox("##include_profile_terrain", &included))
            {
                SetTerrainProfileIncludesTerrain(activeProfile, dataset, included);
                RebuildTerrainProfileSamples(activeProfile);
            }
            ImGui::SameLine();
            ImGui::Text("%s%s", dataset.name.c_str(), dataset.loaded ? "" : " (not loaded)");
            ImGui::PopID();
        }
        if (ImGui::Button("Include Visible Terrains"))
        {
            activeProfile.includedTerrainNames.clear();
            for (const TerrainDataset& dataset : m_TerrainDatasets)
            {
                if (dataset.visible)
                {
                    activeProfile.includedTerrainNames.push_back(dataset.name);
                }
            }
            RebuildTerrainProfileSamples(activeProfile);
        }
        ImGui::SameLine();
        if (ImGui::Button("Include Active Only"))
        {
            activeProfile.includedTerrainNames.clear();
            const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
            if (activeTerrain != nullptr)
            {
                activeProfile.includedTerrainNames.push_back(activeTerrain->name);
            }
            RebuildTerrainProfileSamples(activeProfile);
        }

        if (ImGui::Checkbox("Show This Profile In World", &activeProfile.showInWorld))
        {
            EnsureTerrainProfileHasTerrainSelection(activeProfile);
            RebuildTerrainProfileSamples(activeProfile);
        }
        ImGui::SameLine();
        if (ImGui::Button("Send Active Line To World"))
        {
            EnsureTerrainProfileHasTerrainSelection(activeProfile);
            RebuildTerrainProfileSamples(activeProfile);
            if (activeProfile.vertices.size() < 2 || activeProfile.samples.size() < 2)
            {
                m_StatusMessage = "Draw at least two profile vertices before sending the line to the world.";
            }
            else
            {
                activeProfile.worldThicknessMeters =
                    std::max(activeProfile.worldThicknessMeters, kMinimumVisibleProfileWorldThicknessMeters);
                activeProfile.showInWorld = true;
                const int validSampleCount = static_cast<int>(std::count_if(activeProfile.samples.begin(),
                                                                            activeProfile.samples.end(),
                                                                            [](const TerrainProfileSample& sample) {
                                                                                return sample.valid;
                                                                            }));
                m_StatusMessage = "Sent terrain profile line to world: " + activeProfile.name + " (" +
                                  std::to_string(validSampleCount) + " valid samples, " +
                                  std::to_string(static_cast<int>(std::round(activeProfile.worldThicknessMeters))) +
                                  " m thick)";
            }
        }
        if (ImGui::Button("Send Visible Profiles To World"))
        {
            int sentCount = 0;
            for (TerrainProfile& profile : m_TerrainProfiles)
            {
                if (!profile.visible || profile.vertices.size() < 2)
                {
                    continue;
                }
                EnsureTerrainProfileHasTerrainSelection(profile);
                RebuildTerrainProfileSamples(profile);
                if (profile.samples.size() < 2)
                {
                    continue;
                }
                profile.worldThicknessMeters =
                    std::max(profile.worldThicknessMeters, kMinimumVisibleProfileWorldThicknessMeters);
                profile.showInWorld = true;
                ++sentCount;
            }
            m_StatusMessage = "Sent visible terrain profile lines to world: " + std::to_string(sentCount);
        }
    }

    if (ImGui::CollapsingHeader("Map And Isolines", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginCombo("Map Size", ProfileMapSizeModeLabel(m_ProfileMapSizeMode)))
        {
            const ProfileMapSizeMode modes[] = {ProfileMapSizeMode::Small, ProfileMapSizeMode::Medium, ProfileMapSizeMode::Large, ProfileMapSizeMode::Fill};
            for (ProfileMapSizeMode mode : modes)
            {
                const bool selected = mode == m_ProfileMapSizeMode;
                if (ImGui::Selectable(ProfileMapSizeModeLabel(mode), selected))
                {
                    m_ProfileMapSizeMode = mode;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("Show Isolines", &m_IsolineSettings.enabled))
        {
            MarkTerrainIsolinesDirty();
        }
        if (ImGui::Checkbox("Use GPU Isolines", &m_UseGpuIsolineGeneration))
        {
            MarkTerrainIsolinesDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Rebuild Isolines"))
        {
            RebuildTerrainIsolines();
        }
        ImGui::Text("Isoline Status: %s  Sample Grid: %s  Backend: %s",
                    m_TerrainIsolinesDirty ? "stale" : "current",
                    m_TerrainIsolineSampleGridDirty ? "stale" : "cached",
                    m_TerrainIsolinesUsedGpu ? "Metal GPU" : "CPU");
        if (ImGui::BeginCombo("Isoline Resolution", m_IsolineSettings.resolutionX == 32 ? "Fast 32 x 32" :
                                                        m_IsolineSettings.resolutionX == 128 ? "Medium 128 x 128" :
                                                        m_IsolineSettings.resolutionX == 256 ? "High 256 x 256" : "Low 64 x 64"))
        {
            struct ResolutionOption
            {
                const char* label;
                int value;
            };
            const ResolutionOption options[] = {{"Fast 32 x 32", 32}, {"Low 64 x 64", 64}, {"Medium 128 x 128", 128}, {"High 256 x 256", 256}};
            for (const ResolutionOption& option : options)
            {
                const bool selected = m_IsolineSettings.resolutionX == option.value;
                if (ImGui::Selectable(option.label, selected))
                {
                    m_IsolineSettings.resolutionX = option.value;
                    m_IsolineSettings.resolutionZ = option.value;
                    MarkTerrainIsolineSampleGridDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Auto Interval", &m_IsolineSettings.autoInterval))
        {
            MarkTerrainIsolinesDirty();
        }
        if (!m_IsolineSettings.autoInterval && ImGui::InputDouble("Interval (m)", &m_IsolineSettings.contourIntervalMeters, 1.0, 10.0, "%.2f"))
        {
            m_IsolineSettings.contourIntervalMeters = std::max(m_IsolineSettings.contourIntervalMeters, 0.1);
            MarkTerrainIsolinesDirty();
        }
        if (ImGui::SliderFloat("Isoline Thickness", &m_IsolineSettings.thickness, 0.5f, 5.0f, "%.1f px"))
        {
            m_IsolineSettings.thickness = std::max(m_IsolineSettings.thickness, 0.1f);
        }
        if (ImGui::SliderFloat("Isoline Opacity", &m_IsolineSettings.opacity, 0.1f, 1.0f, "%.2f"))
        {
            MarkTerrainIsolinesDirty();
        }
    }

    if (ImGui::CollapsingHeader("Sampling", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const int invalidSampleCount = static_cast<int>(std::count_if(activeProfile.samples.begin(),
                                                                      activeProfile.samples.end(),
                                                                      [](const TerrainProfileSample& sample) {
                                                                          return !sample.valid;
                                                                      }));
        if (invalidSampleCount > 0)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                               "%d sample(s) are outside terrain coverage. Invalid segments are skipped in 3D/graph output and flagged on export.",
                               invalidSampleCount);
        }
        if (ImGui::InputFloat("Sample Spacing (m)", &activeProfile.sampleSpacingMeters, 1.0f, 10.0f, "%.1f"))
        {
            activeProfile.sampleSpacingMeters = std::max(activeProfile.sampleSpacingMeters, 0.1f);
            RebuildTerrainProfileSamples(activeProfile);
        }
        const char* currentScaleLabel = ProfileScaleModeLabel(m_ProfileScaleMode);
        if (ImGui::BeginCombo("Elevation Scale", currentScaleLabel))
        {
            const ProfileElevationScaleMode modes[] = {ProfileElevationScaleMode::Auto,
                                                       ProfileElevationScaleMode::OneX,
                                                       ProfileElevationScaleMode::TwoX,
                                                       ProfileElevationScaleMode::FiveX,
                                                       ProfileElevationScaleMode::TenX,
                                                       ProfileElevationScaleMode::Fixed};
            for (ProfileElevationScaleMode mode : modes)
            {
                const bool selected = mode == m_ProfileScaleMode;
                if (ImGui::Selectable(ProfileScaleModeLabel(mode), selected))
                {
                    m_ProfileScaleMode = mode;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (m_ProfileScaleMode == ProfileElevationScaleMode::Fixed)
        {
            ImGui::InputFloat("Fixed Min Height", &m_ProfileFixedMinHeight, 1.0f, 10.0f, "%.2f");
            ImGui::InputFloat("Fixed Max Height", &m_ProfileFixedMaxHeight, 1.0f, 10.0f, "%.2f");
            if (m_ProfileFixedMaxHeight <= m_ProfileFixedMinHeight)
            {
                m_ProfileFixedMaxHeight = m_ProfileFixedMinHeight + 1.0f;
            }
        }
    }

    if (ImGui::CollapsingHeader("Import / Export"))
    {
        char profilePathBuffer[512];
        std::snprintf(profilePathBuffer, sizeof(profilePathBuffer), "%s", m_TerrainProfileFilePath.c_str());
        if (ImGui::InputText("Profile File", profilePathBuffer, sizeof(profilePathBuffer)))
        {
            m_TerrainProfileFilePath = profilePathBuffer;
        }
        if (ImGui::Button("Choose Profile File"))
        {
            const std::string selectedPath = OpenNativeFileDialog("Import Terrain Profiles", {{"GeoFPS Profile", {".geofpsprofile"}}, {"Text", {".txt"}}});
            if (!selectedPath.empty())
            {
                m_TerrainProfileFilePath = selectedPath;
            }
            else
            {
                m_StatusMessage = "No terrain profile file selected.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Choose Profile Save Location"))
        {
            const std::string selectedPath = SaveNativeFileDialog("Export Terrain Profiles",
                                                                  {{"GeoFPS Profile", {".geofpsprofile"}}},
                                                                  FileNameFromPath(m_TerrainProfileFilePath, "terrain_profiles.geofpsprofile"));
            if (!selectedPath.empty())
            {
                m_TerrainProfileFilePath = selectedPath;
            }
            else
            {
                m_StatusMessage = "No profile export location selected.";
            }
        }
        if (ImGui::Button("Export Profiles"))
        {
            ExportTerrainProfileFile(m_TerrainProfileFilePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Profiles"))
        {
            ImportTerrainProfileFile(m_TerrainProfileFilePath);
        }
    }

    const bool hasSelection = (m_SelectedProfileVertexIndex >= 0 && m_SelectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size())) ||
                              (m_SelectedProfileSampleIndex >= 0 && m_SelectedProfileSampleIndex < static_cast<int>(activeProfile.samples.size()));
    if (ImGui::CollapsingHeader("Selection Details", hasSelection ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::Text("Vertices: %zu  Samples: %zu", activeProfile.vertices.size(), activeProfile.samples.size());
        if (m_SelectedProfileVertexIndex >= 0 && m_SelectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()))
        {
            const TerrainProfileVertex& vertex = activeProfile.vertices[static_cast<size_t>(m_SelectedProfileVertexIndex)];
            const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(activeProfile);
            GeoConverter converter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
            const TerrainCoordinateMode coordinateMode = profileTerrain != nullptr ?
                                                             profileTerrain->settings.coordinateMode :
                                                             TerrainCoordinateMode::Geographic;
            const TerrainProfileVertex geoVertex = ProfileVertexAsGeographic(activeProfile, vertex, converter, coordinateMode);
            const bool hasTerrainHeight =
                profileTerrain != nullptr && TerrainDatasetContainsCoordinate(*profileTerrain, geoVertex.latitude, geoVertex.longitude);
            const double height = hasTerrainHeight ?
                                      SampleTerrainHeightAt(*profileTerrain, geoVertex.latitude, geoVertex.longitude) :
                                      0.0;
            const glm::dvec3 local = activeProfile.useLocalCoordinates ? vertex.localPosition :
                                      profileTerrain != nullptr ?
                                                                           TerrainCoordinateToLocal(*profileTerrain, geoVertex.latitude, geoVertex.longitude, height) :
                                                                           converter.ToLocal(geoVertex.latitude, geoVertex.longitude, height);
            ImGui::Text("Selected vertex: %d", m_SelectedProfileVertexIndex + 1);
            if (coordinateMode == TerrainCoordinateMode::LocalMeters)
            {
                ImGui::Text("X %.3f  Z %.3f  Height %.2f", geoVertex.latitude, geoVertex.longitude, height);
            }
            else
            {
                ImGui::Text("Lat %.8f  Lon %.8f  Height %.2f", geoVertex.latitude, geoVertex.longitude, height);
            }
            if (activeProfile.useLocalCoordinates)
            {
                ImGui::Text("Profile mode: local XYZ");
                TerrainProfileVertex& editableVertex = activeProfile.vertices[static_cast<size_t>(m_SelectedProfileVertexIndex)];
                bool changedLocal = false;
                changedLocal |= ImGui::InputDouble("Local X", &editableVertex.localPosition.x, 0.0, 0.0, "%.3f");
                changedLocal |= ImGui::InputDouble("Local Y", &editableVertex.localPosition.y, 0.0, 0.0, "%.3f");
                changedLocal |= ImGui::InputDouble("Local Z", &editableVertex.localPosition.z, 0.0, 0.0, "%.3f");
                if (changedLocal)
                {
                    const glm::dvec3 geographic =
                        ProfileLocalToTerrainCoordinate(profileTerrain, converter, editableVertex.localPosition);
                    editableVertex.latitude = geographic.x;
                    editableVertex.longitude = geographic.y;
                    RebuildTerrainProfileSamples(activeProfile);
                }
            }
            else
            {
                TerrainProfileVertex& editableVertex = activeProfile.vertices[static_cast<size_t>(m_SelectedProfileVertexIndex)];
                bool changedGeo = false;
                changedGeo |= ImGui::InputDouble("Vertex Latitude", &editableVertex.latitude, 0.0, 0.0, "%.8f");
                changedGeo |= ImGui::InputDouble("Vertex Longitude", &editableVertex.longitude, 0.0, 0.0, "%.8f");
                if (changedGeo)
                {
                    RebuildTerrainProfileSamples(activeProfile);
                }
            }
            if (!hasTerrainHeight)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Warning: vertex is outside the active terrain coverage.");
            }
            ImGui::Text("Local xyz: %.2f, %.2f, %.2f", local.x, local.y, local.z);
            if (ImGui::Button("Delete Selected Vertex"))
            {
                activeProfile.vertices.erase(activeProfile.vertices.begin() + m_SelectedProfileVertexIndex);
                m_SelectedProfileVertexIndex = -1;
                RebuildTerrainProfileSamples(activeProfile);
                m_HoveredProfileSampleIndex = -1;
            }
        }
        if (m_SelectedProfileSampleIndex >= 0 && m_SelectedProfileSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            const TerrainProfileSample& sample = activeProfile.samples[static_cast<size_t>(m_SelectedProfileSampleIndex)];
            ImGui::Text("Selected sample: %.2f m", sample.distanceMeters);
            ImGui::Text("Lat %.8f  Lon %.8f  Height %.2f", sample.latitude, sample.longitude, sample.height);
            if (!sample.valid)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "Warning: sample is outside the terrain coverage and should not be used for extraction.");
            }
            ImGui::Text("Line angle: %.2f degrees", sample.lineAngleDegrees);
            ImGui::Text("Local xyz: %.2f, %.2f, %.2f",
                        sample.localPosition.x,
                        sample.localPosition.y,
                        sample.localPosition.z);
        }
    }
}

void Application::RenderTerrainProfileToolbar(TerrainProfile& activeProfile)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.12f, 0.86f));
    ImGui::BeginChild("##profile_map_toolbar", ImVec2(0.0f, 48.0f), true, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::Button(m_ProfileDrawMode ? "Draw Profile On" : "Draw Profile"))
    {
        m_ProfileDrawMode = !m_ProfileDrawMode;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ProfileAuxiliaryDrawMode ? "A Vertices On" : "A Vertices"))
    {
        m_ProfileAuxiliaryDrawMode = !m_ProfileAuxiliaryDrawMode;
        if (m_ProfileAuxiliaryDrawMode)
        {
            m_ProfileDrawMode = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ProfileEditMode ? "Edit Vertices On" : "Edit Vertices"))
    {
        m_ProfileEditMode = !m_ProfileEditMode;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Profile"))
    {
        TerrainProfile profile;
        profile.name = "Profile " + std::to_string(m_TerrainProfiles.size() + 1);
        m_TerrainProfiles.push_back(std::move(profile));
        m_ActiveTerrainProfileIndex = static_cast<int>(m_TerrainProfiles.size()) - 1;
        m_SelectedProfileVertexIndex = -1;
        m_SelectedProfileSampleIndex = -1;
        m_HoveredProfileSampleIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Path"))
    {
        activeProfile.vertices.clear();
        activeProfile.samples.clear();
        m_SelectedProfileVertexIndex = -1;
        m_SelectedProfileSampleIndex = -1;
        m_HoveredProfileSampleIndex = -1;
    }
    ImGui::SameLine();
    if (m_SelectedProfileVertexIndex >= 0 && m_SelectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()) &&
        ImGui::Button("Delete Vertex"))
    {
        activeProfile.vertices.erase(activeProfile.vertices.begin() + m_SelectedProfileVertexIndex);
        m_SelectedProfileVertexIndex = -1;
        m_HoveredProfileSampleIndex = -1;
        RebuildTerrainProfileSamples(activeProfile);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Aerial Map", &m_ShowProfileAerialImage);
    ImGui::SameLine();
    ImGui::Checkbox("Samples", &m_ShowProfileSamples);
    ImGui::SameLine();
    if (ImGui::Checkbox("Isolines", &m_IsolineSettings.enabled))
    {
        MarkTerrainIsolinesDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("CSV##export_profile_csv"))
    {
        const std::string savePath = SaveNativeFileDialog("Export Profile CSV",
            {{"CSV", {".csv"}}}, activeProfile.name + ".csv");
        if (!savePath.empty()) ExportActiveProfileAsCsv(savePath);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export profile samples as CSV");
    ImGui::SameLine();
    if (ImGui::Button("KML##export_profile_kml"))
    {
        const std::string savePath = SaveNativeFileDialog("Export Profile KML",
            {{"KML", {".kml"}}}, activeProfile.name + ".kml");
        if (!savePath.empty()) ExportActiveProfileAsKml(savePath);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export profile as KML LineString");
    ImGui::SameLine();
    ImGui::TextUnformatted(activeProfile.samples.empty() ? "No profile samples" : "Profile ready");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Draw mode: click the map to add vertices. Toggle A Vertices to place auxiliary vertices.");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void Application::RenderTerrainProfileMap(TerrainProfile& activeProfile)
{
    const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(activeProfile);
    TerrainBounds bounds;
    bool hasProfileBounds = false;
    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        if (!TerrainDatasetHasCoverage(dataset) || !TerrainProfileIncludesTerrain(activeProfile, dataset))
        {
            continue;
        }

        if (!dataset.bounds.valid)
        {
            continue;
        }

        const TerrainBounds datasetBounds = ToTerrainBounds(dataset.bounds);
        bounds.minLatitude = std::min(bounds.minLatitude, datasetBounds.minLatitude);
        bounds.maxLatitude = std::max(bounds.maxLatitude, datasetBounds.maxLatitude);
        bounds.minLongitude = std::min(bounds.minLongitude, datasetBounds.minLongitude);
        bounds.maxLongitude = std::max(bounds.maxLongitude, datasetBounds.maxLongitude);
        bounds.minHeight = std::min(bounds.minHeight, datasetBounds.minHeight);
        bounds.maxHeight = std::max(bounds.maxHeight, datasetBounds.maxHeight);
        hasProfileBounds = true;
    }

    if (!hasProfileBounds)
    {
        ImGui::Text("Load and include at least one visible terrain before drawing profiles.");
        return;
    }

    if (!m_ProfileMapViewInitialized)
    {
        m_ProfileMapMinLatitude = bounds.minLatitude;
        m_ProfileMapMaxLatitude = bounds.maxLatitude;
        m_ProfileMapMinLongitude = bounds.minLongitude;
        m_ProfileMapMaxLongitude = bounds.maxLongitude;
        m_ProfileMapViewInitialized = true;
    }

    const auto refitMapView = [&]() {
        m_ProfileMapMinLatitude = bounds.minLatitude;
        m_ProfileMapMaxLatitude = bounds.maxLatitude;
        m_ProfileMapMinLongitude = bounds.minLongitude;
        m_ProfileMapMaxLongitude = bounds.maxLongitude;
    };

    m_ProfileMapMinLatitude = std::clamp(m_ProfileMapMinLatitude, bounds.minLatitude, bounds.maxLatitude);
    m_ProfileMapMaxLatitude = std::clamp(m_ProfileMapMaxLatitude, bounds.minLatitude, bounds.maxLatitude);
    m_ProfileMapMinLongitude = std::clamp(m_ProfileMapMinLongitude, bounds.minLongitude, bounds.maxLongitude);
    m_ProfileMapMaxLongitude = std::clamp(m_ProfileMapMaxLongitude, bounds.minLongitude, bounds.maxLongitude);
    if (m_ProfileMapMaxLatitude - m_ProfileMapMinLatitude < 1e-9 || m_ProfileMapMaxLongitude - m_ProfileMapMinLongitude < 1e-9)
    {
        refitMapView();
    }

    const double longitudeSpan = std::max(m_ProfileMapMaxLongitude - m_ProfileMapMinLongitude, 1e-9);
    const double latitudeSpan = std::max(m_ProfileMapMaxLatitude - m_ProfileMapMinLatitude, 1e-9);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    RenderTerrainProfileToolbar(activeProfile);
    float mapWidth = std::max(availableWidth, 360.0f);
    float mapHeight = 420.0f;
    if (m_ProfileMapSizeMode == ProfileMapSizeMode::Small)
    {
        mapWidth = std::min(mapWidth, 560.0f);
        mapHeight = 260.0f;
    }
    else if (m_ProfileMapSizeMode == ProfileMapSizeMode::Medium)
    {
        mapWidth = std::min(mapWidth, 760.0f);
        mapHeight = 340.0f;
    }
    else if (m_ProfileMapSizeMode == ProfileMapSizeMode::Large)
    {
        mapWidth = std::min(mapWidth, 1040.0f);
        mapHeight = 420.0f;
    }
    else
    {
        mapHeight = std::max(ImGui::GetContentRegionAvail().y - 260.0f, 460.0f);
    }
    m_ProfileMapLastWidth = mapWidth;
    const ImVec2 mapTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 mapBottomRight(mapTopLeft.x + mapWidth, mapTopLeft.y + mapHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(mapTopLeft, mapBottomRight, true);

    // Clamped version — keeps lines, circles, and labels on-screen.
    const auto mapPointFromGeo = [&](double latitude, double longitude) {
        const float v = static_cast<float>((latitude - m_ProfileMapMinLatitude) / latitudeSpan);
        const float u = static_cast<float>((longitude - m_ProfileMapMinLongitude) / longitudeSpan);
        return ImVec2(mapTopLeft.x + (std::clamp(u, 0.0f, 1.0f) * mapWidth),
                      mapBottomRight.y - (std::clamp(v, 0.0f, 1.0f) * mapHeight));
    };
    // Unclamped version — used for AddImageQuad corners so the image scales
    // correctly when zoomed in and corners fall outside the visible canvas.
    // The PushClipRect above ensures nothing outside the canvas is actually drawn.
    const auto mapPointFromGeoUnclamped = [&](double latitude, double longitude) {
        const float v = static_cast<float>((latitude - m_ProfileMapMinLatitude) / latitudeSpan);
        const float u = static_cast<float>((longitude - m_ProfileMapMinLongitude) / longitudeSpan);
        return ImVec2(mapTopLeft.x + (u * mapWidth),
                      mapBottomRight.y - (v * mapHeight));
    };
    GeoConverter profileConverter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
    const TerrainCoordinateMode profileCoordinateMode = profileTerrain != nullptr ?
                                                            profileTerrain->settings.coordinateMode :
                                                            TerrainCoordinateMode::Geographic;

    const auto geoFromMapPoint = [&](const ImVec2& point) {
        const float u = std::clamp((point.x - mapTopLeft.x) / mapWidth, 0.0f, 1.0f);
        const float v = std::clamp((mapBottomRight.y - point.y) / mapHeight, 0.0f, 1.0f);
        return MakeProfileVertexFromGeographic(activeProfile,
                                                m_ProfileMapMinLatitude + (static_cast<double>(v) * latitudeSpan),
                                                m_ProfileMapMinLongitude + (static_cast<double>(u) * longitudeSpan),
                                                m_ProfileAuxiliaryDrawMode,
                                                profileConverter,
                                                profileCoordinateMode);
    };

    drawList->AddRectFilled(mapTopLeft, mapBottomRight, IM_COL32(34, 44, 56, 255), 4.0f);
    const OverlayEntry* overlay = GetActiveOverlayEntry();
    const bool overlayReady = m_ShowProfileAerialImage && overlay != nullptr && overlay->image.enabled && overlay->image.loaded && overlay->texture.IsLoaded();
    if (overlayReady)
    {
        const ImTextureID textureId = static_cast<ImTextureID>(static_cast<uintptr_t>(overlay->texture.GetNativeHandle()));
        const ImVec2 topLeft     = mapPointFromGeoUnclamped(overlay->image.topLeft.latitude,     overlay->image.topLeft.longitude);
        const ImVec2 topRight    = mapPointFromGeoUnclamped(overlay->image.topRight.latitude,    overlay->image.topRight.longitude);
        const ImVec2 bottomRight = mapPointFromGeoUnclamped(overlay->image.bottomRight.latitude, overlay->image.bottomRight.longitude);
        const ImVec2 bottomLeft  = mapPointFromGeoUnclamped(overlay->image.bottomLeft.latitude,  overlay->image.bottomLeft.longitude);
        drawList->AddImageQuad(textureId,
                               topLeft,
                               topRight,
                               bottomRight,
                               bottomLeft,
                               ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 0.0f),
                               ImVec2(1.0f, 1.0f),
                               ImVec2(0.0f, 1.0f),
                               IM_COL32(255, 255, 255, 255));
    }
    else
    {
        for (int tick = 1; tick < 4; ++tick)
        {
            const float x = mapTopLeft.x + (mapWidth * static_cast<float>(tick) / 4.0f);
            const float y = mapTopLeft.y + (mapHeight * static_cast<float>(tick) / 4.0f);
            drawList->AddLine(ImVec2(x, mapTopLeft.y), ImVec2(x, mapBottomRight.y), IM_COL32(68, 86, 104, 255), 1.0f);
            drawList->AddLine(ImVec2(mapTopLeft.x, y), ImVec2(mapBottomRight.x, y), IM_COL32(68, 86, 104, 255), 1.0f);
        }
    }
    drawList->AddRect(mapTopLeft, mapBottomRight, IM_COL32(152, 172, 192, 255), 4.0f, 0, 1.5f);

    if (m_IsolineSettings.enabled)
    {
        RebuildTerrainIsolinesIfNeeded();
        for (const TerrainIsolineSegment& segment : m_TerrainIsolines)
        {
            drawList->AddLine(mapPointFromGeoUnclamped(segment.start.latitude, segment.start.longitude),
                              mapPointFromGeoUnclamped(segment.end.latitude, segment.end.longitude),
                              ColorU32(segment.color),
                              m_IsolineSettings.thickness);
        }
    }

    for (const TerrainProfile& profile : m_TerrainProfiles)
    {
        if (!profile.visible || profile.vertices.size() < 2)
        {
            continue;
        }
        for (size_t index = 0; index + 1 < profile.vertices.size(); ++index)
        {
            const TerrainDataset* lineTerrain = GetPrimaryTerrainForProfile(profile);
            GeoConverter lineConverter(lineTerrain != nullptr ? lineTerrain->geoReference : m_GeoReference);
            const TerrainCoordinateMode lineCoordinateMode = lineTerrain != nullptr ?
                                                                 lineTerrain->settings.coordinateMode :
                                                                 TerrainCoordinateMode::Geographic;
            const TerrainProfileVertex start =
                ProfileVertexAsGeographic(profile, profile.vertices[index], lineConverter, lineCoordinateMode);
            const TerrainProfileVertex end =
                ProfileVertexAsGeographic(profile, profile.vertices[index + 1], lineConverter, lineCoordinateMode);
            drawList->AddLine(mapPointFromGeoUnclamped(start.latitude, start.longitude),
                              mapPointFromGeoUnclamped(end.latitude, end.longitude),
                              ProfileColorU32(profile),
                              profile.thickness);
        }
    }

    if (m_ShowProfileSamples)
    {
        for (const TerrainProfileSample& sample : activeProfile.samples)
        {
            drawList->AddCircleFilled(mapPointFromGeoUnclamped(sample.latitude, sample.longitude),
                                      sample.valid ? 1.7f : 2.5f,
                                      sample.valid ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 110, 70, 190),
                                      8);
        }
    }
    for (int index = 0; index < static_cast<int>(activeProfile.vertices.size()); ++index)
    {
        const TerrainProfileVertex& vertex = activeProfile.vertices[static_cast<size_t>(index)];
        const TerrainProfileVertex geoVertex =
            ProfileVertexAsGeographic(activeProfile, vertex, profileConverter, profileCoordinateMode);
        const ImVec2 point = mapPointFromGeoUnclamped(geoVertex.latitude, geoVertex.longitude);
        const bool selected = index == m_SelectedProfileVertexIndex;
        const ImU32 vertexColor = vertex.auxiliary ? IM_COL32(90, 230, 255, 255) : ProfileColorU32(activeProfile);
        drawList->AddCircleFilled(point, selected ? 6.0f : 4.5f, selected ? IM_COL32(255, 220, 64, 255) : vertexColor, 18);
        drawList->AddCircle(point, selected ? 7.5f : 6.0f, IM_COL32(20, 24, 28, 230), 18, 1.5f);
        char markerLabel[16];
        std::snprintf(markerLabel, sizeof(markerLabel), "%c%d", vertex.auxiliary ? 'A' : 'V', index + 1);
        drawList->AddText(ImVec2(point.x + 7.0f, point.y + 3.0f), IM_COL32(255, 255, 255, 235), markerLabel);
        if (m_ShowProfileHeightLabels)
        {
            const bool hasTerrainHeight =
                profileTerrain != nullptr && TerrainDatasetContainsCoordinate(*profileTerrain, geoVertex.latitude, geoVertex.longitude);
            const double height = hasTerrainHeight ?
                                      SampleTerrainHeightAt(*profileTerrain, geoVertex.latitude, geoVertex.longitude) :
                                      0.0;
            char label[64];
            std::snprintf(label, sizeof(label), hasTerrainHeight ? "%.1f m" : "outside", height);
            drawList->AddText(ImVec2(point.x + 8.0f, point.y - 8.0f),
                              hasTerrainHeight ? IM_COL32(255, 255, 255, 220) : IM_COL32(255, 130, 80, 235),
                              label);
        }
    }

    TerrainProfileSample highlightedSample;
    bool hasHighlightedSample = false;
    bool hoveredSample = false;
    if (m_ProfileGraphHoverActive && m_ProfileGraphHoverSample.valid)
    {
        highlightedSample = m_ProfileGraphHoverSample;
        hasHighlightedSample = true;
        hoveredSample = true;
    }
    else
    {
        const int highlightedSampleIndex =
            (m_HoveredProfileSampleIndex >= 0 && m_HoveredProfileSampleIndex < static_cast<int>(activeProfile.samples.size())) ?
                m_HoveredProfileSampleIndex :
                m_SelectedProfileSampleIndex;
        if (highlightedSampleIndex >= 0 && highlightedSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            highlightedSample = activeProfile.samples[static_cast<size_t>(highlightedSampleIndex)];
            hasHighlightedSample = true;
            hoveredSample = highlightedSampleIndex == m_HoveredProfileSampleIndex;
        }
    }
    if (hasHighlightedSample)
    {
        const ImVec2 samplePoint = mapPointFromGeoUnclamped(highlightedSample.latitude, highlightedSample.longitude);
        const ImU32 markerColor = !highlightedSample.valid ? IM_COL32(255, 110, 70, 255) :
                                  hoveredSample ? IM_COL32(90, 230, 255, 255) :
                                                  IM_COL32(255, 220, 64, 255);
        drawList->AddCircleFilled(samplePoint, hoveredSample ? 9.0f : 7.0f, markerColor, 28);
        drawList->AddCircle(samplePoint, hoveredSample ? 14.0f : 10.0f, IM_COL32(20, 24, 28, 230), 28, 2.5f);
        drawList->AddCircle(samplePoint, hoveredSample ? 20.0f : 14.0f, markerColor, 28, 1.5f);
        drawList->AddLine(ImVec2(samplePoint.x - 16.0f, samplePoint.y), ImVec2(samplePoint.x + 16.0f, samplePoint.y), markerColor, 2.5f);
        drawList->AddLine(ImVec2(samplePoint.x, samplePoint.y - 16.0f), ImVec2(samplePoint.x, samplePoint.y + 16.0f), markerColor, 2.5f);
        if (hoveredSample)
        {
            char hoverLabel[80];
            std::snprintf(hoverLabel, sizeof(hoverLabel), "%.0f m", highlightedSample.distanceMeters);
            drawList->AddText(ImVec2(samplePoint.x + 12.0f, samplePoint.y - 24.0f),
                              IM_COL32(90, 230, 255, 255),
                              hoverLabel);
        }
    }

    ImGui::SetCursorScreenPos(mapTopLeft);

    ImGui::InvisibleButton("##terrain_profile_map", ImVec2(mapWidth, mapHeight));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
    {
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const TerrainProfileVertex hoveredVertex = geoFromMapPoint(mousePosition);
        const bool hasTerrainHeight =
            profileTerrain != nullptr && TerrainDatasetContainsCoordinate(*profileTerrain, hoveredVertex.latitude, hoveredVertex.longitude);
        const double hoveredHeight = hasTerrainHeight ?
                                         SampleTerrainHeightAt(*profileTerrain, hoveredVertex.latitude, hoveredVertex.longitude) :
                                         0.0;
        const glm::dvec3 hoveredLocal = profileTerrain != nullptr ?
                                            TerrainCoordinateToLocal(*profileTerrain,
                                                                     hoveredVertex.latitude,
                                                                     hoveredVertex.longitude,
                                                                     hoveredHeight) :
                                            profileConverter.ToLocal(hoveredVertex.latitude, hoveredVertex.longitude, hoveredHeight);
        drawList->AddCircle(mousePosition, 4.0f, IM_COL32(255, 255, 255, 180), 16, 1.2f);
        char heightLabel[80];
        std::snprintf(heightLabel,
                      sizeof(heightLabel),
                      hasTerrainHeight ? "Height %.2f m" : "Outside terrain coverage",
                      hoveredHeight);
        if (profileCoordinateMode == TerrainCoordinateMode::LocalMeters)
        {
            ImGui::SetTooltip("X %.3f\nZ %.3f\n%s\nXYZ %.2f, %.2f, %.2f",
                              hoveredVertex.latitude,
                              hoveredVertex.longitude,
                              heightLabel,
                              hoveredLocal.x,
                              hoveredLocal.y,
                              hoveredLocal.z);
        }
        else
        {
            ImGui::SetTooltip("Lat %.8f\nLon %.8f\n%s\nXYZ %.2f, %.2f, %.2f",
                              hoveredVertex.latitude,
                              hoveredVertex.longitude,
                              heightLabel,
                              hoveredLocal.x,
                              hoveredLocal.y,
                              hoveredLocal.z);
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f)
        {
            const double zoomFactor = io.MouseWheel > 0.0f ? 0.82 : 1.22;
            const double cursorLatitudeT = std::clamp((hoveredVertex.latitude - m_ProfileMapMinLatitude) / latitudeSpan, 0.0, 1.0);
            const double cursorLongitudeT = std::clamp((hoveredVertex.longitude - m_ProfileMapMinLongitude) / longitudeSpan, 0.0, 1.0);
            const double newLatitudeSpan = std::clamp(latitudeSpan * zoomFactor, (bounds.maxLatitude - bounds.minLatitude) * 0.002, bounds.maxLatitude - bounds.minLatitude);
            const double newLongitudeSpan = std::clamp(longitudeSpan * zoomFactor, (bounds.maxLongitude - bounds.minLongitude) * 0.002, bounds.maxLongitude - bounds.minLongitude);
            m_ProfileMapMinLatitude = hoveredVertex.latitude - (newLatitudeSpan * cursorLatitudeT);
            m_ProfileMapMaxLatitude = m_ProfileMapMinLatitude + newLatitudeSpan;
            m_ProfileMapMinLongitude = hoveredVertex.longitude - (newLongitudeSpan * cursorLongitudeT);
            m_ProfileMapMaxLongitude = m_ProfileMapMinLongitude + newLongitudeSpan;
            if (m_ProfileMapMinLatitude < bounds.minLatitude)
            {
                m_ProfileMapMaxLatitude += bounds.minLatitude - m_ProfileMapMinLatitude;
                m_ProfileMapMinLatitude = bounds.minLatitude;
            }
            if (m_ProfileMapMaxLatitude > bounds.maxLatitude)
            {
                m_ProfileMapMinLatitude -= m_ProfileMapMaxLatitude - bounds.maxLatitude;
                m_ProfileMapMaxLatitude = bounds.maxLatitude;
            }
            if (m_ProfileMapMinLongitude < bounds.minLongitude)
            {
                m_ProfileMapMaxLongitude += bounds.minLongitude - m_ProfileMapMinLongitude;
                m_ProfileMapMinLongitude = bounds.minLongitude;
            }
            if (m_ProfileMapMaxLongitude > bounds.maxLongitude)
            {
                m_ProfileMapMinLongitude -= m_ProfileMapMaxLongitude - bounds.maxLongitude;
                m_ProfileMapMaxLongitude = bounds.maxLongitude;
            }
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
        {
            refitMapView();
        }

        const bool shiftPanning = io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (shiftPanning && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_ProfileMapIsPanning = true;
            m_ProfileMapLastPanMouse = {mousePosition.x, mousePosition.y};
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_ProfileMapIsPanning = false;
        }
        if (m_ProfileMapIsPanning && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const glm::vec2 currentMouse(mousePosition.x, mousePosition.y);
            const glm::vec2 delta = currentMouse - m_ProfileMapLastPanMouse;
            m_ProfileMapLastPanMouse = currentMouse;
            const double longitudeDelta = -(static_cast<double>(delta.x) / static_cast<double>(mapWidth)) * longitudeSpan;
            const double latitudeDelta = (static_cast<double>(delta.y) / static_cast<double>(mapHeight)) * latitudeSpan;
            m_ProfileMapMinLatitude += latitudeDelta;
            m_ProfileMapMaxLatitude += latitudeDelta;
            m_ProfileMapMinLongitude += longitudeDelta;
            m_ProfileMapMaxLongitude += longitudeDelta;
            if (m_ProfileMapMinLatitude < bounds.minLatitude)
            {
                m_ProfileMapMaxLatitude += bounds.minLatitude - m_ProfileMapMinLatitude;
                m_ProfileMapMinLatitude = bounds.minLatitude;
            }
            if (m_ProfileMapMaxLatitude > bounds.maxLatitude)
            {
                m_ProfileMapMinLatitude -= m_ProfileMapMaxLatitude - bounds.maxLatitude;
                m_ProfileMapMaxLatitude = bounds.maxLatitude;
            }
            if (m_ProfileMapMinLongitude < bounds.minLongitude)
            {
                m_ProfileMapMaxLongitude += bounds.minLongitude - m_ProfileMapMinLongitude;
                m_ProfileMapMinLongitude = bounds.minLongitude;
            }
            if (m_ProfileMapMaxLongitude > bounds.maxLongitude)
            {
                m_ProfileMapMinLongitude -= m_ProfileMapMaxLongitude - bounds.maxLongitude;
                m_ProfileMapMaxLongitude = bounds.maxLongitude;
            }
        }

        if (!io.KeyShift && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            int nearestVertex = -1;
            float nearestDistance = 12.0f;
            for (int index = 0; index < static_cast<int>(activeProfile.vertices.size()); ++index)
            {
                const TerrainProfileVertex& vertex = activeProfile.vertices[static_cast<size_t>(index)];
                const TerrainProfileVertex geoVertex =
                    ProfileVertexAsGeographic(activeProfile, vertex, profileConverter, profileCoordinateMode);
                const ImVec2 point = mapPointFromGeoUnclamped(geoVertex.latitude, geoVertex.longitude);
                const float distance = std::hypot(point.x - mousePosition.x, point.y - mousePosition.y);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestVertex = index;
                }
            }

            if (m_ProfileEditMode && nearestVertex >= 0 && !m_ProfileAuxiliaryDrawMode)
            {
                m_SelectedProfileVertexIndex = nearestVertex;
            }
            else if (m_ProfileDrawMode)
            {
                if (m_ProfileAuxiliaryDrawMode)
                {
                    if (activeProfile.vertices.size() < 2)
                    {
                        m_StatusMessage = "Add at least two V vertices before inserting A vertices between them.";
                    }
                    else
                    {
                        int insertIndex = static_cast<int>(activeProfile.vertices.size());
                        float bestSegmentDistance = std::numeric_limits<float>::max();
                        for (int segmentIndex = 0; segmentIndex + 1 < static_cast<int>(activeProfile.vertices.size()); ++segmentIndex)
                        {
                            const TerrainProfileVertex startGeo =
                                ProfileVertexAsGeographic(activeProfile,
                                                          activeProfile.vertices[static_cast<size_t>(segmentIndex)],
                                                          profileConverter,
                                                          profileCoordinateMode);
                            const TerrainProfileVertex endGeo =
                                ProfileVertexAsGeographic(activeProfile,
                                                          activeProfile.vertices[static_cast<size_t>(segmentIndex + 1)],
                                                          profileConverter,
                                                          profileCoordinateMode);
                            const ImVec2 startPoint = mapPointFromGeo(startGeo.latitude, startGeo.longitude);
                            const ImVec2 endPoint = mapPointFromGeo(endGeo.latitude, endGeo.longitude);
                            const float dx = endPoint.x - startPoint.x;
                            const float dy = endPoint.y - startPoint.y;
                            const float lengthSquared = (dx * dx) + (dy * dy);
                            if (lengthSquared <= 0.0001f)
                            {
                                continue;
                            }

                            const float t = std::clamp(((mousePosition.x - startPoint.x) * dx +
                                                        (mousePosition.y - startPoint.y) * dy) /
                                                           lengthSquared,
                                                       0.0f,
                                                       1.0f);
                            const ImVec2 projected(startPoint.x + (dx * t), startPoint.y + (dy * t));
                            const float distance = std::hypot(projected.x - mousePosition.x, projected.y - mousePosition.y);
                            if (distance < bestSegmentDistance)
                            {
                                bestSegmentDistance = distance;
                                insertIndex = segmentIndex + 1;
                            }
                        }

                        TerrainProfileVertex auxiliaryVertex = hoveredVertex;
                        auxiliaryVertex.auxiliary = true;
                        insertIndex = std::clamp(insertIndex, 1, static_cast<int>(activeProfile.vertices.size()));
                        activeProfile.vertices.insert(activeProfile.vertices.begin() + insertIndex, auxiliaryVertex);
                        m_SelectedProfileVertexIndex = insertIndex;
                        RebuildTerrainProfileSamples(activeProfile);
                        m_StatusMessage = "Inserted auxiliary profile vertex between V vertices.";
                    }
                }
                else
                {
                    activeProfile.vertices.push_back(hoveredVertex);
                    m_SelectedProfileVertexIndex = static_cast<int>(activeProfile.vertices.size()) - 1;
                    RebuildTerrainProfileSamples(activeProfile);
                    m_StatusMessage = "Added profile vertex.";
                }
            }
        }

        if (!io.KeyShift && m_ProfileEditMode && m_SelectedProfileVertexIndex >= 0 &&
            m_SelectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            TerrainProfileVertex movedVertex = hoveredVertex;
            movedVertex.auxiliary = activeProfile.vertices[static_cast<size_t>(m_SelectedProfileVertexIndex)].auxiliary;
            activeProfile.vertices[static_cast<size_t>(m_SelectedProfileVertexIndex)] = movedVertex;
            RebuildTerrainProfileSamples(activeProfile);
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_ProfileDrawMode = false;
        }
    }

    drawList->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(mapTopLeft.x, mapBottomRight.y));

    if (m_IsolineSettings.enabled)
    {
        const double minHeight = m_TerrainIsolineSampleGrid.IsValid() ? m_TerrainIsolineSampleGrid.minHeight : 0.0;
        const double maxHeight = m_TerrainIsolineSampleGrid.IsValid() ? m_TerrainIsolineSampleGrid.maxHeight : 0.0;
        const double interval = ResolveContourInterval(minHeight, maxHeight, m_IsolineSettings);
        ImGui::Text("Map source: %s  Isolines: %zu%s  Interval: %.2f m  Height: %.2f to %.2f m",
                    overlayReady ? "active aerial overlay" : "terrain bounds grid",
                    m_TerrainIsolines.size(),
                    m_TerrainIsolinesDirty ? " (stale)" : "",
                    interval,
                    minHeight,
                    maxHeight);
        ImGui::SameLine();
        ImGui::Text("Backend: %s%s",
                    m_UseGpuIsolineGeneration ? (m_TerrainIsolinesUsedGpu ? "Metal GPU" : "CPU fallback") : "CPU",
                    m_TerrainIsolineSampleGridDirty ? "  Grid stale" : "");
    }
    else
    {
        ImGui::Text("Map source: %s", overlayReady ? "active aerial overlay" : "terrain bounds grid");
    }
}

void Application::SyncProfileMapToGraphZoom(const TerrainProfile& profile)
{
    // Compute the geographic bounding box of all samples within the current
    // zoom distance range, then set the map view to that bbox (with padding).
    // The map's own clamping will keep it inside the terrain bounds.
    if (m_ProfileGraphZoomMaxDist <= m_ProfileGraphZoomMinDist)
        return;

    double minLat = std::numeric_limits<double>::max();
    double maxLat = std::numeric_limits<double>::lowest();
    double minLon = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();

    for (const TerrainProfileSample& sample : profile.samples)
    {
        if (!sample.valid) continue;
        if (sample.distanceMeters < m_ProfileGraphZoomMinDist || sample.distanceMeters > m_ProfileGraphZoomMaxDist) continue;
        minLat = std::min(minLat, sample.latitude);
        maxLat = std::max(maxLat, sample.latitude);
        minLon = std::min(minLon, sample.longitude);
        maxLon = std::max(maxLon, sample.longitude);
    }

    if (minLat == std::numeric_limits<double>::max()) return; // no samples in range

    // Add ~20 % padding so the route doesn't fill edge-to-edge on the map
    const double latSpan = std::max(maxLat - minLat, 1e-6);
    const double lonSpan = std::max(maxLon - minLon, 1e-6);
    const double latPad  = latSpan * 0.20;
    const double lonPad  = lonSpan * 0.20;
    m_ProfileMapMinLatitude  = minLat - latPad;
    m_ProfileMapMaxLatitude  = maxLat + latPad;
    m_ProfileMapMinLongitude = minLon - lonPad;
    m_ProfileMapMaxLongitude = maxLon + lonPad;
    // Do NOT set m_ProfileMapViewInitialized to false — we are deliberately
    // overriding the view; the clamping in RenderTerrainProfileMap will handle bounds.
}

void Application::RenderTerrainProfileGraph(TerrainProfile& activeProfile)
{
    if (activeProfile.samples.empty())
    {
        m_HoveredProfileSampleIndex = -1;
        m_ProfileGraphHoverActive = false;
        ImGui::Text("Draw at least two vertices to generate an elevation profile.");
        return;
    }

    double minHeight = std::numeric_limits<double>::max();
    double maxHeight = std::numeric_limits<double>::lowest();
    int invalidSampleCount = 0;
    for (const TerrainProfileSample& sample : activeProfile.samples)
    {
        if (!sample.valid)
        {
            ++invalidSampleCount;
            continue;
        }
        minHeight = std::min(minHeight, sample.height);
        maxHeight = std::max(maxHeight, sample.height);
    }
    if (minHeight == std::numeric_limits<double>::max())
    {
        m_HoveredProfileSampleIndex = -1;
        m_ProfileGraphHoverActive = false;
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                           "The profile has no valid terrain samples inside the selected terrain coverage.");
        return;
    }

    const double centerHeight = 0.5 * (minHeight + maxHeight);
    const double heightRange = std::max(maxHeight - minHeight, 1.0);
    double graphMinHeight = minHeight;
    double graphMaxHeight = maxHeight;
    if (m_ProfileScaleMode == ProfileElevationScaleMode::Fixed)
    {
        graphMinHeight = m_ProfileFixedMinHeight;
        graphMaxHeight = m_ProfileFixedMaxHeight;
    }
    else if (m_ProfileScaleMode != ProfileElevationScaleMode::Auto)
    {
        double exaggeration = 1.0;
        if (m_ProfileScaleMode == ProfileElevationScaleMode::TwoX) exaggeration = 2.0;
        else if (m_ProfileScaleMode == ProfileElevationScaleMode::FiveX) exaggeration = 5.0;
        else if (m_ProfileScaleMode == ProfileElevationScaleMode::TenX) exaggeration = 10.0;
        const double visibleRange = std::max(heightRange / exaggeration, 1.0);
        graphMinHeight = centerHeight - (visibleRange * 0.5);
        graphMaxHeight = centerHeight + (visibleRange * 0.5);
    }

    const float leftAxisWidth = 74.0f;
    const float graphWidth = std::max(m_ProfileMapLastWidth, 360.0f);
    const float graphHeight = 220.0f;
    ImGui::Checkbox("Add A Vertices From Height Graph", &m_ProfileGraphAuxiliaryInsertMode);
    ImGui::SameLine();
    ImGui::TextDisabled("Click the elevation profile to insert an A vertex at the nearest distance.");
    const ImVec2 graphTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 plotTopLeft(graphTopLeft.x + leftAxisWidth, graphTopLeft.y);
    const ImVec2 graphBottomRight(graphTopLeft.x + graphWidth, graphTopLeft.y + graphHeight);
    const ImVec2 plotBottomRight(graphBottomRight.x, graphBottomRight.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const double totalDistance = std::max(activeProfile.samples.back().distanceMeters, 1.0);
    const double graphHeightRange = std::max(graphMaxHeight - graphMinHeight, 1.0);
    const float plotWidth = std::max(graphWidth - leftAxisWidth, 1.0f);
    const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(activeProfile);
    GeoConverter profileConverter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
    const TerrainCoordinateMode profileCoordinateMode = profileTerrain != nullptr ?
                                                            profileTerrain->settings.coordinateMode :
                                                            TerrainCoordinateMode::Geographic;

    // Horizontal zoom range (in metres along path). Negative max = full profile.
    double zoomedMinDist = (m_ProfileGraphZoomMaxDist > 0.0) ? m_ProfileGraphZoomMinDist : 0.0;
    double zoomedMaxDist = (m_ProfileGraphZoomMaxDist > 0.0) ? m_ProfileGraphZoomMaxDist : totalDistance;
    zoomedMinDist = std::clamp(zoomedMinDist, 0.0, totalDistance);
    zoomedMaxDist = std::clamp(zoomedMaxDist, zoomedMinDist + 1.0, totalDistance);
    const double zoomedRange  = std::max(zoomedMaxDist - zoomedMinDist, 1.0);
    const bool   hasGraphZoom = zoomedMinDist > 0.0 || zoomedMaxDist < totalDistance * 0.9999;

    const auto graphPointFromSample = [&](const TerrainProfileSample& sample) {
        const float x = plotTopLeft.x + static_cast<float>((sample.distanceMeters - zoomedMinDist) / zoomedRange) * plotWidth;
        const float y = plotBottomRight.y - static_cast<float>((sample.height - graphMinHeight) / graphHeightRange) * graphHeight;
        return ImVec2(x, std::clamp(y, plotTopLeft.y, plotBottomRight.y));
    };

    const auto sampleAtDistance = [&](double distanceMeters) {
        const double clampedDistance = std::clamp(distanceMeters, 0.0, totalDistance);
        for (size_t sampleIndex = 0; sampleIndex + 1 < activeProfile.samples.size(); ++sampleIndex)
        {
            const TerrainProfileSample& start = activeProfile.samples[sampleIndex];
            const TerrainProfileSample& end = activeProfile.samples[sampleIndex + 1];
            if (!start.valid || !end.valid)
            {
                continue;
            }
            if (clampedDistance < std::min(start.distanceMeters, end.distanceMeters) ||
                clampedDistance > std::max(start.distanceMeters, end.distanceMeters))
            {
                continue;
            }

            const double span = std::max(std::abs(end.distanceMeters - start.distanceMeters), 1e-9);
            const double t = std::clamp((clampedDistance - start.distanceMeters) / span, 0.0, 1.0);
            TerrainProfileSample interpolated;
            interpolated.distanceMeters = clampedDistance;
            interpolated.latitude = start.latitude + ((end.latitude - start.latitude) * t);
            interpolated.longitude = start.longitude + ((end.longitude - start.longitude) * t);
            interpolated.height = start.height + ((end.height - start.height) * t);
            interpolated.lineAngleDegrees = start.lineAngleDegrees + ((end.lineAngleDegrees - start.lineAngleDegrees) * t);
            interpolated.localPosition = glm::mix(start.localPosition, end.localPosition, t);
            interpolated.valid = true;
            return interpolated;
        }

        for (const TerrainProfileSample& sample : activeProfile.samples)
        {
            if (sample.valid)
            {
                return sample;
            }
        }
        return activeProfile.samples.front();
    };

    const auto nearestSampleIndexForVertex = [&](const TerrainProfileVertex& vertex) {
        int nearestIndex = -1;
        double nearestDistanceSquared = std::numeric_limits<double>::max();
        const TerrainProfileVertex geoVertex =
            ProfileVertexAsGeographic(activeProfile, vertex, profileConverter, profileCoordinateMode);
        for (int sampleIndex = 0; sampleIndex < static_cast<int>(activeProfile.samples.size()); ++sampleIndex)
        {
            const TerrainProfileSample& sample = activeProfile.samples[static_cast<size_t>(sampleIndex)];
            const double latitudeDelta = sample.latitude - geoVertex.latitude;
            const double longitudeDelta = sample.longitude - geoVertex.longitude;
            const double distanceSquared = (latitudeDelta * latitudeDelta) + (longitudeDelta * longitudeDelta);
            if (distanceSquared < nearestDistanceSquared)
            {
                nearestDistanceSquared = distanceSquared;
                nearestIndex = sampleIndex;
            }
        }
        return nearestIndex;
    };

    drawList->AddRectFilled(graphTopLeft, graphBottomRight, IM_COL32(34, 44, 56, 255), 4.0f);
    drawList->AddRectFilled(graphTopLeft, ImVec2(plotTopLeft.x, graphBottomRight.y), IM_COL32(42, 54, 66, 255), 4.0f);
    for (int tick = 1; tick < 4; ++tick)
    {
        const float x = plotTopLeft.x + (plotWidth * static_cast<float>(tick) / 4.0f);
        const float y = plotTopLeft.y + (graphHeight * static_cast<float>(tick) / 4.0f);
        drawList->AddLine(ImVec2(x, plotTopLeft.y), ImVec2(x, plotBottomRight.y), IM_COL32(72, 88, 106, 255), 1.0f);
        drawList->AddLine(ImVec2(plotTopLeft.x, y), ImVec2(plotBottomRight.x, y), IM_COL32(72, 88, 106, 255), 1.0f);
    }
    char topLabel[64];
    char bottomLabel[64];
    char lengthLabel[96];
    std::snprintf(topLabel, sizeof(topLabel), "%.1f m", graphMaxHeight);
    std::snprintf(bottomLabel, sizeof(bottomLabel), "%.1f m", graphMinHeight);
    if (hasGraphZoom)
        std::snprintf(lengthLabel, sizeof(lengthLabel), "%.0f – %.0f m  (total %.0f m)  Dbl-click to reset", zoomedMinDist, zoomedMaxDist, totalDistance);
    else
        std::snprintf(lengthLabel, sizeof(lengthLabel), "Length %.1f m  |  Scroll to zoom", totalDistance);
    drawList->AddText(ImVec2(graphTopLeft.x + 8.0f, graphTopLeft.y + 8.0f), IM_COL32(230, 238, 246, 255), topLabel);
    drawList->AddText(ImVec2(graphTopLeft.x + 8.0f, graphBottomRight.y - 24.0f), IM_COL32(230, 238, 246, 255), bottomLabel);
    drawList->AddText(ImVec2(plotTopLeft.x + 8.0f, graphBottomRight.y - 24.0f), IM_COL32(255, 220, 120, 255), lengthLabel);
    drawList->AddLine(ImVec2(plotTopLeft.x, plotTopLeft.y), ImVec2(plotTopLeft.x, plotBottomRight.y), IM_COL32(152, 172, 192, 255), 1.2f);
    // Clip all profile geometry so zoomed samples don't bleed outside the plot area
    drawList->PushClipRect(plotTopLeft, plotBottomRight, true);
    for (size_t index = 0; index + 1 < activeProfile.samples.size(); ++index)
    {
        if (!activeProfile.samples[index].valid || !activeProfile.samples[index + 1].valid)
        {
            continue;
        }
        drawList->AddLine(graphPointFromSample(activeProfile.samples[index]),
                          graphPointFromSample(activeProfile.samples[index + 1]),
                          ProfileColorU32(activeProfile),
                          std::max(activeProfile.thickness, 1.5f));
    }

    for (int vertexIndex = 0; vertexIndex < static_cast<int>(activeProfile.vertices.size()); ++vertexIndex)
    {
        const int sampleIndex = nearestSampleIndexForVertex(activeProfile.vertices[static_cast<size_t>(vertexIndex)]);
        if (sampleIndex < 0)
        {
            continue;
        }

        const TerrainProfileSample& sample = activeProfile.samples[static_cast<size_t>(sampleIndex)];
        const ImVec2 vertexPoint = graphPointFromSample(sample);
        const TerrainProfileVertex& vertex = activeProfile.vertices[static_cast<size_t>(vertexIndex)];
        const bool directionVertex = vertexIndex > 0 && vertexIndex + 1 < static_cast<int>(activeProfile.vertices.size());
        const ImU32 markerColor = vertex.auxiliary ? IM_COL32(90, 230, 255, 235) :
                                   directionVertex ? IM_COL32(255, 180, 80, 235) :
                                                     IM_COL32(255, 220, 120, 220);
        drawList->AddLine(ImVec2(vertexPoint.x, plotTopLeft.y),
                          ImVec2(vertexPoint.x, plotBottomRight.y),
                          markerColor,
                          vertex.auxiliary || directionVertex ? 1.8f : 1.2f);
        drawList->AddCircleFilled(vertexPoint, vertex.auxiliary || directionVertex ? 4.5f : 3.5f, markerColor, 16);

        char vertexLabel[80];
        std::snprintf(vertexLabel, sizeof(vertexLabel), "%c%d %.0fm", vertex.auxiliary ? 'A' : 'V', vertexIndex + 1, sample.distanceMeters);
        drawList->AddText(ImVec2(vertexPoint.x + 5.0f, plotTopLeft.y + 8.0f + static_cast<float>(vertexIndex % 4) * 14.0f),
                          markerColor,
                          vertexLabel);
    }
    drawList->PopClipRect();
    drawList->AddRect(graphTopLeft, graphBottomRight, IM_COL32(152, 172, 192, 255), 4.0f, 0, 1.5f);

    ImGui::InvisibleButton("##terrain_profile_graph", ImVec2(graphWidth, graphHeight));
    bool foundHoveredSample = false;
    if (ImGui::IsItemHovered())
    {
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        int nearestSample = -1;
        float nearestDistance = std::numeric_limits<float>::max();
        const bool mouseInsidePlot = mousePosition.x >= plotTopLeft.x && mousePosition.x <= plotBottomRight.x &&
                                     mousePosition.y >= plotTopLeft.y && mousePosition.y <= plotBottomRight.y;
        const float clampedMouseX = std::clamp(mousePosition.x, plotTopLeft.x, plotBottomRight.x);
        const double clickedDistance = zoomedMinDist +
            (static_cast<double>(clampedMouseX - plotTopLeft.x) / static_cast<double>(plotWidth)) * zoomedRange;
        for (int index = 0; index < static_cast<int>(activeProfile.samples.size()); ++index)
        {
            const TerrainProfileSample& candidate = activeProfile.samples[static_cast<size_t>(index)];
            if (!candidate.valid)
            {
                continue;
            }

            const ImVec2 point = graphPointFromSample(candidate);
            const float distance = mouseInsidePlot ?
                                       std::abs(point.x - clampedMouseX) :
                                       std::hypot(point.x - mousePosition.x, point.y - mousePosition.y);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestSample = index;
            }
        }
        if (nearestSample >= 0 && (mouseInsidePlot || nearestDistance <= 16.0f))
        {
            foundHoveredSample = true;
            m_HoveredProfileSampleIndex = nearestSample;
            const TerrainProfileSample& nearestProfileSample = activeProfile.samples[static_cast<size_t>(nearestSample)];
            const TerrainProfileSample cursorSample = sampleAtDistance(clickedDistance);
            m_ProfileGraphHoverActive = mouseInsidePlot;
            m_ProfileGraphHoverSample = cursorSample;
            const TerrainProfileSample& displayedSample = mouseInsidePlot ? cursorSample : nearestProfileSample;
            const ImVec2 samplePoint = graphPointFromSample(displayedSample);
            if (mouseInsidePlot)
            {
                drawList->AddLine(ImVec2(clampedMouseX, plotTopLeft.y),
                                  ImVec2(clampedMouseX, plotBottomRight.y),
                                  m_ProfileGraphAuxiliaryInsertMode ? IM_COL32(90, 230, 255, 230) : IM_COL32(255, 220, 64, 210),
                                  m_ProfileGraphAuxiliaryInsertMode ? 2.5f : 2.0f);
            }
            drawList->AddCircleFilled(samplePoint,
                                      m_ProfileGraphAuxiliaryInsertMode ? 6.0f : 5.0f,
                                      m_ProfileGraphAuxiliaryInsertMode ? IM_COL32(90, 230, 255, 255) : IM_COL32(255, 220, 64, 255),
                                      18);
            if (m_ProfileGraphAuxiliaryInsertMode)
            {
                drawList->AddLine(ImVec2(samplePoint.x, plotTopLeft.y),
                                  ImVec2(samplePoint.x, plotBottomRight.y),
                                  IM_COL32(90, 230, 255, 160),
                                  1.5f);
            }
            ImGui::SetTooltip("%s%.2f m along path\nHeight %.2f m\nLine angle %.2f degrees",
                              m_ProfileGraphAuxiliaryInsertMode ? "Click to insert A vertex\n" : "",
                              displayedSample.distanceMeters,
                              displayedSample.height,
                              displayedSample.lineAngleDegrees);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (m_ProfileGraphAuxiliaryInsertMode)
                {
                    const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(activeProfile);
                    GeoConverter converter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
                    const TerrainProfileSample insertionSample = cursorSample;
                    TerrainProfileVertex insertedVertex =
                        MakeProfileVertexFromGeographic(activeProfile,
                                                        insertionSample.latitude,
                                                        insertionSample.longitude,
                                                        true,
                                                        converter,
                                                        profileTerrain != nullptr ? profileTerrain->settings.coordinateMode :
                                                                                    TerrainCoordinateMode::Geographic);
                    int insertIndex = static_cast<int>(activeProfile.vertices.size());
                    bool foundVSegment = false;
                    for (int startVertexIndex = 0; startVertexIndex + 1 < static_cast<int>(activeProfile.vertices.size()); ++startVertexIndex)
                    {
                        if (activeProfile.vertices[static_cast<size_t>(startVertexIndex)].auxiliary)
                        {
                            continue;
                        }

                        int endVertexIndex = -1;
                        for (int candidateIndex = startVertexIndex + 1; candidateIndex < static_cast<int>(activeProfile.vertices.size()); ++candidateIndex)
                        {
                            if (!activeProfile.vertices[static_cast<size_t>(candidateIndex)].auxiliary)
                            {
                                endVertexIndex = candidateIndex;
                                break;
                            }
                        }
                        if (endVertexIndex < 0)
                        {
                            break;
                        }

                        const int startSampleIndex =
                            nearestSampleIndexForVertex(activeProfile.vertices[static_cast<size_t>(startVertexIndex)]);
                        const int endSampleIndex =
                            nearestSampleIndexForVertex(activeProfile.vertices[static_cast<size_t>(endVertexIndex)]);
                        if (startSampleIndex < 0 || endSampleIndex < 0)
                        {
                            continue;
                        }

                        const double startDistance =
                            activeProfile.samples[static_cast<size_t>(startSampleIndex)].distanceMeters;
                        const double endDistance =
                            activeProfile.samples[static_cast<size_t>(endSampleIndex)].distanceMeters;
                        const double minDistance = std::min(startDistance, endDistance);
                        const double maxDistance = std::max(startDistance, endDistance);
                        if (insertionSample.distanceMeters < minDistance || insertionSample.distanceMeters > maxDistance)
                        {
                            continue;
                        }

                        insertIndex = endVertexIndex;
                        for (int candidateIndex = startVertexIndex + 1; candidateIndex < endVertexIndex; ++candidateIndex)
                        {
                            const int candidateSampleIndex =
                                nearestSampleIndexForVertex(activeProfile.vertices[static_cast<size_t>(candidateIndex)]);
                            if (candidateSampleIndex >= 0 &&
                                activeProfile.samples[static_cast<size_t>(candidateSampleIndex)].distanceMeters >
                                    insertionSample.distanceMeters)
                            {
                                insertIndex = candidateIndex;
                                break;
                            }
                        }
                        foundVSegment = true;
                        break;
                    }
                    if (!foundVSegment)
                    {
                        m_StatusMessage = "A vertices from the height graph must be inserted between two V vertices.";
                    }
                    else
                    {
                        insertIndex = std::clamp(insertIndex, 1, static_cast<int>(activeProfile.vertices.size()) - 1);
                        activeProfile.vertices.insert(activeProfile.vertices.begin() + insertIndex, insertedVertex);
                        m_SelectedProfileVertexIndex = insertIndex;
                        m_SelectedProfileSampleIndex = -1;
                        m_HoveredProfileSampleIndex = -1;
                        RebuildTerrainProfileSamples(activeProfile);
                        m_StatusMessage = "Inserted auxiliary profile vertex between V vertices at " +
                                          std::to_string(insertionSample.distanceMeters) + " m.";
                    }
                }
                else
                {
                    m_SelectedProfileSampleIndex = nearestSample;
                }
            }
        }
    }
    // Graph zoom: scroll to zoom horizontally, double-click to reset
    if (ImGui::IsItemHovered())
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f)
        {
            const float  tX         = std::clamp((io.MousePos.x - plotTopLeft.x) / plotWidth, 0.0f, 1.0f);
            const double cursorDist = zoomedMinDist + static_cast<double>(tX) * zoomedRange;
            const double factor     = io.MouseWheel > 0.0f ? 0.70 : 1.4285;
            const double newRange   = std::clamp(zoomedRange * factor, totalDistance * 0.01, totalDistance);
            m_ProfileGraphZoomMinDist = std::max(cursorDist - static_cast<double>(tX) * newRange, 0.0);
            m_ProfileGraphZoomMaxDist = std::min(m_ProfileGraphZoomMinDist + newRange, totalDistance);
            SyncProfileMapToGraphZoom(activeProfile);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_ProfileGraphAuxiliaryInsertMode)
        {
            m_ProfileGraphZoomMinDist = 0.0;
            m_ProfileGraphZoomMaxDist = -1.0;
            m_ProfileMapViewInitialized = false; // also reset the map to full terrain view
        }
    }

    if (!foundHoveredSample)
    {
        m_HoveredProfileSampleIndex = -1;
        m_ProfileGraphHoverActive = false;
    }

    ImGui::Text("Elevation: %.2f m to %.2f m  Path length: %.2f m", graphMinHeight, graphMaxHeight, totalDistance);
    if (invalidSampleCount > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                           "Warning: %d sample(s) are outside terrain coverage, skipped in the graph, and flagged on export.",
                           invalidSampleCount);
    }
    ImGui::Text("Graph vertices: vertical markers show path vertices and distance from origin. Turn Draw on, then click the height graph to insert a vertex on the top-view line.");
}

void Application::ProcessOrientationGizmoInput()
{
    if (!m_ShowOrientationGizmo)
    {
        m_GizmoHovered = false;
        return;
    }

    constexpr float RADIUS = 68.0f;
    constexpr float ARM = 52.0f;
    constexpr float DRAG_DEG_PER_PX = 0.6f;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 center(vp->Pos.x + vp->Size.x - RADIUS - 20.0f,
                        vp->Pos.y + 44.0f + RADIUS + 20.0f);

    struct AxisDef {
        glm::vec3 dir;
        float snapYaw;
        float snapPitch;
    };
    const std::array<AxisDef, 3> axes = {{
        {{1, 0, 0}, 180.0f, 0.0f},
        {{0, 1, 0}, 0.0f, -89.0f},
        {{0, 0, 1}, -90.0f, 0.0f},
    }};

    const glm::mat4 rot = m_Camera.GetViewMatrixRotationOnly();
    struct ProjectedAxis { ImVec2 posTip; ImVec2 negTip; float depth; int idx; };
    std::array<ProjectedAxis, 3> proj;
    for (int i = 0; i < 3; ++i)
    {
        const glm::vec4 c = rot * glm::vec4(axes[static_cast<size_t>(i)].dir, 0.0f);
        proj[static_cast<size_t>(i)] = {
            ImVec2(center.x + c.x * ARM, center.y - c.y * ARM),
            ImVec2(center.x - c.x * ARM, center.y + c.y * ARM),
            c.z,
            i
        };
    }
    std::sort(proj.begin(), proj.end(), [](const ProjectedAxis& a, const ProjectedAxis& b) {
        return a.depth > b.depth;
    });

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const float mdx = mouse.x - center.x;
    const float mdy = mouse.y - center.y;
    const bool inCircle = (mdx * mdx + mdy * mdy) <= (RADIUS * RADIUS);
    m_GizmoHovered = inCircle;
    if (!inCircle)
    {
        return;
    }

    int hovAxis = -1;
    bool hovNeg = false;
    float bestDist = 20.0f * 20.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto& pa = proj[static_cast<size_t>(i)];
        const float dp = (mouse.x - pa.posTip.x) * (mouse.x - pa.posTip.x) +
                         (mouse.y - pa.posTip.y) * (mouse.y - pa.posTip.y);
        if (dp < bestDist)
        {
            bestDist = dp;
            hovAxis = i;
            hovNeg = false;
        }

        const float dn = (mouse.x - pa.negTip.x) * (mouse.x - pa.negTip.x) +
                         (mouse.y - pa.negTip.y) * (mouse.y - pa.negTip.y);
        if (dn < bestDist)
        {
            bestDist = dn;
            hovAxis = i;
            hovNeg = true;
        }
    }

    if (io.MouseDown[0] && !io.MouseClicked[0])
    {
        const ImVec2 delta = io.MouseDelta;
        if (delta.x != 0.0f || delta.y != 0.0f)
        {
            const glm::vec2 lookDelta(delta.x * DRAG_DEG_PER_PX, -delta.y * DRAG_DEG_PER_PX);
            m_PendingCameraCommand.cancelSnap = true;
            m_PendingCameraCommand.lookDeltaDegrees += lookDelta;
            m_Diagnostics.queuedLookDeltaDegrees += lookDelta;
            m_FPSController.ResetMouseState();
        }
        return;
    }

    if (io.MouseClicked[0])
    {
        if (hovAxis >= 0)
        {
            const AxisDef& ax = axes[static_cast<size_t>(proj[static_cast<size_t>(hovAxis)].idx)];
            if (!hovNeg)
            {
                SnapCameraView(ax.snapYaw, ax.snapPitch);
            }
            else
            {
                SnapCameraView(ax.snapYaw + 180.0f, -ax.snapPitch);
            }
        }
        else
        {
            SnapCameraView(0.0f, -20.0f);
        }
    }
}

void Application::RenderOrientationGizmo()
{
    if (!m_ShowOrientationGizmo)
    {
        m_GizmoHovered = false;
        return;
    }

    // ── Layout ───────────────────────────────────────────────────────────────
    constexpr float RADIUS    = 68.0f;
    constexpr float ARM       = 52.0f;
    constexpr float TIP_R     = 8.0f;
    constexpr float TIP_R_DIM = 5.0f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 center(vp->Pos.x + vp->Size.x - RADIUS - 20.0f,
                        vp->Pos.y + 44.0f + RADIUS + 20.0f);

    // ── Axis definitions ─────────────────────────────────────────────────────
    struct AxisDef {
        glm::vec3   dir;
        ImU32       color;
        ImU32       colorDim;
        const char* label;
        float       snapYaw;
        float       snapPitch;
    };
    const std::array<AxisDef, 3> axes = {{
        { { 1, 0, 0}, IM_COL32(220,  70,  70, 255), IM_COL32(220,  70,  70, 100), "X",  180.0f,   0.0f },
        { { 0, 1, 0}, IM_COL32( 70, 205, 100, 255), IM_COL32( 70, 205, 100, 100), "Y",    0.0f, -89.0f },
        { { 0, 0, 1}, IM_COL32( 75, 140, 225, 255), IM_COL32( 75, 140, 225, 100), "Z",  -90.0f,   0.0f },
    }};

    // ── Project axes through view rotation ───────────────────────────────────
    const glm::mat4 rot = m_Camera.GetViewMatrixRotationOnly();
    struct ProjectedAxis { ImVec2 posTip; ImVec2 negTip; float depth; int idx; };
    std::array<ProjectedAxis, 3> proj;
    for (int i = 0; i < 3; ++i)
    {
        const glm::vec4 c = rot * glm::vec4(axes[static_cast<size_t>(i)].dir, 0.0f);
        proj[static_cast<size_t>(i)] = {
            ImVec2(center.x + c.x * ARM, center.y - c.y * ARM),
            ImVec2(center.x - c.x * ARM, center.y + c.y * ARM),
            c.z, i
        };
    }
    std::sort(proj.begin(), proj.end(), [](const ProjectedAxis& a, const ProjectedAxis& b){
        return a.depth > b.depth;
    });

    // ── Hover detection ───────────────────────────────────────────────────────
    const ImGuiIO& io  = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const float  mdx   = mouse.x - center.x;
    const float  mdy   = mouse.y - center.y;
    const bool inCircle = (mdx * mdx + mdy * mdy) <= (RADIUS * RADIUS);
    m_GizmoHovered = inCircle;

    // ── Draw background ───────────────────────────────────────────────────────
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImU32 bgColor = inCircle ? IM_COL32(28, 38, 52, 200) : IM_COL32(18, 24, 32, 165);
    dl->AddCircleFilled(center, RADIUS, bgColor, 48);
    dl->AddCircle      (center, RADIUS, inCircle ? IM_COL32(140, 170, 210, 140) : IM_COL32(90, 110, 130, 90), 48, 1.2f);

    // ── Draw axes ────────────────────────────────────────────────────────────
    for (const ProjectedAxis& pa : proj)
    {
        const AxisDef& ax = axes[static_cast<size_t>(pa.idx)];
        dl->AddLine(center, pa.negTip, ax.colorDim, 1.5f);
        dl->AddCircleFilled(pa.negTip, TIP_R_DIM, ax.colorDim, 16);
        dl->AddLine(center, pa.posTip, ax.color, 3.5f);
        dl->AddCircleFilled(pa.posTip, TIP_R, ax.color, 16);
        dl->AddText(ImVec2(pa.posTip.x - 4.0f, pa.posTip.y - 7.0f),
                    IM_COL32(255, 255, 255, 230), ax.label);
    }

    if (!inCircle)
        return;

    // ── Find nearest axis tip ────────────────────────────────────────────────
    int   hovAxis  = -1;
    bool  hovNeg   = false;
    float bestDist = 20.0f * 20.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto& pa = proj[static_cast<size_t>(i)];
        const float dp = (mouse.x-pa.posTip.x)*(mouse.x-pa.posTip.x) + (mouse.y-pa.posTip.y)*(mouse.y-pa.posTip.y);
        if (dp < bestDist) { bestDist = dp; hovAxis = i; hovNeg = false; }
        const float dn = (mouse.x-pa.negTip.x)*(mouse.x-pa.negTip.x) + (mouse.y-pa.negTip.y)*(mouse.y-pa.negTip.y);
        if (dn < bestDist) { bestDist = dn; hovAxis = i; hovNeg = true; }
    }

    // Highlight hovered tip
    if (hovAxis >= 0)
    {
        const ProjectedAxis& pa  = proj[static_cast<size_t>(hovAxis)];
        const ImVec2&        tip = hovNeg ? pa.negTip : pa.posTip;
        dl->AddCircle(tip, TIP_R + 4.0f, IM_COL32(255, 255, 255, 220), 16, 2.0f);
    }

    // ── Drag: free-rotate view ───────────────────────────────────────────────
    if (io.MouseDown[0] && !io.MouseClicked[0])  // held (not just-pressed)
    {
        dl->AddCircle(center, RADIUS - 4.0f, IM_COL32(255, 255, 255, 50), 48, 1.0f);
        return;  // don't process click-to-snap while dragging
    }

    // ── Tooltip ──────────────────────────────────────────────────────────────
    if (hovAxis >= 0)
    {
        const AxisDef& ax = axes[static_cast<size_t>(proj[static_cast<size_t>(hovAxis)].idx)];
        ImGui::SetTooltip("Click: snap to %s%s view\nDrag: orbit freely", hovNeg ? "-" : "+", ax.label);
    }
    else
    {
        ImGui::SetTooltip("Click: reset view\nDrag: orbit freely\nNumpad 1/3/7 = Front/Right/Top");
    }
}

void Application::RenderEditor()
{
    if (!m_ShowWorkspaceWindow)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 18.0f, viewport->Pos.y + 44.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!ImGui::Begin("GeoFPS Workspace", &m_ShowWorkspaceWindow, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImGui::PopStyleVar();

    const float sidebarWidth = 184.0f;
    const float footerHeight = 72.0f;
    const ImVec2 content = ImGui::GetContentRegionAvail();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.075f, 0.090f, 0.98f));
    ImGui::BeginChild("##workspace_sidebar", ImVec2(sidebarWidth, std::max(content.y - footerHeight, 320.0f)), true);
    ImGui::TextUnformatted("GeoFPS");
    ImGui::TextDisabled("Geospatial 3D workspace");
    ImGui::Separator();

    const auto navButton = [&](WorkspaceSection section, const char* shortcut) {
        const bool selected = m_ActiveWorkspaceSection == section;
        ImGui::PushID(static_cast<int>(section));
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.38f, 0.43f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.44f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.52f, 0.58f, 1.0f));
        }
        if (ImGui::Button(WorkspaceSectionLabel(section), ImVec2(-1.0f, 34.0f)))
        {
            m_ActiveWorkspaceSection = section;
        }
        if (selected)
        {
            ImGui::PopStyleColor(3);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", WorkspaceSectionHint(section));
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shortcut);
        ImGui::PopID();
    };

    navButton(WorkspaceSection::World, "files");
    navButton(WorkspaceSection::Terrain, "data");
    navButton(WorkspaceSection::Profiles, "lines");
    navButton(WorkspaceSection::Assets, "glb");
    navButton(WorkspaceSection::Lighting, "sun");
    navButton(WorkspaceSection::Diagnostics, "logs");

    ImGui::Separator();
    ImGui::TextWrapped("Detailed task panels are opened from Panels in the top menu.");

    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::BeginChild("##workspace_main", ImVec2(0.0f, std::max(content.y - footerHeight, 320.0f)), false);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
    ImGui::BeginChild("##workspace_card", ImVec2(0.0f, 0.0f), true);

    ImGui::Text("%s", WorkspaceSectionLabel(m_ActiveWorkspaceSection));
    ImGui::TextDisabled("%s", WorkspaceSectionHint(m_ActiveWorkspaceSection));
    ImGui::Separator();

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    ImportedAsset* activeAsset = GetActiveImportedAsset();

    if (m_ActiveWorkspaceSection == WorkspaceSection::World)
    {
        char worldNameBuffer[256];
        std::snprintf(worldNameBuffer, sizeof(worldNameBuffer), "%s", m_WorldName.c_str());
        if (ImGui::InputText("World Name", worldNameBuffer, sizeof(worldNameBuffer)))
        {
            m_WorldName = worldNameBuffer;
        }

        char worldPathBuffer[512];
        std::snprintf(worldPathBuffer, sizeof(worldPathBuffer), "%s", m_WorldFilePath.c_str());
        if (ImGui::InputText("World File", worldPathBuffer, sizeof(worldPathBuffer)))
        {
            m_WorldFilePath = worldPathBuffer;
        }
        if (ImGui::Button("Save World", ImVec2(128.0f, 32.0f)))
        {
            SaveWorldToFile(m_WorldFilePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load World", ImVec2(128.0f, 32.0f)))
        {
            LoadWorldFromFile(m_WorldFilePath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Write Readout", ImVec2(128.0f, 32.0f)))
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

        ImGui::SeparatorText("Camera");
        if (ImGui::SliderFloat("Move Speed", &m_BaseMoveSpeed, 1.0f, 200.0f, "%.1f m/s"))
        {
            m_BaseMoveSpeed = std::clamp(m_BaseMoveSpeed, 0.5f, 3000.0f);
        }
        float sprintMultiplier = m_FPSController.GetSprintMultiplier();
        if (ImGui::SliderFloat("Sprint Multiplier", &sprintMultiplier, 1.0f, 10.0f, "%.1fx"))
        {
            m_FPSController.SetSprintMultiplier(sprintMultiplier);
        }
        float nearClip = m_Camera.GetNearClip();
        if (ImGui::SliderFloat("Near Clip", &nearClip, 0.1f, 100.0f, "%.1f m"))
        {
            m_Camera.SetNearClip(nearClip);
        }
        float farClip = m_Camera.GetFarClip();
        if (ImGui::SliderFloat("Far Clip", &farClip, 1000.0f, 200000.0f, "%.0f m"))
        {
            m_Camera.SetFarClip(farClip);
        }

        // ── Gravity / Terrain Collision ───────────────────────────────────────
        ImGui::SeparatorText("Gravity & Collision");
        ImGui::Checkbox("Enable Gravity", &m_GravitySettings.enabled);
        if (m_GravitySettings.enabled)
        {
            ImGui::DragFloat("Player Height (m)", &m_GravitySettings.playerHeightMeters, 0.05f, 0.3f, 5.0f, "%.2f");
            ImGui::DragFloat("Jump Height (m)",   &m_GravitySettings.jumpHeightMeters,   0.1f,  0.5f, 20.0f, "%.1f");
            ImGui::DragFloat("Gravity (m/s²)",    &m_GravitySettings.gravityAcceleration, 0.1f, 1.0f, 30.0f, "%.1f");
            ImGui::Text("On ground: %s   Vertical vel: %.1f m/s",
                        m_OnGround ? "yes" : "no", m_VerticalVelocity);
            ImGui::TextDisabled("Space = jump");
        }

        // ── Elevation-scaled Speed ────────────────────────────────────────────
        ImGui::SeparatorText("Elevation-Scaled Speed");
        ImGui::Checkbox("Enable Elevation Speed", &m_ElevationSpeedSettings.enabled);
        if (m_ElevationSpeedSettings.enabled)
        {
            ImGui::DragFloat("Reference Height (m)", &m_ElevationSpeedSettings.referenceHeight, 1.0f, 1.0f, 50000.0f, "%.0f");
            ImGui::DragFloat("Log Scale",            &m_ElevationSpeedSettings.logScale,        0.01f, 0.01f, 10.0f, "%.2f");
            ImGui::DragFloat2("Min / Max Multiplier", &m_ElevationSpeedSettings.minMultiplier,  0.01f, 0.01f, 100.0f, "%.2f");
        }
    }
    else if (m_ActiveWorkspaceSection == WorkspaceSection::Terrain)
    {
        ImGui::Text("Terrain Datasets");
        if (activeTerrain == nullptr)
        {
            ImGui::Text("No terrain dataset available.");
        }
        else
        {
            for (int i = 0; i < static_cast<int>(m_TerrainDatasets.size()); ++i)
            {
                TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::Checkbox("##workspace_terrain_visible", &dataset.visible);
                ImGui::SameLine();
                if (ImGui::Selectable(dataset.name.c_str(), i == m_ActiveTerrainIndex))
                {
                    ActivateTerrainDataset(i);
                    activeTerrain = GetActiveTerrainDataset();
                }
                ImGui::SameLine();
                const bool terrainReady = dataset.loaded && (dataset.hasTileManifest || dataset.mesh != nullptr);
                DrawStatusBadge(terrainReady ? "Ready" : "Not Loaded", terrainReady);
                ImGui::PopID();
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
            ImGui::SameLine();
            if (ImGui::Button("Browse##workspace_terrain_csv"))
            {
                const std::string selectedPath =
                    OpenNativeFileDialog("Load Terrain CSV", {{"CSV", {".csv"}}, {"Text", {".txt"}}});
                if (!selectedPath.empty())
                {
                    activeTerrain->path = selectedPath;
                }
            }

            // ── Tile manifest ─────────────────────────────────────────────────
            ImGui::Spacing();
            ImGui::Checkbox("Use Tile Manifest##ws", &activeTerrain->hasTileManifest);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Load terrain from a JSON manifest file that references\n"
                                  "multiple CSV tiles instead of a single CSV file.");
            if (activeTerrain->hasTileManifest)
            {
                char manifestBuffer[512];
                std::snprintf(manifestBuffer, sizeof(manifestBuffer),
                              "%s", activeTerrain->tileManifestPath.c_str());
                ImGui::SetNextItemWidth(-60.0f);
                if (ImGui::InputText("##ws_manifest_path", manifestBuffer, sizeof(manifestBuffer)))
                    activeTerrain->tileManifestPath = manifestBuffer;
                ImGui::SameLine();
                if (ImGui::Button("Browse##ws_manifest"))
                {
                    const std::string sel = OpenNativeFileDialog(
                        "Load Tile Manifest", {{"JSON Manifest", {".json"}}});
                    if (!sel.empty())
                        activeTerrain->tileManifestPath = sel;
                }
                ImGui::TextDisabled("Manifest (JSON)");
                if (!activeTerrain->tileManifestPath.empty() &&
                    !PathExists(activeTerrain->tileManifestPath))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f),
                                       "Manifest file does not exist.");
                }
            }
            ImGui::Spacing();

            ImGui::InputDouble("Origin Latitude", &activeTerrain->geoReference.originLatitude, 0.0, 0.0, "%.8f");
            ImGui::InputDouble("Origin Longitude", &activeTerrain->geoReference.originLongitude, 0.0, 0.0, "%.8f");
            ImGui::InputDouble("Origin Height", &activeTerrain->geoReference.originHeight, 0.0, 0.0, "%.3f");
            if (ImGui::Button("Draft 128##workspace_terrain_resolution"))
            {
                activeTerrain->settings.gridResolutionX = 128;
                activeTerrain->settings.gridResolutionZ = 128;
            }
            ImGui::SameLine();
            if (ImGui::Button("Balanced 256##workspace_terrain_resolution"))
            {
                activeTerrain->settings.gridResolutionX = 256;
                activeTerrain->settings.gridResolutionZ = 256;
            }
            ImGui::SameLine();
            if (ImGui::Button("High 384##workspace_terrain_resolution"))
            {
                activeTerrain->settings.gridResolutionX = 384;
                activeTerrain->settings.gridResolutionZ = 384;
            }
            ImGui::SameLine();
            if (ImGui::Button("Max 512##workspace_terrain_resolution"))
            {
                activeTerrain->settings.gridResolutionX = 512;
                activeTerrain->settings.gridResolutionZ = 512;
            }
            ImGui::SameLine();
            if (ImGui::Button("Ultra 1024##workspace_terrain_resolution"))
            {
                activeTerrain->settings.gridResolutionX = 1024;
                activeTerrain->settings.gridResolutionZ = 1024;
            }
            ImGui::SliderInt("Grid X", &activeTerrain->settings.gridResolutionX, 8, 1024);
            ImGui::SliderInt("Grid Z", &activeTerrain->settings.gridResolutionZ, 8, 1024);
            ImGui::SliderFloat("Height Scale", &activeTerrain->settings.heightScale, 0.1f, 5.0f);
            ImGui::SliderInt("Smoothing Passes", &activeTerrain->settings.smoothingPasses, 0, 4);
            ImGui::SliderInt("Import Sample Step", &activeTerrain->settings.importSampleStep, 1, 32);
            ImGui::SliderInt("Chunk Resolution", &activeTerrain->settings.chunkResolution, 16, 128);
            ImGui::Checkbox("Color By Height", &activeTerrain->settings.colorByHeight);
            ImGui::Checkbox("Auto Height Color Range", &activeTerrain->settings.autoHeightColorRange);
            if (!activeTerrain->settings.autoHeightColorRange)
            {
                ImGui::InputFloat("Height Color Min", &activeTerrain->settings.heightColorMin, 0.0f, 0.0f, "%.2f");
                ImGui::InputFloat("Height Color Max", &activeTerrain->settings.heightColorMax, 0.0f, 0.0f, "%.2f");
            }
            ImGui::ColorEdit3("Low Height Color", &activeTerrain->settings.lowHeightColor.x);
            ImGui::ColorEdit3("Mid Height Color", &activeTerrain->settings.midHeightColor.x);
            ImGui::ColorEdit3("High Height Color", &activeTerrain->settings.highHeightColor.x);
            if (ImGui::BeginCombo("Coordinate Mode", TerrainCoordinateModeLabel(activeTerrain->settings.coordinateMode)))
            {
                if (ImGui::Selectable("Geographic lat/lon/height",
                                      activeTerrain->settings.coordinateMode == TerrainCoordinateMode::Geographic))
                {
                    activeTerrain->settings.coordinateMode = TerrainCoordinateMode::Geographic;
                    activeTerrain->settings.crs = GeoConverter::ParseCrs("EPSG:4326");
                }
                if (ImGui::Selectable("Local meters X/Z/height",
                                      activeTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters))
                {
                    activeTerrain->settings.coordinateMode = TerrainCoordinateMode::LocalMeters;
                    activeTerrain->settings.crs = GeoConverter::ParseCrs("LOCAL_METERS");
                }
                if (ImGui::Selectable("Projected CRS meters",
                                      activeTerrain->settings.coordinateMode == TerrainCoordinateMode::Projected))
                {
                    activeTerrain->settings.coordinateMode = TerrainCoordinateMode::Projected;
                    if (activeTerrain->settings.crs.kind == CrsKind::GeographicWgs84)
                    {
                        activeTerrain->settings.crs = GeoConverter::ParseCrs("EPSG:3857");
                    }
                }
                ImGui::EndCombo();
            }
            char crsBuffer[128];
            std::snprintf(crsBuffer, sizeof(crsBuffer), "%s", activeTerrain->settings.crs.id.c_str());
            if (ImGui::InputText("CRS", crsBuffer, sizeof(crsBuffer)))
            {
                activeTerrain->settings.crs = GeoConverter::ParseCrs(crsBuffer);
            }

            if (ImGui::Button("Queue Active Terrain Build", ImVec2(210.0f, 34.0f)))
            {
                StartTerrainBuildJob(m_ActiveTerrainIndex);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Dataset", ImVec2(120.0f, 34.0f)))
            {
                TerrainDataset dataset;
                dataset.name = "Terrain " + std::to_string(m_TerrainDatasets.size() + 1);
                dataset.path = activeTerrain->path;
                dataset.settings = activeTerrain->settings;
                dataset.geoReference = activeTerrain->geoReference;
                dataset.overlays.push_back(OverlayEntry {});
                m_TerrainDatasets.push_back(std::move(dataset));
                ActivateTerrainDataset(static_cast<int>(m_TerrainDatasets.size()) - 1);
                activeTerrain = GetActiveTerrainDataset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Dataset", ImVec2(130.0f, 34.0f)))
            {
                DeleteTerrainDataset(m_ActiveTerrainIndex);
                activeTerrain = GetActiveTerrainDataset();
            }

            ImGui::SeparatorText("Aerial Overlay");
            OverlayEntry* activeOverlay = GetActiveOverlayEntry();
            if (activeOverlay != nullptr)
            {
                ImGui::Checkbox("Enable Overlay", &activeOverlay->image.enabled);
                ImGui::SameLine();
                ImGui::SliderFloat("Opacity", &activeOverlay->image.opacity, 0.0f, 1.0f);
                char overlayPathBuffer[512];
                std::snprintf(overlayPathBuffer, sizeof(overlayPathBuffer), "%s", activeOverlay->image.imagePath.c_str());
                if (ImGui::InputText("Overlay Image", overlayPathBuffer, sizeof(overlayPathBuffer)))
                {
                    activeOverlay->image.imagePath = overlayPathBuffer;
                }
                ImGui::SameLine();
                if (ImGui::Button("Browse##workspace_overlay"))
                {
                    const std::string selectedPath =
                        OpenNativeFileDialog("Load Aerial Overlay Image",
                                             {{"Images", {".png", ".jpg", ".jpeg", ".bmp", ".tga"}}});
                    if (!selectedPath.empty())
                    {
                        activeOverlay->image.imagePath = selectedPath;
                    }
                }
                if (ImGui::Button("Load Overlay", ImVec2(140.0f, 30.0f)))
                {
                    LoadOverlayImage(*activeOverlay);
                }
                ImGui::SameLine();
                if (ImGui::Button("Fit To Terrain", ImVec2(140.0f, 30.0f)))
                {
                    ResetOverlayToTerrainBounds(activeOverlay->image);
                }
            }

            if (activeTerrain->loaded && activeTerrain->bounds.valid)
            {
                const TerrainBounds bounds = ToTerrainBounds(activeTerrain->bounds);
                ImGui::SeparatorText("Metadata");
                ImGui::Text("Points: %zu", activeTerrain->points.size());
                ImGui::Text("Latitude %.8f to %.8f", bounds.minLatitude, bounds.maxLatitude);
                ImGui::Text("Longitude %.8f to %.8f", bounds.minLongitude, bounds.maxLongitude);
                ImGui::Text("Height %.2f m to %.2f m", bounds.minHeight, bounds.maxHeight);
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Load Visible Terrains", ImVec2(180.0f, 34.0f)))
        {
            int loadedCount = 0;
            for (TerrainDataset& dataset : m_TerrainDatasets)
            {
                if (dataset.visible && (dataset.loaded || StartTerrainBuildJob(static_cast<int>(&dataset - m_TerrainDatasets.data()))))
                {
                    ++loadedCount;
                }
            }
            m_StatusMessage = "Loaded or queued visible terrains: " + std::to_string(loadedCount);
        }

        // ── LOD Tile Streaming ────────────────────────────────────────────────
        ImGui::SeparatorText("LOD Tile Streaming");
        ImGui::Checkbox("Enable LOD Streaming", &m_TileLODSettings.enabled);
        if (m_TileLODSettings.enabled)
        {
            ImGui::DragFloat("Near Radius (m)",   &m_TileLODSettings.nearRadiusMeters,   10.0f, 100.0f,   50000.0f, "%.0f");
            ImGui::DragFloat("Mid Radius (m)",    &m_TileLODSettings.midRadiusMeters,    10.0f, 100.0f,  100000.0f, "%.0f");
            ImGui::DragFloat("Unload Radius (m)", &m_TileLODSettings.unloadRadiusMeters, 10.0f, 500.0f,  200000.0f, "%.0f");
            ImGui::DragInt("Max Concurrent Loads", &m_TileLODSettings.maxConcurrentLoads, 1, 1, 16);
        }
    }
    else if (m_ActiveWorkspaceSection == WorkspaceSection::Profiles)
    {
        ImGui::Text("Profiles: %zu", m_TerrainProfiles.size());
        if (m_TerrainProfiles.empty() && ImGui::Button("Create First Profile", ImVec2(180.0f, 34.0f)))
        {
            TerrainProfile profile;
            profile.name = "Profile 1";
            m_TerrainProfiles.push_back(profile);
            m_ActiveTerrainProfileIndex = 0;
        }
        if (!m_TerrainProfiles.empty())
        {
            m_ActiveTerrainProfileIndex = std::clamp(m_ActiveTerrainProfileIndex, 0, static_cast<int>(m_TerrainProfiles.size()) - 1);
            TerrainProfile& profile = m_TerrainProfiles[static_cast<size_t>(m_ActiveTerrainProfileIndex)];
            EnsureTerrainProfileHasTerrainSelection(profile);

            if (ImGui::BeginCombo("Active Profile", profile.name.c_str()))
            {
                for (int index = 0; index < static_cast<int>(m_TerrainProfiles.size()); ++index)
                {
                    const bool selected = index == m_ActiveTerrainProfileIndex;
                    if (ImGui::Selectable(m_TerrainProfiles[static_cast<size_t>(index)].name.c_str(), selected))
                    {
                        m_ActiveTerrainProfileIndex = index;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            char profileNameBuffer[256];
            std::snprintf(profileNameBuffer, sizeof(profileNameBuffer), "%s", profile.name.c_str());
            if (ImGui::InputText("Profile Name", profileNameBuffer, sizeof(profileNameBuffer)))
            {
                profile.name = profileNameBuffer;
            }
            ImGui::Checkbox("Visible", &profile.visible);
            ImGui::SameLine();
            ImGui::Checkbox("Show In 3D World", &profile.showInWorld);
            bool useLocalCoordinates = profile.useLocalCoordinates;
            if (ImGui::Checkbox("Use Local XYZ Coordinates", &useLocalCoordinates))
            {
                const TerrainDataset* profileTerrain = GetPrimaryTerrainForProfile(profile);
                GeoConverter converter(profileTerrain != nullptr ? profileTerrain->geoReference : m_GeoReference);
                const TerrainCoordinateMode coordinateMode = profileTerrain != nullptr ?
                                                                 profileTerrain->settings.coordinateMode :
                                                                 TerrainCoordinateMode::Geographic;
                if (useLocalCoordinates && !profile.useLocalCoordinates)
                {
                    for (TerrainProfileVertex& vertex : profile.vertices)
                    {
                        vertex.localPosition = profileTerrain != nullptr ?
                                                   TerrainCoordinateToLocal(*profileTerrain, vertex.latitude, vertex.longitude, 0.0) :
                                                   converter.ToLocal(vertex.latitude, vertex.longitude, 0.0);
                    }
                }
                else if (!useLocalCoordinates && profile.useLocalCoordinates)
                {
                    for (TerrainProfileVertex& vertex : profile.vertices)
                    {
                        const glm::dvec3 geographic =
                            coordinateMode == TerrainCoordinateMode::LocalMeters ?
                                glm::dvec3(vertex.localPosition.x, vertex.localPosition.z, vertex.localPosition.y) :
                                converter.ToGeographic(vertex.localPosition);
                        vertex.latitude = geographic.x;
                        vertex.longitude = geographic.y;
                    }
                }
                profile.useLocalCoordinates = useLocalCoordinates;
                RebuildTerrainProfileSamples(profile);
            }
            ImGui::SliderFloat("2D Thickness", &profile.thickness, 1.0f, 12.0f, "%.1f px");
            ImGui::SliderFloat("3D Thickness", &profile.worldThicknessMeters, 1.0f, 250.0f, "%.1f m");
            ImGui::SliderFloat("3D Height Above Terrain", &profile.worldGroundOffsetMeters, 0.0f, 500.0f, "%.1f m");
            ImGui::SliderFloat("Sample Spacing", &profile.sampleSpacingMeters, 0.5f, 50.0f, "%.1f m");
            ImGui::Text("Vertices: %zu  Samples: %zu", profile.vertices.size(), profile.samples.size());
            DrawStatusBadge(profile.showInWorld ? "In 3D World" : "Map Only", profile.showInWorld);

            if (ImGui::Button("Rebuild Samples", ImVec2(150.0f, 34.0f)))
            {
                RebuildTerrainProfileSamples(profile);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Open the top-view editor from Panels > Terrain Profiles.");
        }
    }
    else if (m_ActiveWorkspaceSection == WorkspaceSection::Assets)
    {
        ImGui::Text("Imported assets: %zu", m_ImportedAssets.size());
        if (activeAsset != nullptr)
        {
            for (int i = 0; i < static_cast<int>(m_ImportedAssets.size()); ++i)
            {
                ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::Checkbox("##workspace_asset_selected", &asset.selected);
                ImGui::SameLine();
                if (ImGui::Selectable(asset.name.c_str(), i == m_ActiveImportedAssetIndex))
                {
                    m_ActiveImportedAssetIndex = i;
                    activeAsset = GetActiveImportedAsset();
                }
                ImGui::SameLine();
                DrawStatusBadge(asset.loaded ? "Loaded" : "Not Loaded", asset.loaded);
                ImGui::PopID();
            }

            char assetNameBuffer[256];
            std::snprintf(assetNameBuffer, sizeof(assetNameBuffer), "%s", activeAsset->name.c_str());
            if (ImGui::InputText("Asset Name", assetNameBuffer, sizeof(assetNameBuffer)))
            {
                activeAsset->name = assetNameBuffer;
            }
            char assetPathBuffer[512];
            std::snprintf(assetPathBuffer, sizeof(assetPathBuffer), "%s", activeAsset->path.c_str());
            if (ImGui::InputText("Asset File", assetPathBuffer, sizeof(assetPathBuffer)))
            {
                activeAsset->path = assetPathBuffer;
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse##workspace_asset"))
            {
                const std::string selectedPath =
                    OpenNativeFileDialog("Load Blender Asset", {{"Blender/3D Assets", {".glb", ".gltf", ".obj"}}});
                if (!selectedPath.empty())
                {
                    activeAsset->path = selectedPath;
                }
            }

            if (ImGui::Button("Queue Asset Import", ImVec2(160.0f, 34.0f)))
            {
                StartImportedAssetLoadJob(m_ActiveImportedAssetIndex);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Asset", ImVec2(120.0f, 34.0f)))
            {
                ImportedAsset asset;
                asset.name = "Asset " + std::to_string(m_ImportedAssets.size() + 1);
                m_ImportedAssets.push_back(std::move(asset));
                m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
                activeAsset = GetActiveImportedAsset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Asset", ImVec2(120.0f, 34.0f)))
            {
                DeleteImportedAsset(m_ActiveImportedAssetIndex);
                activeAsset = GetActiveImportedAsset();
            }

            ImGui::Checkbox("Use Geographic Placement", &activeAsset->useGeographicPlacement);
            if (activeAsset->useGeographicPlacement)
            {
                ImGui::InputDouble("Latitude", &activeAsset->latitude, 0.0, 0.0, "%.8f");
                ImGui::InputDouble("Longitude", &activeAsset->longitude, 0.0, 0.0, "%.8f");
                ImGui::InputDouble("Height", &activeAsset->height, 0.0, 0.0, "%.3f");
                if (ImGui::Button("Apply Geographic Position", ImVec2(210.0f, 30.0f)))
                {
                    UpdateImportedAssetPositionFromGeographic(*activeAsset);
                }
            }
            ImGui::DragFloat3("Position XYZ", &activeAsset->position.x, 0.25f);
            ImGui::DragFloat("Rotation Z", &activeAsset->rotationDegrees.z, 1.0f, -360.0f, 360.0f, "%.1f deg");
            ImGui::DragFloat3("Scale XYZ", &activeAsset->scale.x, 0.02f, 0.01f, 100.0f);
            ImGui::ColorEdit3("Tint", &activeAsset->tint.x);
            ImGui::Text("Selected assets: %zu", GetSelectedImportedAssetCount());
        }

        if (ImGui::Button("Import Asset List", ImVec2(170.0f, 34.0f)))
        {
            LoadBlenderAssetsFromFile(m_BlenderAssetsFilePath);
        }
    }
    else if (m_ActiveWorkspaceSection == WorkspaceSection::Lighting)
    {
        RenderSunControls();
        ImGui::TextDisabled("Open the detailed lighting panel from Panels > Sun Illumination.");
    }
    else
    {
        RenderDiagnosticsPanel();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("##workspace_activity", ImVec2(0.0f, footerHeight - 8.0f), false);
    ImGui::TextDisabled("Activity");
    ImGui::SameLine();
    if (!m_StatusMessage.empty())
    {
        ImGui::TextWrapped("%s", m_StatusMessage.c_str());
    }
    else
    {
        ImGui::TextWrapped("Ready. Open a workspace from the left rail or use the top menu for detailed panels.");
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace GeoFPS
