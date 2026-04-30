#include "Core/Application.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/NativeFileDialog.h"

#include "imgui.h"
#include <string>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationUIInternal;

void Application::RenderMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("Open World..."))
        {
            const std::string selectedPath = OpenNativeFileDialog("Load GeoFPS World", {{"GeoFPS World", {".geofpsworld"}}, {"Text", {".txt"}}});
            if (!selectedPath.empty())
            {
                m_WorldFilePath = selectedPath;
                LoadWorldFromFile(m_WorldFilePath);
            }
            else
            {
                m_StatusMessage = "No world file selected.";
            }
        }

        if (ImGui::MenuItem("Save World"))
        {
            SaveWorldToFile(m_WorldFilePath);
        }

        if (ImGui::MenuItem("Save World As..."))
        {
            const std::string selectedPath = SaveNativeFileDialog("Save GeoFPS World",
                                                                  {{"GeoFPS World", {".geofpsworld"}}},
                                                                  FileNameFromPath(m_WorldFilePath, "world.geofpsworld"));
            if (!selectedPath.empty())
            {
                m_WorldFilePath = selectedPath;
                SaveWorldToFile(m_WorldFilePath);
            }
            else
            {
                m_StatusMessage = "No world save location selected.";
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Import Blender Asset List..."))
        {
            const std::string selectedPath = OpenNativeFileDialog("Import Blender Assets File", {{"Text", {".txt"}}, {"GeoFPS World", {".geofpsworld"}}});
            if (!selectedPath.empty())
            {
                m_BlenderAssetsFilePath = selectedPath;
                LoadBlenderAssetsFromFile(m_BlenderAssetsFilePath);
            }
            else
            {
                m_StatusMessage = "No Blender assets file selected.";
            }
        }

        if (ImGui::MenuItem("Export World Readout..."))
        {
            const std::string selectedPath = SaveNativeFileDialog("Save Current World Readout",
                                                                  {{"Text", {".txt"}}},
                                                                  FileNameFromPath(m_WorldReadoutFilePath, "current_world_readout.txt"));
            if (!selectedPath.empty())
            {
                m_WorldReadoutFilePath = selectedPath;
                if (WriteCurrentWorldReadout(m_WorldReadoutFilePath))
                {
                    m_StatusMessage = "Wrote current world readout: " + m_WorldReadoutFilePath;
                }
                else
                {
                    m_StatusMessage = "Failed to write current world readout.";
                }
            }
            else
            {
                m_StatusMessage = "No readout save location selected.";
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reload Active Terrain"))
        {
            if (GetActiveTerrainDataset() != nullptr)
            {
                StartTerrainBuildJob(m_ActiveTerrainIndex);
            }
        }

        if (ImGui::MenuItem("Reload Active Overlay"))
        {
            LoadActiveOverlayImage();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Workspace"))
    {
        const auto workspaceItem = [&](WorkspaceSection section) {
            const bool selected = m_ActiveWorkspaceSection == section;
            if (ImGui::MenuItem(WorkspaceSectionLabel(section), nullptr, selected))
            {
                m_ActiveWorkspaceSection = section;
                m_ShowWorkspaceWindow = true;
            }
        };

        workspaceItem(WorkspaceSection::World);
        workspaceItem(WorkspaceSection::Terrain);
        workspaceItem(WorkspaceSection::Profiles);
        workspaceItem(WorkspaceSection::Assets);
        workspaceItem(WorkspaceSection::Lighting);
        workspaceItem(WorkspaceSection::Diagnostics);
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
            dataset.overlays.push_back(std::move(overlay));
            m_TerrainDatasets.push_back(std::move(dataset));
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
                dataset->overlays.push_back(std::move(overlay));
                dataset->activeOverlayIndex = static_cast<int>(dataset->overlays.size()) - 1;
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
        ImGui::MenuItem("Workspace", nullptr, &m_ShowWorkspaceWindow);
        ImGui::Separator();
        ImGui::MenuItem("Terrain Minimap", nullptr, &m_ShowMiniMapWindow);
        ImGui::MenuItem("Terrain Datasets", nullptr, &m_ShowTerrainDatasetWindow);
        ImGui::MenuItem("Sun Illumination", nullptr, &m_ShowSunWindow);
        ImGui::MenuItem("Aerial Overlay", nullptr, &m_ShowAerialOverlayWindow);
        ImGui::MenuItem("Blender Assets", nullptr, &m_ShowBlenderAssetsWindow);
        ImGui::MenuItem("Terrain Profiles", nullptr, &m_ShowTerrainProfilesWindow);
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
            if (GetActiveImportedAsset() != nullptr)
            {
                StartImportedAssetLoadJob(m_ActiveImportedAssetIndex);
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}
} // namespace GeoFPS
