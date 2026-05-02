#include "Core/Application.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/NativeFileDialog.h"

#include "imgui.h"
#include <glm/geometric.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationUIInternal;

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: tinted section label
// ─────────────────────────────────────────────────────────────────────────────
static void SectionLabel(const char* label)
{
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "%s", label);
}

void Application::RenderBlenderAssetsWindow()
{
    if (!m_ShowBlenderAssetsWindow)
        return;

    ImportedAsset* activeAsset = GetActiveImportedAsset();
    const size_t selectedAssetCount = GetSelectedImportedAssetCount();
    static glm::vec3 bulkLocalPosition(0.0f);
    static glm::vec3 bulkScale(1.0f);
    static float bulkRotationZ = 0.0f;
    static double bulkLatitude = 0.0;
    static double bulkLongitude = 0.0;
    static double bulkHeight = 0.0;

    ImGui::SetNextWindowSize(ImVec2(400.0f, 700.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Blender Assets", &m_ShowBlenderAssetsWindow))
    {
        ImGui::End();
        return;
    }

    // ── Label settings ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("World Labels"))
    {
        ImGui::Checkbox("Show Asset Labels", &m_AssetLabelSettings.visible);
        if (m_AssetLabelSettings.visible)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##label_dist", &m_AssetLabelSettings.maxDistanceMeters,
                             10.0f, 100.0f, 50000.0f, "Max distance: %.0f m");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::DragFloat("##label_voff", &m_AssetLabelSettings.verticalOffsetMeters,
                             0.1f, 0.0f, 100.0f, "Height offset: %.1f m");
        }
    }

    // ── Asset list ────────────────────────────────────────────────────────────
    {
        const int loadedCount = static_cast<int>(
            std::count_if(m_ImportedAssets.begin(), m_ImportedAssets.end(),
                          [](const ImportedAsset& a){ return a.loaded; }));
        char listHeader[64];
        std::snprintf(listHeader, sizeof(listHeader),
                      "Assets   %d / %d loaded###AssetList",
                      loadedCount, static_cast<int>(m_ImportedAssets.size()));

        if (ImGui::CollapsingHeader(listHeader, ImGuiTreeNodeFlags_DefaultOpen))
        {
            // ── Toolbar ───────────────────────────────────────────────────────
            if (ImGui::Button("+ Add"))
            {
                ImportedAsset asset;
                asset.name = "Asset " + std::to_string(m_ImportedAssets.size() + 1);
                m_ImportedAssets.push_back(std::move(asset));
                m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
                activeAsset = GetActiveImportedAsset();
                m_StatusMessage = "Added asset slot.";
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_ImportedAssets.empty());
            if (ImGui::Button("Delete"))
            {
                DeleteImportedAsset(m_ActiveImportedAssetIndex);
                activeAsset = GetActiveImportedAsset();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            if (ImGui::Button("All"))  { SelectAllImportedAssets(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Select all");
            ImGui::SameLine();
            if (ImGui::Button("None")) { SelectAllImportedAssets(false); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deselect all");
            ImGui::SameLine();
            ImGui::BeginDisabled(selectedAssetCount == 0);
            if (ImGui::Button("Del Sel"))
            {
                DeleteSelectedImportedAssets();
                activeAsset = GetActiveImportedAsset();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Delete selected (%zu)", selectedAssetCount);
            ImGui::EndDisabled();

            ImGui::Spacing();

            // ── Table list ────────────────────────────────────────────────────
            constexpr float kRowH    = 24.0f;
            constexpr float kMaxRows = 8.0f;
            const float listHeight = std::min(
                static_cast<float>(m_ImportedAssets.size()) * kRowH + 6.0f,
                kMaxRows * kRowH + 6.0f);

            constexpr ImGuiTableFlags kTableFlags =
                ImGuiTableFlags_BordersInnerH  |
                ImGuiTableFlags_RowBg          |
                ImGuiTableFlags_ScrollY        |
                ImGuiTableFlags_SizingStretchProp;

            if (m_ImportedAssets.empty())
            {
                ImGui::TextDisabled("  No assets — press '+ Add' to create one.");
            }
            else if (ImGui::BeginTable("##asset_tbl", 4, kTableFlags, ImVec2(-1.0f, listHeight)))
            {
                ImGui::TableSetupScrollFreeze(0, 0);
                ImGui::TableSetupColumn("##chk",  ImGuiTableColumnFlags_WidthFixed,   20.0f);
                ImGui::TableSetupColumn("##dot",  ImGuiTableColumnFlags_WidthFixed,   14.0f);
                ImGui::TableSetupColumn("##nm",   ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("##ext",  ImGuiTableColumnFlags_WidthFixed,   38.0f);

                for (int i = 0; i < static_cast<int>(m_ImportedAssets.size()); ++i)
                {
                    ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(i)];
                    const bool isActive = (i == m_ActiveImportedAssetIndex);
                    ImGui::PushID(i);
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, kRowH);

                    // Tint active row
                    if (isActive)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                            ImGui::GetColorU32(ImVec4(0.22f, 0.48f, 0.28f, 0.30f)));

                    // Col 0 — checkbox
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
                    ImGui::Checkbox("##s", &asset.selected);

                    // Col 1 — status dot
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                    if (asset.loaded)
                        ImGui::TextColored(ImVec4(0.38f, 0.85f, 0.52f, 1.0f), "\xe2\x97\x8f");
                    else
                        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.45f, 1.0f), "\xe2\x97\x8b");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(asset.loaded ? "Loaded" : "Not loaded");

                    // Col 2 — name selectable (spans to end of row)
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::Selectable(asset.name.c_str(), isActive,
                                          ImGuiSelectableFlags_SpanAllColumns,
                                          ImVec2(0.0f, kRowH - 6.0f)))
                    {
                        m_ActiveImportedAssetIndex = i;
                        activeAsset = GetActiveImportedAsset();
                    }

                    // Col 3 — file extension badge
                    ImGui::TableSetColumnIndex(3);
                    if (!asset.path.empty())
                    {
                        const size_t dot = asset.path.rfind('.');
                        if (dot != std::string::npos)
                        {
                            // Abbreviate: ".gltf"→"gltf", ".glb"→"glb", etc.
                            const std::string ext = asset.path.substr(dot + 1u);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
                            ImGui::TextDisabled("%s", ext.c_str());
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    // ── Bulk operations (shown when ≥1 selected) ──────────────────────────────
    if (selectedAssetCount > 0)
    {
        if (ImGui::CollapsingHeader(
                (std::string("Bulk Operations (") + std::to_string(selectedAssetCount) + " selected)").c_str()))
        {
            SectionLabel("Position");
            ImGui::DragFloat3("##bulk_local", glm::value_ptr(bulkLocalPosition), 0.1f, 0.0f, 0.0f, "Local XYZ %.1f");
            if (ImGui::Button("Apply Local Position"))
            {
                for (ImportedAsset& a : m_ImportedAssets)
                {
                    if (!a.selected) continue;
                    a.useGeographicPlacement = false;
                    a.position = bulkLocalPosition;
                }
                m_StatusMessage = "Applied local position to " + std::to_string(selectedAssetCount) + " asset(s).";
            }

            ImGui::Spacing();
            ImGui::InputDouble("##bulk_lat",  &bulkLatitude,  0.0, 0.0, "Lat %.8f");
            ImGui::InputDouble("##bulk_lon",  &bulkLongitude, 0.0, 0.0, "Lon %.8f");
            ImGui::InputDouble("##bulk_hgt",  &bulkHeight,    0.0, 0.0, "Alt %.3f m");
            if (ImGui::Button("Apply Geographic Position"))
            {
                for (ImportedAsset& a : m_ImportedAssets)
                {
                    if (!a.selected) continue;
                    a.useGeographicPlacement = true;
                    a.latitude  = bulkLatitude;
                    a.longitude = bulkLongitude;
                    a.height    = bulkHeight;
                    UpdateImportedAssetPositionFromGeographic(a);
                }
                m_StatusMessage = "Applied geographic position to " + std::to_string(selectedAssetCount) + " asset(s).";
            }

            ImGui::Spacing();
            SectionLabel("Transform");
            ImGui::DragFloat3("##bulk_scale", glm::value_ptr(bulkScale), 0.05f, 0.01f, 1000.0f, "Scale %.2f");
            if (ImGui::Button("Apply Scale"))
            {
                for (ImportedAsset& a : m_ImportedAssets)
                {
                    if (!a.selected) continue;
                    a.scale = bulkScale;
                }
                m_StatusMessage = "Applied scale to " + std::to_string(selectedAssetCount) + " asset(s).";
            }
            ImGui::DragFloat("##bulk_rz", &bulkRotationZ, 0.5f, 0.0f, 0.0f, "Rot Z %.1f°");
            if (ImGui::Button("Apply Rotation Z"))
            {
                for (ImportedAsset& a : m_ImportedAssets)
                {
                    if (!a.selected) continue;
                    a.rotationDegrees.z = bulkRotationZ;
                }
                m_StatusMessage = "Applied Z rotation to " + std::to_string(selectedAssetCount) + " asset(s).";
            }

            ImGui::Spacing();
            SectionLabel("Snap / Copy / Paste");
            if (ImGui::Button("Snap to Terrain"))      { SnapSelectedImportedAssetsToTerrain(); }
            ImGui::SameLine();
            if (ImGui::Button("Copy Selected"))         { CopySelectedImportedAssets(); }
            ImGui::SameLine();
            if (ImGui::Button("Paste"))                 { PasteCopiedImportedAssets(); activeAsset = GetActiveImportedAsset(); }
            ImGui::DragFloat3("Paste Offset##bulk_paste", glm::value_ptr(m_AssetPasteOffset),
                              0.25f, -1000.0f, 1000.0f);
        }
    }

    // ── Active asset details ──────────────────────────────────────────────────
    if (activeAsset != nullptr)
    {
        ImGui::Separator();

        // Status badge line
        {
            const bool isLoaded = activeAsset->loaded;
            ImGui::TextColored(isLoaded ? ImVec4(0.40f, 0.88f, 0.55f, 1.0f) : ImVec4(0.80f, 0.50f, 0.28f, 1.0f),
                               isLoaded ? "  Loaded" : "  Not Loaded");
            if (isLoaded)
            {
                size_t verts = 0, tris = 0;
                for (const auto& p : activeAsset->assetData.primitives)
                {
                    verts += p.meshData.vertices.size();
                    tris  += p.meshData.indices.size() / 3u;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| %zu prims  %zu verts  %zu tris",
                                    activeAsset->assetData.primitives.size(), verts, tris);
            }
        }

        // ── Name & path ───────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Identity & Path", ImGuiTreeNodeFlags_DefaultOpen))
        {
            char assetNameBuffer[256];
            std::snprintf(assetNameBuffer, sizeof(assetNameBuffer), "%s", activeAsset->name.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##asset_name", assetNameBuffer, sizeof(assetNameBuffer)))
                activeAsset->name = assetNameBuffer;
            ImGui::TextDisabled("Name");

            ImGui::Spacing();

            char assetPathBuffer[512];
            std::snprintf(assetPathBuffer, sizeof(assetPathBuffer), "%s", activeAsset->path.c_str());
            ImGui::SetNextItemWidth(-60.0f);
            if (ImGui::InputText("##asset_path", assetPathBuffer, sizeof(assetPathBuffer)))
                activeAsset->path = assetPathBuffer;
            ImGui::SameLine();
            if (ImGui::Button("Browse"))
            {
                const std::string selectedPath = OpenNativeFileDialog(
                    "Load Blender Asset", {{"Blender / 3D Assets", {".glb", ".gltf", ".obj"}}});
                if (!selectedPath.empty())
                    activeAsset->path = selectedPath;
                else
                    m_StatusMessage = "No Blender asset selected.";
            }
            if (!activeAsset->path.empty() && !PathExists(activeAsset->path))
                ImGui::TextColored(ImVec4(1.0f, 0.38f, 0.25f, 1.0f), "  File not found");

            ImGui::TextDisabled("Preferred: .glb (materials + skeleton in one file)");

            ImGui::Spacing();
            if (ImGui::Button("Load Asset", ImVec2(-1.0f, 0.0f)))
                StartImportedAssetLoadJob(m_ActiveImportedAssetIndex);
        }

        // ── Navigation ────────────────────────────────────────────────────────
        if (activeAsset->loaded)
        {
            if (ImGui::Button("Go to Asset", ImVec2(-1.0f, 0.0f)))
                GoToActiveAsset();
        }

        // ── Placement ─────────────────────────────────────────────────────────
        if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Use Geographic Placement", &activeAsset->useGeographicPlacement);

            if (activeAsset->useGeographicPlacement)
            {
                ImGui::InputDouble("Latitude##assetlat",  &activeAsset->latitude,  0.0, 0.0, "%.8f");
                ImGui::InputDouble("Longitude##assetlon", &activeAsset->longitude, 0.0, 0.0, "%.8f");
                ImGui::InputDouble("Altitude (m)##assetalt", &activeAsset->height,  0.0, 0.0, "%.3f");
                if (ImGui::Button("Apply Geographic Position"))
                    UpdateImportedAssetPositionFromGeographic(*activeAsset);
            }
            else
            {
                if (ImGui::Button("Set Geo from Local"))
                {
                    GeoConverter converter(m_GeoReference);
                    const glm::dvec3 geo = converter.ToGeographic(
                        {static_cast<double>(activeAsset->position.x),
                         static_cast<double>(activeAsset->position.y),
                         static_cast<double>(activeAsset->position.z)});
                    activeAsset->latitude  = geo.x;
                    activeAsset->longitude = geo.y;
                    activeAsset->height    = geo.z;
                    m_StatusMessage = "Updated asset geographic coordinates.";
                }
            }

            ImGui::Spacing();
            ImGui::DragFloat3("Position##assetpos",  glm::value_ptr(activeAsset->position),        0.1f);
            ImGui::DragFloat ("Rotation Z##assetrz", &activeAsset->rotationDegrees.z,              0.5f);
            ImGui::DragFloat3("Scale##assetscale",   glm::value_ptr(activeAsset->scale),           0.05f, 0.01f, 1000.0f);
            ImGui::ColorEdit3("Tint##assettint",     glm::value_ptr(activeAsset->tint));
            ImGui::Checkbox("Show World Label##assetlbl", &activeAsset->showLabel);
        }

        // ── Skeletal animation ────────────────────────────────────────────────
        if (activeAsset->loaded && activeAsset->assetData.hasSkin)
        {
            if (ImGui::CollapsingHeader("Skeletal Animation"))
            {
                AnimationState& animState   = activeAsset->animState;
                const auto& animations       = activeAsset->assetData.animations;

                const char* clipLabel = (animState.activeClipIndex >= 0 &&
                                         animState.activeClipIndex < static_cast<int>(animations.size()))
                    ? animations[static_cast<size_t>(animState.activeClipIndex)].name.c_str()
                    : "(bind pose)";

                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("##anim_clip", clipLabel))
                {
                    if (ImGui::Selectable("(bind pose)", animState.activeClipIndex < 0))
                    {
                        animState.activeClipIndex = -1;
                        animState.currentTime     = 0.0f;
                        animState.isPlaying       = false;
                    }
                    for (int ci = 0; ci < static_cast<int>(animations.size()); ++ci)
                    {
                        const bool selected = (ci == animState.activeClipIndex);
                        if (ImGui::Selectable(animations[static_cast<size_t>(ci)].name.c_str(), selected))
                        {
                            animState.activeClipIndex = ci;
                            animState.currentTime     = 0.0f;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Clip");

                if (animState.activeClipIndex >= 0 &&
                    animState.activeClipIndex < static_cast<int>(animations.size()))
                {
                    const float dur = animations[static_cast<size_t>(animState.activeClipIndex)].duration;

                    ImGui::Checkbox("Play##anim",  &animState.isPlaying);
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop##anim",  &animState.loop);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::DragFloat("##anim_speed", &animState.playbackSpeed,
                                     0.01f, 0.0f, 5.0f, "Speed %.2f×");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::SliderFloat("##anim_time", &animState.currentTime,
                                       0.0f, std::max(dur, 0.001f), "Time %.2f s");
                    ImGui::TextDisabled("Duration: %.2f s  |  Joints: %d",
                                        dur, static_cast<int>(activeAsset->assetData.skeleton.joints.size()));
                }
            }
        }

        // ── Node-transform animation ──────────────────────────────────────────
        if (activeAsset->loaded && activeAsset->assetData.hasNodeAnimation)
        {
            if (ImGui::CollapsingHeader("Node Animation"))
            {
                NodeAnimationState& ns = activeAsset->nodeAnimState;

                for (const NodeAnimationClip& clip : activeAsset->assetData.nodeAnimations)
                    ImGui::BulletText("%s  (%.2f s)", clip.name.c_str(), clip.duration);

                ImGui::Checkbox("Play##nodeAnim",  &ns.isPlaying);
                ImGui::SameLine();
                ImGui::Checkbox("Loop##nodeAnim",  &ns.loop);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::DragFloat("##nanim_speed", &ns.playbackSpeed,
                                 0.01f, 0.0f, 5.0f, "Speed %.2f×");

                float maxDur = 0.0f;
                for (const NodeAnimationClip& clip : activeAsset->assetData.nodeAnimations)
                    maxDur = std::max(maxDur, clip.duration);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##nanim_time", &ns.currentTime,
                                   0.0f, std::max(maxDur, 0.001f), "Time %.2f s");
                ImGui::TextDisabled("Duration: %.2f s  |  Clips: %zu",
                                    maxDur, activeAsset->assetData.nodeAnimations.size());
            }
        }
    }

    // ── Placement tools ───────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Grid / Line Placement"))
        RenderAssetPlacementPanel();

    ImGui::End();
}
} // namespace GeoFPS
