#include "Core/Application.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/NativeFileDialog.h"

#include "imgui.h"
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <string>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationUIInternal;

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
        if (ImGui::Button("Delete Selected"))
        {
            DeleteSelectedImportedAssets();
            activeAsset = GetActiveImportedAsset();
        }

        if (ImGui::Button("Select All"))
        {
            SelectAllImportedAssets(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Selection"))
        {
            SelectAllImportedAssets(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Snap Selected To Terrain"))
        {
            SnapSelectedImportedAssetsToTerrain();
        }

        ImGui::DragFloat3("Paste Offset XYZ", glm::value_ptr(m_AssetPasteOffset), 0.25f, -1000.0f, 1000.0f);
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
        ImGui::SameLine();
        if (ImGui::Button("Browse##asset_path"))
        {
            const std::string selectedPath = OpenNativeFileDialog("Load Blender Asset",
                                                                  {{"Blender/3D Assets", {".glb", ".gltf", ".obj"}}});
            if (!selectedPath.empty())
            {
                activeAsset->path = selectedPath;
            }
            else
            {
                m_StatusMessage = "No Blender asset selected.";
            }
        }
        if (!activeAsset->path.empty() && !PathExists(activeAsset->path))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f), "Asset file does not exist.");
        }

        ImGui::TextWrapped("Preferred Blender export is .glb. .gltf and .obj also load, but .glb carries materials most cleanly.");
        if (ImGui::Button("Load Asset"))
        {
            StartImportedAssetLoadJob(m_ActiveImportedAssetIndex);
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
        else if (ImGui::Button("Set Geographic From Local Position"))
        {
            GeoConverter converter(m_GeoReference);
            const glm::dvec3 geographic = converter.ToGeographic(
                {static_cast<double>(activeAsset->position.x),
                 static_cast<double>(activeAsset->position.y),
                 static_cast<double>(activeAsset->position.z)});
            activeAsset->latitude = geographic.x;
            activeAsset->longitude = geographic.y;
            activeAsset->height = geographic.z;
            m_StatusMessage = "Updated asset geographic coordinates from local position.";
        }

        ImGui::DragFloat3("Asset Position", glm::value_ptr(activeAsset->position), 0.1f);
        ImGui::DragFloat("Asset Rotation Z", &activeAsset->rotationDegrees.z, 0.5f);
        ImGui::DragFloat3("Asset Scale", glm::value_ptr(activeAsset->scale), 0.05f, 0.01f, 1000.0f);
        ImGui::ColorEdit3("Asset Tint", glm::value_ptr(activeAsset->tint));
        ImGui::Checkbox("Show World Label", &activeAsset->showLabel);

        ImGui::Text("Asset: %s", activeAsset->loaded ? "loaded" : "not loaded");
        if (activeAsset->loaded)
        {
            size_t vertexCount = 0;
            size_t triangleCount = 0;
            size_t texturedPrimitiveCount = 0;
            size_t pbrPrimitiveCount = 0;
            for (const ImportedPrimitiveData& primitive : activeAsset->assetData.primitives)
            {
                vertexCount += primitive.meshData.vertices.size();
                triangleCount += primitive.meshData.indices.size() / 3u;
                texturedPrimitiveCount += primitive.hasBaseColorTexture ? 1u : 0u;
                pbrPrimitiveCount += (primitive.hasMetallicRoughnessTexture || primitive.hasNormalTexture ||
                                      primitive.hasEmissiveTexture || primitive.metallicFactor > 0.0f ||
                                      glm::length(primitive.emissiveFactor) > 0.0f) ? 1u : 0u;
            }

            ImGui::Text("Primitives: %zu", activeAsset->assetData.primitives.size());
            ImGui::Text("Vertices: %zu", vertexCount);
            ImGui::Text("Triangles: %zu", triangleCount);
            ImGui::Text("Base Color Textures: %zu", texturedPrimitiveCount);
            ImGui::Text("PBR/Advanced Materials: %zu", pbrPrimitiveCount);
        }
    }

    ImGui::End();
}
} // namespace GeoFPS
