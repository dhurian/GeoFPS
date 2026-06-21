#include "Core/Application.h"
#include "Core/ApplicationInternal.h"
#include "Core/ApplicationUIHelpers.h"
#include "Core/ImGuiWindowControls.h"
#include "Core/NativeFileDialog.h"
#include "Core/TerrainCoordinateHelpers.h"

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
} // namespace

void Application::RenderTerrainProfilesWindow()
{
    if (!m_ShowTerrainProfilesWindow)
    {
        return;
    }

    if (m_Profiles.profiles().empty())
    {
        TerrainProfile profile;
        profile.name = "Profile 1";
        m_Profiles.profiles().push_back(profile);
        m_Profiles.setActiveIndex(0);
    }

    m_Profiles.setActiveIndex(std::clamp(m_Profiles.activeIndex(), 0, static_cast<int>(m_Profiles.profiles().size()) - 1));

    ImGui::SetNextWindowSize(ImVec2(1180.0f, 760.0f), ImGuiCond_FirstUseEver);
    const bool profilesWindowOpen = ImGui::Begin("Terrain Profiles", &m_ShowTerrainProfilesWindow);
    DrawWindowArrangeMenu("Terrain Profiles");
    if (!profilesWindowOpen)
    {
        ImGui::End();
        return;
    }

    TerrainProfile& activeProfile = m_Profiles.profiles()[static_cast<size_t>(m_Profiles.activeIndex())];
    EnsureTerrainProfileHasTerrainSelection(activeProfile);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float maxDetailWidth = std::max(280.0f, availableWidth * 0.62f);
    const float detailWidth = availableWidth > 760.0f ? std::clamp(m_ProfileView.profileDetailsWidth, 280.0f, maxDetailWidth) : 0.0f;
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
            m_ProfileView.profileDetailsWidth = std::clamp(m_ProfileView.profileDetailsWidth - ImGui::GetIO().MouseDelta.x, 280.0f, maxDetailWidth);
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

    if (m_ProfileView.profileLineVao == 0)
    {
        glGenVertexArrays(1, &m_ProfileView.profileLineVao);
        glGenBuffers(1, &m_ProfileView.profileLineVbo);
        glBindVertexArray(m_ProfileView.profileLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_ProfileView.profileLineVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    }
    else
    {
        glBindVertexArray(m_ProfileView.profileLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_ProfileView.profileLineVbo);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);

    std::vector<glm::vec3> lineVertices;
    std::vector<glm::vec3> ribbonVertices;
    for (const TerrainProfile& profile : m_Profiles.profiles())
    {
        if (!profile.visible || !profile.showInWorld || profile.vertices.size() < 2)
        {
            continue;
        }

        // Collect every loaded, bounds-valid dataset this profile is
        // included in.  Each sample below is then draped over whichever of
        // these datasets contains its lat/lon.  Previously we ran this
        // entire body once per dataset, which produced visibly duplicate
        // ribbons in the world when a profile included two terrains
        // (each iteration drew the *full* path against its own dataset's
        // height grid, with slightly different sampled heights).  Drawing
        // once per profile and picking the containing dataset per-sample
        // gives a single, contiguous line that drapes correctly across
        // every included terrain it crosses.
        std::vector<const TerrainDataset*> coveringDatasets;
        coveringDatasets.reserve(m_Terrain.datasets().size());
        for (const TerrainDataset& dataset : m_Terrain.datasets())
        {
            if (!TerrainDatasetHasCoverage(dataset) || !TerrainProfileIncludesTerrain(profile, dataset))
            {
                continue;
            }
            if (!dataset.bounds.valid)
            {
                continue;
            }
            coveringDatasets.push_back(&dataset);
        }
        if (coveringDatasets.empty())
        {
            continue;
        }

        // Build the sample list once for the profile.  RebuildTerrainProfileSamples
        // already populated profile.samples against the primary terrain at
        // profile.sampleSpacingMeters spacing — sufficient for a single ribbon.
        const std::vector<TerrainProfileSample>& samples = profile.samples;
        if (samples.size() < 2)
        {
            continue;
        }

        lineVertices.clear();
        lineVertices.reserve(samples.size());
        std::vector<bool> validLineVertices;
        validLineVertices.reserve(samples.size());

        for (const TerrainProfileSample& sample : samples)
        {
            // Pick the dataset whose bounds contain this sample's lat/lon —
            // that's the one whose height grid and world translation we
            // want to use here.  First-match wins on overlaps; if no
            // dataset contains the sample we fall back to the first
            // covering dataset so the ribbon still has a position
            // (sample.valid is already false in that case so the segment
            // will be skipped below anyway).
            const TerrainDataset* owner = nullptr;
            for (const TerrainDataset* candidate : coveringDatasets)
            {
                if (TerrainDatasetContainsCoordinate(*candidate, sample.latitude, sample.longitude))
                {
                    owner = candidate;
                    break;
                }
            }
            if (owner == nullptr)
            {
                owner = coveringDatasets.front();
            }

            const glm::dvec3 local =
                TerrainCoordinateToLocal(*owner, sample.latitude, sample.longitude, owner->geoReference.originHeight);
            const float localHeight = sample.valid ?
                                          SampleRenderedTerrainLocalHeightAt(*owner, sample.latitude, sample.longitude) +
                                              profile.worldGroundOffsetMeters :
                                          0.0f;
            const glm::dvec3 terrainTranslation = GetDatasetWorldTranslation(*owner);
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

    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Application::RenderTerrainProfileDetails(TerrainProfile& activeProfile)
{
    ImGui::SliderFloat("Options Width", &m_ProfileView.profileDetailsWidth, 280.0f, 720.0f, "%.0f px");

    if (ImGui::CollapsingHeader("Profiles", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Profiles");
    for (int index = 0; index < static_cast<int>(m_Profiles.profiles().size()); ++index)
    {
        TerrainProfile& profile = m_Profiles.profiles()[static_cast<size_t>(index)];
        ImGui::PushID(index);
        ImGui::Checkbox("##profile_visible", &profile.visible);
        ImGui::SameLine();
        const bool selected = index == m_Profiles.activeIndex();
        if (ImGui::Selectable(profile.name.c_str(), selected))
        {
            m_Profiles.setActiveIndex(index);
            m_ProfileView.selectedProfileVertexIndex = -1;
            m_ProfileView.selectedProfileSampleIndex = -1;
            m_ProfileView.hoveredProfileSampleIndex = -1;
        }
        ImGui::PopID();
    }

        if (ImGui::Button("New Profile"))
        {
            TerrainProfile profile;
            profile.name = "Profile " + std::to_string(m_Profiles.profiles().size() + 1);
            m_Profiles.profiles().push_back(std::move(profile));
            m_Profiles.setActiveIndex(static_cast<int>(m_Profiles.profiles().size()) - 1);
            m_ProfileView.selectedProfileVertexIndex = -1;
            m_ProfileView.selectedProfileSampleIndex = -1;
            m_ProfileView.hoveredProfileSampleIndex = -1;
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Profile") && m_Profiles.profiles().size() > 1)
        {
            m_Profiles.profiles().erase(m_Profiles.profiles().begin() + m_Profiles.activeIndex());
            m_Profiles.setActiveIndex(std::clamp(m_Profiles.activeIndex(), 0, static_cast<int>(m_Profiles.profiles().size()) - 1));
            m_ProfileView.selectedProfileVertexIndex = -1;
            m_ProfileView.selectedProfileSampleIndex = -1;
            m_ProfileView.hoveredProfileSampleIndex = -1;
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
        ImGui::Checkbox("Show Height Labels", &m_ProfileView.showProfileHeightLabels);
        ImGui::Checkbox("Show Sample Points", &m_ProfileView.showProfileSamples);
    }

    if (ImGui::CollapsingHeader("Terrains And World", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Use Profile On Terrains");
        for (TerrainDataset& dataset : m_Terrain.datasets())
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
            for (const TerrainDataset& dataset : m_Terrain.datasets())
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
            for (TerrainProfile& profile : m_Profiles.profiles())
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
        if (ImGui::BeginCombo("Map Size", ProfileMapSizeModeLabel(m_ProfileView.profileMapSizeMode)))
        {
            const ProfileMapSizeMode modes[] = {ProfileMapSizeMode::Small, ProfileMapSizeMode::Medium, ProfileMapSizeMode::Large, ProfileMapSizeMode::Fill};
            for (ProfileMapSizeMode mode : modes)
            {
                const bool selected = mode == m_ProfileView.profileMapSizeMode;
                if (ImGui::Selectable(ProfileMapSizeModeLabel(mode), selected))
                {
                    m_ProfileView.profileMapSizeMode = mode;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("Show Isolines", &m_Isolines.settings().enabled))
        {
            m_Isolines.MarkSegmentsDirty();
        }
        if (ImGui::Checkbox("Use GPU Isolines", &m_Isolines.useGpuGeneration()))
        {
            m_Isolines.MarkSegmentsDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Rebuild Isolines"))
        {
            m_Isolines.MarkSegmentsDirty();
            RefreshIsolinesIfNeeded();
        }
        ImGui::Text("Isoline Status: %s  Sample Grid: %s  Backend: %s",
                    m_Isolines.segmentsDirty() ? "stale" : "current",
                    m_Isolines.sampleGridDirty() ? "stale" : "cached",
                    m_Isolines.usedGpu() ? "Metal GPU" : "CPU");
        if (ImGui::BeginCombo("Isoline Resolution", m_Isolines.settings().resolutionX == 32 ? "Fast 32 x 32" :
                                                        m_Isolines.settings().resolutionX == 128 ? "Medium 128 x 128" :
                                                        m_Isolines.settings().resolutionX == 256 ? "High 256 x 256" : "Low 64 x 64"))
        {
            struct ResolutionOption
            {
                const char* label;
                int value;
            };
            const ResolutionOption options[] = {{"Fast 32 x 32", 32}, {"Low 64 x 64", 64}, {"Medium 128 x 128", 128}, {"High 256 x 256", 256}};
            for (const ResolutionOption& option : options)
            {
                const bool selected = m_Isolines.settings().resolutionX == option.value;
                if (ImGui::Selectable(option.label, selected))
                {
                    m_Isolines.settings().resolutionX = option.value;
                    m_Isolines.settings().resolutionZ = option.value;
                    m_Isolines.MarkSampleGridDirty();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Checkbox("Auto Interval", &m_Isolines.settings().autoInterval))
        {
            m_Isolines.MarkSegmentsDirty();
        }
        if (!m_Isolines.settings().autoInterval && ImGui::InputDouble("Interval (m)", &m_Isolines.settings().contourIntervalMeters, 1.0, 10.0, "%.2f"))
        {
            m_Isolines.settings().contourIntervalMeters = std::max(m_Isolines.settings().contourIntervalMeters, 0.1);
            m_Isolines.MarkSegmentsDirty();
        }
        if (ImGui::SliderFloat("Isoline Thickness", &m_Isolines.settings().thickness, 0.5f, 5.0f, "%.1f px"))
        {
            m_Isolines.settings().thickness = std::max(m_Isolines.settings().thickness, 0.1f);
        }
        if (ImGui::SliderFloat("Isoline Opacity", &m_Isolines.settings().opacity, 0.1f, 1.0f, "%.2f"))
        {
            m_Isolines.MarkSegmentsDirty();
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
        const char* currentScaleLabel = ProfileScaleModeLabel(m_ProfileView.profileScaleMode);
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
                const bool selected = mode == m_ProfileView.profileScaleMode;
                if (ImGui::Selectable(ProfileScaleModeLabel(mode), selected))
                {
                    m_ProfileView.profileScaleMode = mode;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (m_ProfileView.profileScaleMode == ProfileElevationScaleMode::Fixed)
        {
            ImGui::InputFloat("Fixed Min Height", &m_ProfileView.profileFixedMinHeight, 1.0f, 10.0f, "%.2f");
            ImGui::InputFloat("Fixed Max Height", &m_ProfileView.profileFixedMaxHeight, 1.0f, 10.0f, "%.2f");
            if (m_ProfileView.profileFixedMaxHeight <= m_ProfileView.profileFixedMinHeight)
            {
                m_ProfileView.profileFixedMaxHeight = m_ProfileView.profileFixedMinHeight + 1.0f;
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

    const bool hasSelection = (m_ProfileView.selectedProfileVertexIndex >= 0 && m_ProfileView.selectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size())) ||
                              (m_ProfileView.selectedProfileSampleIndex >= 0 && m_ProfileView.selectedProfileSampleIndex < static_cast<int>(activeProfile.samples.size()));
    if (ImGui::CollapsingHeader("Selection Details", hasSelection ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::Text("Vertices: %zu  Samples: %zu", activeProfile.vertices.size(), activeProfile.samples.size());
        if (m_ProfileView.selectedProfileVertexIndex >= 0 && m_ProfileView.selectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()))
        {
            const TerrainProfileVertex& vertex = activeProfile.vertices[static_cast<size_t>(m_ProfileView.selectedProfileVertexIndex)];
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
            ImGui::Text("Selected vertex: %d", m_ProfileView.selectedProfileVertexIndex + 1);
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
                TerrainProfileVertex& editableVertex = activeProfile.vertices[static_cast<size_t>(m_ProfileView.selectedProfileVertexIndex)];
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
                TerrainProfileVertex& editableVertex = activeProfile.vertices[static_cast<size_t>(m_ProfileView.selectedProfileVertexIndex)];
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
                activeProfile.vertices.erase(activeProfile.vertices.begin() + m_ProfileView.selectedProfileVertexIndex);
                m_ProfileView.selectedProfileVertexIndex = -1;
                RebuildTerrainProfileSamples(activeProfile);
                m_ProfileView.hoveredProfileSampleIndex = -1;
            }
        }
        if (m_ProfileView.selectedProfileSampleIndex >= 0 && m_ProfileView.selectedProfileSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            const TerrainProfileSample& sample = activeProfile.samples[static_cast<size_t>(m_ProfileView.selectedProfileSampleIndex)];
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

    if (ImGui::Button(m_ProfileView.profileDrawMode ? "Draw Profile On" : "Draw Profile"))
    {
        m_ProfileView.profileDrawMode = !m_ProfileView.profileDrawMode;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ProfileView.profileAuxiliaryDrawMode ? "A Vertices On" : "A Vertices"))
    {
        m_ProfileView.profileAuxiliaryDrawMode = !m_ProfileView.profileAuxiliaryDrawMode;
        if (m_ProfileView.profileAuxiliaryDrawMode)
        {
            m_ProfileView.profileDrawMode = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_ProfileView.profileEditMode ? "Edit Vertices On" : "Edit Vertices"))
    {
        m_ProfileView.profileEditMode = !m_ProfileView.profileEditMode;
    }
    ImGui::SameLine();
    if (ImGui::Button("New Profile"))
    {
        TerrainProfile profile;
        profile.name = "Profile " + std::to_string(m_Profiles.profiles().size() + 1);
        m_Profiles.profiles().push_back(std::move(profile));
        m_Profiles.setActiveIndex(static_cast<int>(m_Profiles.profiles().size()) - 1);
        m_ProfileView.selectedProfileVertexIndex = -1;
        m_ProfileView.selectedProfileSampleIndex = -1;
        m_ProfileView.hoveredProfileSampleIndex = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Path"))
    {
        activeProfile.vertices.clear();
        activeProfile.samples.clear();
        m_ProfileView.selectedProfileVertexIndex = -1;
        m_ProfileView.selectedProfileSampleIndex = -1;
        m_ProfileView.hoveredProfileSampleIndex = -1;
    }
    ImGui::SameLine();
    if (m_ProfileView.selectedProfileVertexIndex >= 0 && m_ProfileView.selectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()) &&
        ImGui::Button("Delete Vertex"))
    {
        activeProfile.vertices.erase(activeProfile.vertices.begin() + m_ProfileView.selectedProfileVertexIndex);
        m_ProfileView.selectedProfileVertexIndex = -1;
        m_ProfileView.hoveredProfileSampleIndex = -1;
        RebuildTerrainProfileSamples(activeProfile);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Aerial Map", &m_ProfileView.showProfileAerialImage);
    ImGui::SameLine();
    ImGui::Checkbox("Samples", &m_ProfileView.showProfileSamples);
    ImGui::SameLine();
    if (ImGui::Checkbox("Isolines", &m_Isolines.settings().enabled))
    {
        m_Isolines.MarkSegmentsDirty();
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
    for (const TerrainDataset& dataset : m_Terrain.datasets())
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

    if (!m_ProfileView.profileMapViewInitialized)
    {
        m_ProfileView.profileMapMinLatitude = bounds.minLatitude;
        m_ProfileView.profileMapMaxLatitude = bounds.maxLatitude;
        m_ProfileView.profileMapMinLongitude = bounds.minLongitude;
        m_ProfileView.profileMapMaxLongitude = bounds.maxLongitude;
        m_ProfileView.profileMapViewInitialized = true;
    }

    const auto refitMapView = [&]() {
        m_ProfileView.profileMapMinLatitude = bounds.minLatitude;
        m_ProfileView.profileMapMaxLatitude = bounds.maxLatitude;
        m_ProfileView.profileMapMinLongitude = bounds.minLongitude;
        m_ProfileView.profileMapMaxLongitude = bounds.maxLongitude;
    };

    m_ProfileView.profileMapMinLatitude = std::clamp(m_ProfileView.profileMapMinLatitude, bounds.minLatitude, bounds.maxLatitude);
    m_ProfileView.profileMapMaxLatitude = std::clamp(m_ProfileView.profileMapMaxLatitude, bounds.minLatitude, bounds.maxLatitude);
    m_ProfileView.profileMapMinLongitude = std::clamp(m_ProfileView.profileMapMinLongitude, bounds.minLongitude, bounds.maxLongitude);
    m_ProfileView.profileMapMaxLongitude = std::clamp(m_ProfileView.profileMapMaxLongitude, bounds.minLongitude, bounds.maxLongitude);
    if (m_ProfileView.profileMapMaxLatitude - m_ProfileView.profileMapMinLatitude < 1e-9 || m_ProfileView.profileMapMaxLongitude - m_ProfileView.profileMapMinLongitude < 1e-9)
    {
        refitMapView();
    }

    const double longitudeSpan = std::max(m_ProfileView.profileMapMaxLongitude - m_ProfileView.profileMapMinLongitude, 1e-9);
    const double latitudeSpan = std::max(m_ProfileView.profileMapMaxLatitude - m_ProfileView.profileMapMinLatitude, 1e-9);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    RenderTerrainProfileToolbar(activeProfile);
    float mapWidth = std::max(availableWidth, 360.0f);
    float mapHeight = 420.0f;
    if (m_ProfileView.profileMapSizeMode == ProfileMapSizeMode::Small)
    {
        mapWidth = std::min(mapWidth, 560.0f);
        mapHeight = 260.0f;
    }
    else if (m_ProfileView.profileMapSizeMode == ProfileMapSizeMode::Medium)
    {
        mapWidth = std::min(mapWidth, 760.0f);
        mapHeight = 340.0f;
    }
    else if (m_ProfileView.profileMapSizeMode == ProfileMapSizeMode::Large)
    {
        mapWidth = std::min(mapWidth, 1040.0f);
        mapHeight = 420.0f;
    }
    else
    {
        mapHeight = std::max(ImGui::GetContentRegionAvail().y - 260.0f, 460.0f);
    }
    m_ProfileView.profileMapLastWidth = mapWidth;
    const ImVec2 mapTopLeft = ImGui::GetCursorScreenPos();
    const ImVec2 mapBottomRight(mapTopLeft.x + mapWidth, mapTopLeft.y + mapHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(mapTopLeft, mapBottomRight, true);

    // Clamped version — keeps lines, circles, and labels on-screen.
    const auto mapPointFromGeo = [&](double latitude, double longitude) {
        const float v = static_cast<float>((latitude - m_ProfileView.profileMapMinLatitude) / latitudeSpan);
        const float u = static_cast<float>((longitude - m_ProfileView.profileMapMinLongitude) / longitudeSpan);
        return ImVec2(mapTopLeft.x + (std::clamp(u, 0.0f, 1.0f) * mapWidth),
                      mapBottomRight.y - (std::clamp(v, 0.0f, 1.0f) * mapHeight));
    };
    // Unclamped version — used for AddImageQuad corners so the image scales
    // correctly when zoomed in and corners fall outside the visible canvas.
    // The PushClipRect above ensures nothing outside the canvas is actually drawn.
    const auto mapPointFromGeoUnclamped = [&](double latitude, double longitude) {
        const float v = static_cast<float>((latitude - m_ProfileView.profileMapMinLatitude) / latitudeSpan);
        const float u = static_cast<float>((longitude - m_ProfileView.profileMapMinLongitude) / longitudeSpan);
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
                                                m_ProfileView.profileMapMinLatitude + (static_cast<double>(v) * latitudeSpan),
                                                m_ProfileView.profileMapMinLongitude + (static_cast<double>(u) * longitudeSpan),
                                                m_ProfileView.profileAuxiliaryDrawMode,
                                                profileConverter,
                                                profileCoordinateMode);
    };

    drawList->AddRectFilled(mapTopLeft, mapBottomRight, IM_COL32(34, 44, 56, 255), 4.0f);
    const OverlayEntry* overlay = GetActiveOverlayEntry();
    const bool overlayReady = m_ProfileView.showProfileAerialImage && overlay != nullptr && overlay->image.enabled && overlay->image.loaded && overlay->texture.IsLoaded();
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

    if (m_Isolines.settings().enabled)
    {
        RefreshIsolinesIfNeeded();
        for (const TerrainIsolineSegment& segment : m_Isolines.segments())
        {
            drawList->AddLine(mapPointFromGeoUnclamped(segment.start.latitude, segment.start.longitude),
                              mapPointFromGeoUnclamped(segment.end.latitude, segment.end.longitude),
                              ColorU32(segment.color),
                              m_Isolines.settings().thickness);
        }
    }

    for (const TerrainProfile& profile : m_Profiles.profiles())
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

    if (m_ProfileView.showProfileSamples)
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
        const bool selected = index == m_ProfileView.selectedProfileVertexIndex;
        const ImU32 vertexColor = vertex.auxiliary ? IM_COL32(90, 230, 255, 255) : ProfileColorU32(activeProfile);
        drawList->AddCircleFilled(point, selected ? 6.0f : 4.5f, selected ? IM_COL32(255, 220, 64, 255) : vertexColor, 18);
        drawList->AddCircle(point, selected ? 7.5f : 6.0f, IM_COL32(20, 24, 28, 230), 18, 1.5f);
        char markerLabel[16];
        std::snprintf(markerLabel, sizeof(markerLabel), "%c%d", vertex.auxiliary ? 'A' : 'V', index + 1);
        drawList->AddText(ImVec2(point.x + 7.0f, point.y + 3.0f), IM_COL32(255, 255, 255, 235), markerLabel);
        if (m_ProfileView.showProfileHeightLabels)
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
    if (m_ProfileView.profileGraphHoverActive && m_ProfileView.profileGraphHoverSample.valid)
    {
        highlightedSample = m_ProfileView.profileGraphHoverSample;
        hasHighlightedSample = true;
        hoveredSample = true;
    }
    else
    {
        const int highlightedSampleIndex =
            (m_ProfileView.hoveredProfileSampleIndex >= 0 && m_ProfileView.hoveredProfileSampleIndex < static_cast<int>(activeProfile.samples.size())) ?
                m_ProfileView.hoveredProfileSampleIndex :
                m_ProfileView.selectedProfileSampleIndex;
        if (highlightedSampleIndex >= 0 && highlightedSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            highlightedSample = activeProfile.samples[static_cast<size_t>(highlightedSampleIndex)];
            hasHighlightedSample = true;
            hoveredSample = highlightedSampleIndex == m_ProfileView.hoveredProfileSampleIndex;
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
            const double cursorLatitudeT = std::clamp((hoveredVertex.latitude - m_ProfileView.profileMapMinLatitude) / latitudeSpan, 0.0, 1.0);
            const double cursorLongitudeT = std::clamp((hoveredVertex.longitude - m_ProfileView.profileMapMinLongitude) / longitudeSpan, 0.0, 1.0);
            const double newLatitudeSpan = std::clamp(latitudeSpan * zoomFactor, (bounds.maxLatitude - bounds.minLatitude) * 0.002, bounds.maxLatitude - bounds.minLatitude);
            const double newLongitudeSpan = std::clamp(longitudeSpan * zoomFactor, (bounds.maxLongitude - bounds.minLongitude) * 0.002, bounds.maxLongitude - bounds.minLongitude);
            m_ProfileView.profileMapMinLatitude = hoveredVertex.latitude - (newLatitudeSpan * cursorLatitudeT);
            m_ProfileView.profileMapMaxLatitude = m_ProfileView.profileMapMinLatitude + newLatitudeSpan;
            m_ProfileView.profileMapMinLongitude = hoveredVertex.longitude - (newLongitudeSpan * cursorLongitudeT);
            m_ProfileView.profileMapMaxLongitude = m_ProfileView.profileMapMinLongitude + newLongitudeSpan;
            if (m_ProfileView.profileMapMinLatitude < bounds.minLatitude)
            {
                m_ProfileView.profileMapMaxLatitude += bounds.minLatitude - m_ProfileView.profileMapMinLatitude;
                m_ProfileView.profileMapMinLatitude = bounds.minLatitude;
            }
            if (m_ProfileView.profileMapMaxLatitude > bounds.maxLatitude)
            {
                m_ProfileView.profileMapMinLatitude -= m_ProfileView.profileMapMaxLatitude - bounds.maxLatitude;
                m_ProfileView.profileMapMaxLatitude = bounds.maxLatitude;
            }
            if (m_ProfileView.profileMapMinLongitude < bounds.minLongitude)
            {
                m_ProfileView.profileMapMaxLongitude += bounds.minLongitude - m_ProfileView.profileMapMinLongitude;
                m_ProfileView.profileMapMinLongitude = bounds.minLongitude;
            }
            if (m_ProfileView.profileMapMaxLongitude > bounds.maxLongitude)
            {
                m_ProfileView.profileMapMinLongitude -= m_ProfileView.profileMapMaxLongitude - bounds.maxLongitude;
                m_ProfileView.profileMapMaxLongitude = bounds.maxLongitude;
            }
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
        {
            refitMapView();
        }

        const bool shiftPanning = io.KeyShift && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        if (shiftPanning && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_ProfileView.profileMapIsPanning = true;
            m_ProfileView.profileMapLastPanMouse = {mousePosition.x, mousePosition.y};
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_ProfileView.profileMapIsPanning = false;
        }
        if (m_ProfileView.profileMapIsPanning && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const glm::vec2 currentMouse(mousePosition.x, mousePosition.y);
            const glm::vec2 delta = currentMouse - m_ProfileView.profileMapLastPanMouse;
            m_ProfileView.profileMapLastPanMouse = currentMouse;
            const double longitudeDelta = -(static_cast<double>(delta.x) / static_cast<double>(mapWidth)) * longitudeSpan;
            const double latitudeDelta = (static_cast<double>(delta.y) / static_cast<double>(mapHeight)) * latitudeSpan;
            m_ProfileView.profileMapMinLatitude += latitudeDelta;
            m_ProfileView.profileMapMaxLatitude += latitudeDelta;
            m_ProfileView.profileMapMinLongitude += longitudeDelta;
            m_ProfileView.profileMapMaxLongitude += longitudeDelta;
            if (m_ProfileView.profileMapMinLatitude < bounds.minLatitude)
            {
                m_ProfileView.profileMapMaxLatitude += bounds.minLatitude - m_ProfileView.profileMapMinLatitude;
                m_ProfileView.profileMapMinLatitude = bounds.minLatitude;
            }
            if (m_ProfileView.profileMapMaxLatitude > bounds.maxLatitude)
            {
                m_ProfileView.profileMapMinLatitude -= m_ProfileView.profileMapMaxLatitude - bounds.maxLatitude;
                m_ProfileView.profileMapMaxLatitude = bounds.maxLatitude;
            }
            if (m_ProfileView.profileMapMinLongitude < bounds.minLongitude)
            {
                m_ProfileView.profileMapMaxLongitude += bounds.minLongitude - m_ProfileView.profileMapMinLongitude;
                m_ProfileView.profileMapMinLongitude = bounds.minLongitude;
            }
            if (m_ProfileView.profileMapMaxLongitude > bounds.maxLongitude)
            {
                m_ProfileView.profileMapMinLongitude -= m_ProfileView.profileMapMaxLongitude - bounds.maxLongitude;
                m_ProfileView.profileMapMaxLongitude = bounds.maxLongitude;
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

            if (m_ProfileView.profileEditMode && nearestVertex >= 0 && !m_ProfileView.profileAuxiliaryDrawMode)
            {
                m_ProfileView.selectedProfileVertexIndex = nearestVertex;
            }
            else if (m_ProfileView.profileDrawMode)
            {
                if (m_ProfileView.profileAuxiliaryDrawMode)
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
                        m_ProfileView.selectedProfileVertexIndex = insertIndex;
                        RebuildTerrainProfileSamples(activeProfile);
                        m_StatusMessage = "Inserted auxiliary profile vertex between V vertices.";
                    }
                }
                else
                {
                    activeProfile.vertices.push_back(hoveredVertex);
                    m_ProfileView.selectedProfileVertexIndex = static_cast<int>(activeProfile.vertices.size()) - 1;
                    RebuildTerrainProfileSamples(activeProfile);
                    m_StatusMessage = "Added profile vertex.";
                }
            }
        }

        if (!io.KeyShift && m_ProfileView.profileEditMode && m_ProfileView.selectedProfileVertexIndex >= 0 &&
            m_ProfileView.selectedProfileVertexIndex < static_cast<int>(activeProfile.vertices.size()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            TerrainProfileVertex movedVertex = hoveredVertex;
            movedVertex.auxiliary = activeProfile.vertices[static_cast<size_t>(m_ProfileView.selectedProfileVertexIndex)].auxiliary;
            activeProfile.vertices[static_cast<size_t>(m_ProfileView.selectedProfileVertexIndex)] = movedVertex;
            RebuildTerrainProfileSamples(activeProfile);
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_ProfileView.profileDrawMode = false;
        }
    }

    drawList->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(mapTopLeft.x, mapBottomRight.y));

    if (m_Isolines.settings().enabled)
    {
        const double minHeight = m_Isolines.sampleGrid().IsValid() ? m_Isolines.sampleGrid().minHeight : 0.0;
        const double maxHeight = m_Isolines.sampleGrid().IsValid() ? m_Isolines.sampleGrid().maxHeight : 0.0;
        const double interval = ResolveContourInterval(minHeight, maxHeight, m_Isolines.settings());
        ImGui::Text("Map source: %s  Isolines: %zu%s  Interval: %.2f m  Height: %.2f to %.2f m",
                    overlayReady ? "active aerial overlay" : "terrain bounds grid",
                    m_Isolines.segments().size(),
                    m_Isolines.segmentsDirty() ? " (stale)" : "",
                    interval,
                    minHeight,
                    maxHeight);
        ImGui::SameLine();
        ImGui::Text("Backend: %s%s",
                    m_Isolines.useGpuGeneration() ? (m_Isolines.usedGpu() ? "Metal GPU" : "CPU fallback") : "CPU",
                    m_Isolines.sampleGridDirty() ? "  Grid stale" : "");
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
    if (m_ProfileView.profileGraphZoomMaxDist <= m_ProfileView.profileGraphZoomMinDist)
        return;

    double minLat = std::numeric_limits<double>::max();
    double maxLat = std::numeric_limits<double>::lowest();
    double minLon = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();

    for (const TerrainProfileSample& sample : profile.samples)
    {
        if (!sample.valid) continue;
        if (sample.distanceMeters < m_ProfileView.profileGraphZoomMinDist || sample.distanceMeters > m_ProfileView.profileGraphZoomMaxDist) continue;
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
    m_ProfileView.profileMapMinLatitude  = minLat - latPad;
    m_ProfileView.profileMapMaxLatitude  = maxLat + latPad;
    m_ProfileView.profileMapMinLongitude = minLon - lonPad;
    m_ProfileView.profileMapMaxLongitude = maxLon + lonPad;
    // Do NOT set m_ProfileView.profileMapViewInitialized to false — we are deliberately
    // overriding the view; the clamping in RenderTerrainProfileMap will handle bounds.
}

void Application::RenderTerrainProfileGraph(TerrainProfile& activeProfile)
{
    if (activeProfile.samples.empty())
    {
        m_ProfileView.hoveredProfileSampleIndex = -1;
        m_ProfileView.profileGraphHoverActive = false;
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
        m_ProfileView.hoveredProfileSampleIndex = -1;
        m_ProfileView.profileGraphHoverActive = false;
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                           "The profile has no valid terrain samples inside the selected terrain coverage.");
        return;
    }

    const double centerHeight = 0.5 * (minHeight + maxHeight);
    const double heightRange = std::max(maxHeight - minHeight, 1.0);
    double graphMinHeight = minHeight;
    double graphMaxHeight = maxHeight;
    if (m_ProfileView.profileScaleMode == ProfileElevationScaleMode::Fixed)
    {
        graphMinHeight = m_ProfileView.profileFixedMinHeight;
        graphMaxHeight = m_ProfileView.profileFixedMaxHeight;
    }
    else if (m_ProfileView.profileScaleMode != ProfileElevationScaleMode::Auto)
    {
        double exaggeration = 1.0;
        if (m_ProfileView.profileScaleMode == ProfileElevationScaleMode::TwoX) exaggeration = 2.0;
        else if (m_ProfileView.profileScaleMode == ProfileElevationScaleMode::FiveX) exaggeration = 5.0;
        else if (m_ProfileView.profileScaleMode == ProfileElevationScaleMode::TenX) exaggeration = 10.0;
        const double visibleRange = std::max(heightRange / exaggeration, 1.0);
        graphMinHeight = centerHeight - (visibleRange * 0.5);
        graphMaxHeight = centerHeight + (visibleRange * 0.5);
    }

    const float leftAxisWidth = 74.0f;
    const float graphWidth = std::max(m_ProfileView.profileMapLastWidth, 360.0f);
    const float graphHeight = 220.0f;
    ImGui::Checkbox("Add A Vertices From Height Graph", &m_ProfileView.profileGraphAuxiliaryInsertMode);
    ImGui::SameLine();
    ImGui::TextDisabled("Click the elevation profile to insert an A vertex at the nearest distance.");
    // ── Line-of-sight controls ───────────────────────────────────────────────
    ImGui::Checkbox("Line of sight", &m_ProfileView.lineOfSightEnabled);
    if (m_ProfileView.lineOfSightEnabled)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragFloat("Observer eye (m)", &m_ProfileView.observerEyeHeightMeters, 0.5f, 0.0f, 500.0f, "%.1f"))
        {
            m_ProfileView.observerEyeHeightMeters = std::clamp(m_ProfileView.observerEyeHeightMeters, 0.0f, 500.0f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("from the first vertex");
    }
    // Jump-to-point button: teleports the camera to the currently selected
    // graph sample (left-click a point on the graph to select it).  Right-
    // clicking the graph offers the same action in a context menu.
    {
        const bool hasSelectedSample =
            m_ProfileView.selectedProfileSampleIndex >= 0 &&
            m_ProfileView.selectedProfileSampleIndex < static_cast<int>(activeProfile.samples.size()) &&
            activeProfile.samples[static_cast<size_t>(m_ProfileView.selectedProfileSampleIndex)].valid;
        ImGui::BeginDisabled(!hasSelectedSample);
        if (ImGui::Button("Jump camera to selected point"))
        {
            JumpCameraToProfileSample(activeProfile,
                                      activeProfile.samples[static_cast<size_t>(m_ProfileView.selectedProfileSampleIndex)]);
        }
        ImGui::EndDisabled();
        if (!hasSelectedSample)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(click a point on the graph to select it)");
        }
    }

    // Compute visibility once per frame when enabled; consumed by the line
    // colouring and overlay markers below.
    ProfileLineOfSightResult losResult;
    const bool losActive = m_ProfileView.lineOfSightEnabled && activeProfile.samples.size() >= 2;
    if (losActive)
    {
        losResult = ComputeProfileLineOfSight(activeProfile.samples,
                                              static_cast<double>(m_ProfileView.observerEyeHeightMeters));
    }
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
    double zoomedMinDist = (m_ProfileView.profileGraphZoomMaxDist > 0.0) ? m_ProfileView.profileGraphZoomMinDist : 0.0;
    double zoomedMaxDist = (m_ProfileView.profileGraphZoomMaxDist > 0.0) ? m_ProfileView.profileGraphZoomMaxDist : totalDistance;
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
    const ImU32 kLosVisibleColor = IM_COL32(95, 200, 95, 255);   // green = visible
    const ImU32 kLosHiddenColor  = IM_COL32(225, 80, 70, 255);   // red = hidden
    for (size_t index = 0; index + 1 < activeProfile.samples.size(); ++index)
    {
        if (!activeProfile.samples[index].valid || !activeProfile.samples[index + 1].valid)
        {
            continue;
        }
        // Under line-of-sight, colour each segment by visibility: green only
        // when both ends are visible from the observer, red otherwise.
        ImU32 segmentColor = ProfileColorU32(activeProfile);
        if (losActive)
        {
            const bool bothVisible = losResult.visible[index] && losResult.visible[index + 1];
            segmentColor = bothVisible ? kLosVisibleColor : kLosHiddenColor;
        }
        drawList->AddLine(graphPointFromSample(activeProfile.samples[index]),
                          graphPointFromSample(activeProfile.samples[index + 1]),
                          segmentColor,
                          std::max(activeProfile.thickness, 1.5f));
    }

    // ── Line-of-sight overlay: observer eye (mast), direct sight line to the
    //    endpoint, and a marker at the first blocked point. ──────────────────
    if (losActive && losResult.observerSampleIndex >= 0)
    {
        const auto graphPointFromDistHeight = [&](double distanceMeters, double height) {
            const float x = plotTopLeft.x +
                static_cast<float>((distanceMeters - zoomedMinDist) / zoomedRange) * plotWidth;
            const float y = plotBottomRight.y -
                static_cast<float>((height - graphMinHeight) / graphHeightRange) * graphHeight;
            return ImVec2(x, std::clamp(y, plotTopLeft.y, plotBottomRight.y));
        };
        const TerrainProfileSample& observer =
            activeProfile.samples[static_cast<size_t>(losResult.observerSampleIndex)];
        const ImVec2 groundPt = graphPointFromSample(observer);
        const ImVec2 eyePt = graphPointFromDistHeight(observer.distanceMeters, losResult.observerEyeHeight);
        // Mast from the ground up to the eye, then the eye dot.
        drawList->AddLine(groundPt, eyePt, IM_COL32(120, 200, 255, 230), 2.0f);
        drawList->AddCircleFilled(eyePt, 4.5f, IM_COL32(120, 200, 255, 255), 16);

        // Direct sight line from the eye to the last valid sample (dashed feel
        // via reduced opacity), tinted by whether that endpoint is visible.
        const TerrainProfileSample& endSample = activeProfile.samples.back();
        const ImVec2 endPt = graphPointFromSample(endSample);
        drawList->AddLine(eyePt, endPt,
                          losResult.endpointVisible ? IM_COL32(95, 200, 95, 140)
                                                    : IM_COL32(225, 80, 70, 140),
                          1.5f);

        if (losResult.firstBlockedSampleIndex >= 0 &&
            losResult.firstBlockedSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            const TerrainProfileSample& blocked =
                activeProfile.samples[static_cast<size_t>(losResult.firstBlockedSampleIndex)];
            const ImVec2 blockedPt = graphPointFromSample(blocked);
            drawList->AddCircle(blockedPt, 7.0f, IM_COL32(225, 80, 70, 255), 16, 2.0f);
            drawList->AddLine(ImVec2(blockedPt.x - 5.0f, blockedPt.y - 5.0f),
                              ImVec2(blockedPt.x + 5.0f, blockedPt.y + 5.0f), IM_COL32(225, 80, 70, 255), 2.0f);
            drawList->AddLine(ImVec2(blockedPt.x + 5.0f, blockedPt.y - 5.0f),
                              ImVec2(blockedPt.x - 5.0f, blockedPt.y + 5.0f), IM_COL32(225, 80, 70, 255), 2.0f);
        }
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

    // ── Right-click context menu ─────────────────────────────────────────────
    // Capture the point under the cursor the moment the menu opens (the mouse
    // may move before the popup renders), then offer to jump the camera there.
    // Right-click is unused by the graph's left-click select/insert/zoom
    // gestures, so there is no conflict.
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        const float contextX = std::clamp(ImGui::GetIO().MousePos.x, plotTopLeft.x, plotBottomRight.x);
        const double contextDistance = zoomedMinDist +
            (static_cast<double>(contextX - plotTopLeft.x) / static_cast<double>(plotWidth)) * zoomedRange;
        m_ProfileView.profileGraphContextSample = sampleAtDistance(contextDistance);
    }
    if (ImGui::BeginPopupContextItem("##terrain_profile_graph_context"))
    {
        const TerrainProfileSample& contextSample = m_ProfileView.profileGraphContextSample;
        ImGui::TextDisabled("%.0f m along path  |  %.1f m elevation",
                            contextSample.distanceMeters, contextSample.height);
        ImGui::Separator();
        ImGui::BeginDisabled(!contextSample.valid);
        if (ImGui::MenuItem("Jump camera here"))
        {
            JumpCameraToProfileSample(activeProfile, contextSample);
        }
        ImGui::EndDisabled();
        if (!contextSample.valid)
        {
            ImGui::TextDisabled("(point is outside terrain coverage)");
        }
        ImGui::EndPopup();
    }

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
            m_ProfileView.hoveredProfileSampleIndex = nearestSample;
            const TerrainProfileSample& nearestProfileSample = activeProfile.samples[static_cast<size_t>(nearestSample)];
            const TerrainProfileSample cursorSample = sampleAtDistance(clickedDistance);
            m_ProfileView.profileGraphHoverActive = mouseInsidePlot;
            m_ProfileView.profileGraphHoverSample = cursorSample;
            const TerrainProfileSample& displayedSample = mouseInsidePlot ? cursorSample : nearestProfileSample;
            const ImVec2 samplePoint = graphPointFromSample(displayedSample);
            if (mouseInsidePlot)
            {
                drawList->AddLine(ImVec2(clampedMouseX, plotTopLeft.y),
                                  ImVec2(clampedMouseX, plotBottomRight.y),
                                  m_ProfileView.profileGraphAuxiliaryInsertMode ? IM_COL32(90, 230, 255, 230) : IM_COL32(255, 220, 64, 210),
                                  m_ProfileView.profileGraphAuxiliaryInsertMode ? 2.5f : 2.0f);
            }
            drawList->AddCircleFilled(samplePoint,
                                      m_ProfileView.profileGraphAuxiliaryInsertMode ? 6.0f : 5.0f,
                                      m_ProfileView.profileGraphAuxiliaryInsertMode ? IM_COL32(90, 230, 255, 255) : IM_COL32(255, 220, 64, 255),
                                      18);
            if (m_ProfileView.profileGraphAuxiliaryInsertMode)
            {
                drawList->AddLine(ImVec2(samplePoint.x, plotTopLeft.y),
                                  ImVec2(samplePoint.x, plotBottomRight.y),
                                  IM_COL32(90, 230, 255, 160),
                                  1.5f);
            }
            ImGui::SetTooltip("%s%.2f m along path\nHeight %.2f m\nLine angle %.2f degrees",
                              m_ProfileView.profileGraphAuxiliaryInsertMode ? "Click to insert A vertex\n" : "",
                              displayedSample.distanceMeters,
                              displayedSample.height,
                              displayedSample.lineAngleDegrees);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (m_ProfileView.profileGraphAuxiliaryInsertMode)
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
                        m_ProfileView.selectedProfileVertexIndex = insertIndex;
                        m_ProfileView.selectedProfileSampleIndex = -1;
                        m_ProfileView.hoveredProfileSampleIndex = -1;
                        RebuildTerrainProfileSamples(activeProfile);
                        m_StatusMessage = "Inserted auxiliary profile vertex between V vertices at " +
                                          std::to_string(insertionSample.distanceMeters) + " m.";
                    }
                }
                else
                {
                    m_ProfileView.selectedProfileSampleIndex = nearestSample;
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
            m_ProfileView.profileGraphZoomMinDist = std::max(cursorDist - static_cast<double>(tX) * newRange, 0.0);
            m_ProfileView.profileGraphZoomMaxDist = std::min(m_ProfileView.profileGraphZoomMinDist + newRange, totalDistance);
            SyncProfileMapToGraphZoom(activeProfile);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_ProfileView.profileGraphAuxiliaryInsertMode)
        {
            m_ProfileView.profileGraphZoomMinDist = 0.0;
            m_ProfileView.profileGraphZoomMaxDist = -1.0;
            m_ProfileView.profileMapViewInitialized = false; // also reset the map to full terrain view
        }
    }

    if (!foundHoveredSample)
    {
        m_ProfileView.hoveredProfileSampleIndex = -1;
        m_ProfileView.profileGraphHoverActive = false;
    }

    ImGui::Text("Elevation: %.2f m to %.2f m  Path length: %.2f m", graphMinHeight, graphMaxHeight, totalDistance);
    if (losActive)
    {
        if (losResult.endpointVisible)
        {
            ImGui::TextColored(ImVec4(0.37f, 0.78f, 0.37f, 1.0f),
                               "Line of sight: end of path is VISIBLE from the observer.");
        }
        else if (losResult.firstBlockedSampleIndex >= 0 &&
                 losResult.firstBlockedSampleIndex < static_cast<int>(activeProfile.samples.size()))
        {
            ImGui::TextColored(ImVec4(0.88f, 0.31f, 0.27f, 1.0f),
                               "Line of sight: BLOCKED at %.0f m along the path.",
                               activeProfile.samples[static_cast<size_t>(losResult.firstBlockedSampleIndex)].distanceMeters);
        }
    }
    if (invalidSampleCount > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f),
                           "Warning: %d sample(s) are outside terrain coverage, skipped in the graph, and flagged on export.",
                           invalidSampleCount);
    }
    ImGui::Text("Graph vertices: vertical markers show path vertices and distance from origin. Turn Draw on, then click the height graph to insert a vertex on the top-view line.");
}
} // namespace GeoFPS
