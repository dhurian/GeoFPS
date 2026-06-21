#include "Core/Application.h"
#include "Core/ApplicationInternal.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/ImGuiWindowControls.h"
#include "Core/NativeFileDialog.h"
#include "Core/TerrainCoordinateHelpers.h"

#include "imgui.h"
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

void Application::RenderTerrainDatasetWindow()
{
    if (!m_ShowTerrainDatasetWindow)
    {
        return;
    }

    TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    const bool datasetsWindowOpen = ImGui::Begin("Terrain Datasets", &m_ShowTerrainDatasetWindow);
    DrawWindowArrangeMenu("Terrain Datasets");
    if (!datasetsWindowOpen)
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

    for (int i = 0; i < static_cast<int>(m_Terrain.datasets().size()); ++i)
    {
        TerrainDataset& dataset = m_Terrain.datasets()[static_cast<size_t>(i)];
        const bool selected = i == m_Terrain.activeIndex();
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
        StartTerrainBuildJob(m_Terrain.activeIndex());
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Terrain Dataset"))
    {
        TerrainDataset dataset;
        dataset.name = "Terrain " + std::to_string(m_Terrain.datasets().size() + 1);
        dataset.path = activeTerrain->path;
        dataset.settings = activeTerrain->settings;
        dataset.overlays.push_back(OverlayEntry {});
        m_Terrain.datasets().push_back(std::move(dataset));
        ActivateTerrainDataset(static_cast<int>(m_Terrain.datasets().size()) - 1);
        activeTerrain = GetActiveTerrainDataset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Active Terrain"))
    {
        DeleteTerrainDataset(m_Terrain.activeIndex());
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
        StartTerrainBuildJob(m_Terrain.activeIndex());
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
        for (TerrainDataset& dataset : m_Terrain.datasets())
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
            else if (StartTerrainBuildJob(static_cast<int>(&dataset - m_Terrain.datasets().data())))
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

    const bool aerialWindowOpen = ImGui::Begin("Aerial Overlay", &m_ShowAerialOverlayWindow);
    DrawWindowArrangeMenu("Aerial Overlay");
    if (!aerialWindowOpen)
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
} // namespace GeoFPS
