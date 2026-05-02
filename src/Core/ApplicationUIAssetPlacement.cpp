#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "Math/GeoConverter.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstring>
#include <string>

namespace GeoFPS
{
using namespace ApplicationInternal;

void Application::RenderAssetPlacementPanel()
{
    static int  s_PlacementMode  = 0;      // 0 = Line, 1 = Grid
    static int  s_CountX         = 3;
    static int  s_CountZ         = 3;
    static float s_SpacingX      = 5.0f;
    static float s_SpacingZ      = 5.0f;
    static float s_StartOffset[3]= {0.0f, 0.0f, 0.0f};
    static bool s_SnapToTerrain  = true;
    static bool s_CopyAnimState  = false;

    ImGui::RadioButton("Line",  &s_PlacementMode, 0); ImGui::SameLine();
    ImGui::RadioButton("Grid",  &s_PlacementMode, 1);

    if (s_PlacementMode == 0)
    {
        ImGui::DragInt  ("Count##place_n",        &s_CountX,   1,  1, 500);
        ImGui::DragFloat("Spacing (m)##place_sx", &s_SpacingX, 0.1f, 0.01f, 10000.0f, "%.2f");
    }
    else
    {
        ImGui::DragInt2 ("Columns / Rows##place_grid",  &s_CountX,   1, 1, 500);
        ImGui::DragFloat2("Spacing X / Z (m)##place_s", &s_SpacingX, 0.1f, 0.01f, 10000.0f, "%.2f");
    }

    ImGui::DragFloat3("Start Offset XYZ##place_off", s_StartOffset, 0.1f, -100000.0f, 100000.0f, "%.1f");
    ImGui::Checkbox("Snap to terrain Y##place_snap", &s_SnapToTerrain);
    ImGui::SameLine();
    ImGui::Checkbox("Copy anim state##place_anim",   &s_CopyAnimState);

    const int totalCount = (s_PlacementMode == 0) ? s_CountX : (s_CountX * s_CountZ);
    ImGui::TextDisabled("Will create %d new asset(s).", totalCount);

    if (ImGui::Button("Place##place_btn", ImVec2(100.0f, 28.0f)))
    {
        const ImportedAsset* source = GetActiveImportedAsset();
        if (source == nullptr || source->path.empty())
        {
            m_StatusMessage = "Select an asset first.";
            return;
        }

        // Deselect all existing
        for (ImportedAsset& a : m_ImportedAssets) a.selected = false;

        const glm::vec3 origin = source->position + glm::vec3(s_StartOffset[0], s_StartOffset[1], s_StartOffset[2]);

        // Build position list
        std::vector<glm::vec3> positions;
        positions.reserve(static_cast<size_t>(totalCount));

        if (s_PlacementMode == 0)
        {
            // Line along +X
            for (int i = 0; i < s_CountX; ++i)
                positions.push_back(origin + glm::vec3(static_cast<float>(i) * s_SpacingX, 0.0f, 0.0f));
        }
        else
        {
            // Grid on XZ plane, centred on origin
            const float halfX = (static_cast<float>(s_CountX - 1) * s_SpacingX) * 0.5f;
            const float halfZ = (static_cast<float>(s_CountZ - 1) * s_SpacingZ) * 0.5f;
            for (int row = 0; row < s_CountZ; ++row)
                for (int col = 0; col < s_CountX; ++col)
                    positions.push_back(origin + glm::vec3(
                        static_cast<float>(col) * s_SpacingX - halfX,
                        0.0f,
                        static_cast<float>(row) * s_SpacingZ - halfZ));
        }

        // Create assets
        const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
        GeoConverter converter(m_GeoReference);

        for (const glm::vec3& pos : positions)
        {
            ImportedAsset asset;
            asset.name                  = source->name;
            asset.path                  = source->path;
            asset.scale                 = source->scale;
            asset.rotationDegrees       = source->rotationDegrees;
            asset.tint                  = source->tint;
            asset.showLabel             = source->showLabel;
            asset.useGeographicPlacement = false;
            asset.position              = pos;
            asset.selected              = true;

            // Snap Y to terrain surface
            if (s_SnapToTerrain && activeTerrain != nullptr)
            {
                const float terrainY = GetTerrainLocalHeightAt(pos.x, pos.z);
                asset.position.y = terrainY;
            }

            if (s_CopyAnimState)
            {
                asset.animState     = source->animState;
                asset.nodeAnimState = source->nodeAnimState;
            }

            if (!asset.path.empty())
                LoadImportedAsset(asset);

            m_ImportedAssets.push_back(std::move(asset));
        }

        m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
        m_StatusMessage = "Placed " + std::to_string(positions.size()) + " asset(s).";
    }
}

} // namespace GeoFPS
