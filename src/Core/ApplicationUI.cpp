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
    atlasTerrains.reserve(m_Terrain.datasets().size());
    for (const TerrainDataset& dataset : m_Terrain.datasets())
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
            // Tag the active dataset explicitly.  The active terrain is the
            // one whose coordinate frame a click teleports into; making it
            // unmistakable here (beyond just the blue tint) heads off the
            // "clicked the atlas, camera jumped to the wrong place" confusion
            // by showing the user which frame is current before they click.
            if (active)
            {
                const ImVec2 tagPosition(labelPosition.x, labelPosition.y + 14.0f);
                if (tagPosition.y < mapBottomRight.y - 14.0f)
                {
                    drawList->AddText(tagPosition, IM_COL32(120, 210, 255, 255), "ACTIVE");
                }
            }
        }
    }

    // Camera-heading arrow on the atlas.  forward.z corresponds to the
    // world Z axis, which is now NEGATIVE when looking north (after the
    // GeoConverter Z-flip).  Screen-Y also increases downward, so a
    // negative forward.z lands on negative screen-Y = "up on screen" =
    // north — exactly what we want on a north-up atlas.  An older version
    // negated direction.y to compensate for forward.z=+1=north under the
    // old convention; that compensation now points the arrow backwards
    // and is removed.
    const glm::vec3 forward = m_Camera.GetForward();
    const glm::vec2 forward2D(forward.x, forward.z);
    const float forwardLength = glm::length(forward2D);
    glm::vec2 direction = forwardLength > 0.0001f ? (forward2D / forwardLength) : glm::vec2(0.0f, -1.0f);

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
        // Height fallback: if the click is outside any terrain, use the
        // current frame's origin height (NOT the target dataset's, which
        // we won't be in after the teleport).
        float targetHeight = static_cast<float>(m_GeoReference.originHeight);
        if (targetTerrain != nullptr && TerrainDatasetContainsCoordinate(*targetTerrain, targetLatitude, targetLongitude))
        {
            targetHeight = SampleTerrainHeightAt(*targetTerrain, targetLatitude, targetLongitude);
        }
        targetHeight += 2.0f;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            // ── Activate the clicked dataset first ──────────────────────────
            //
            // If the click landed on a terrain that ISN'T the currently
            // active dataset, switch to it before computing the teleport
            // target.  Otherwise the active frame's origin (e.g. central
            // Europe for the sample terrain) is used to project the click
            // point (e.g. Nepal lat/lon), and the resulting "local" position
            // is the *separation* between the two origins — millions of
            // metres from origin in the active frame, beyond float
            // precision and beyond the far clip plane.  Activating the
            // target dataset rebases m_GeoReference onto its origin so the
            // teleport target ends up at small, well-conditioned coords.
            if (targetTerrain != nullptr)
            {
                int targetIndex = -1;
                for (size_t i = 0; i < m_Terrain.datasets().size(); ++i)
                {
                    if (&m_Terrain.datasets()[i] == targetTerrain)
                    {
                        targetIndex = static_cast<int>(i);
                        break;
                    }
                }
                if (targetIndex >= 0 && targetIndex != m_Terrain.activeIndex())
                {
                    ActivateTerrainDataset(targetIndex);
                    // m_GeoReference is now == targetTerrain->geoReference.
                }
            }

            // ── Compute the teleport target in the (now-correct) frame ──────
            // After the activation above, m_GeoReference matches the dataset
            // we want to teleport into, so converting the clicked geographic
            // point to local coordinates gives small numbers (the click
            // location relative to the dataset's origin).
            glm::dvec3 localTarget;
            if (targetTerrain != nullptr &&
                targetTerrain->settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
            {
                // LocalMeters: the atlas X/Z values ARE metres in the active
                // frame (GetDatasetWorldTranslation returns 0 for LocalMeters
                // datasets — they render at the active origin).
                localTarget = glm::dvec3(targetLatitude, targetHeight, targetLongitude);
            }
            else
            {
                const GeoConverter currentFrameConverter(m_GeoReference);
                localTarget = currentFrameConverter.ToLocal(targetLatitude,
                                                            targetLongitude,
                                                            targetHeight);
            }
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
    const bool minimapWindowOpen = ImGui::Begin("Terrain Minimap", &m_ShowMiniMapWindow);
    DrawWindowArrangeMenu("Terrain Minimap");
    if (!minimapWindowOpen)
    {
        ImGui::End();
        return;
    }

    RenderMiniMap();
    ImGui::End();
}

void Application::RenderCameraHud()
{
    // Sibling overlays are evaluated unconditionally — each one self-gates
    // on its own m_Show* flag — so they persist across every workspace tab
    // and stay independent of the lat/lon HUD's terrain-loaded check below.
    RenderDiagnosticsOverlay();
    RenderSpeedSliderOverlay();
    RenderOnScreenOverlaysPanel();

    if (!m_ShowCameraHud)
    {
        return;
    }

    // Note: we DON'T early-return on m_TerrainPoints.empty() here.  That
    // collection is only populated for monolithic (non-tiled) datasets;
    // tile-streamed datasets like Nepal keep their points inside each
    // TerrainTile and leave m_TerrainPoints empty forever.  An older
    // version had this guard and it silently hid the HUD whenever the
    // active dataset was tiled.  m_GeoReference is always initialised
    // (either to {0,0,0} at startup or to the active dataset's reference
    // after LoadActiveTerrainIntoScene), so the lat/lon readout is always
    // meaningful and we can render unconditionally.

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

    // Position only on first use, then let the user drag it anywhere (and,
    // with multi-viewport enabled, out of the main window onto the desktop).
    // A title bar gives an obvious drag handle and an X that hides the box;
    // the Panels menu / On-Screen Overlays panel bring it back.  The position
    // persists between sessions (imgui.ini is local-only / git-ignored).
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("Camera HUD", &m_ShowCameraHud, flags))
    {
        // Active frame name on top.  Many "I clicked the atlas and the camera
        // jumped 6000 km" / "I can't find my terrain" reports trace back to
        // the user not realising which dataset's coordinate frame is active —
        // the lat/lon below are expressed in THIS frame, so showing its name
        // makes the frame visible before any teleport math happens.
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 0.95f, 1.0f), "%s",
                           activeTerrain != nullptr ? activeTerrain->name.c_str()
                                                    : "(no active terrain)");
        ImGui::Separator();
        ImGui::Text("%s %.6f", localMetersMode ? "X" : "Lat", cameraGeo.x);
        ImGui::Text("%s %.6f", localMetersMode ? "Z" : "Lon", cameraGeo.y);
        ImGui::Text("Height %.2f m", cameraGeo.z);

        // Origin-drift indicator.  renderOriginDistanceMeters is how far the
        // camera has drifted from the active frame's local origin;
        // renderOriginFloatStepMeters is the resulting single-precision float
        // spacing at that distance.  Past ~1 M m the spacing coarsens enough
        // (>= 0.05 m, the same threshold the diagnostics panel uses) that
        // WASD movement starts rounding away and the camera feels stuck —
        // the classic "megametre-land" symptom.  We surface it here, amber,
        // with a hint to press H, so the user can self-diagnose instead of
        // thinking movement is broken.
        const float driftMeters = m_Diagnostics.renderOriginDistanceMeters;
        const float floatStep   = m_Diagnostics.renderOriginFloatStepMeters;
        const bool precisionCoarse = floatStep >= 0.05f;
        const char* driftUnit = driftMeters >= 1000.0f ? "km" : "m";
        const float driftValue = driftMeters >= 1000.0f ? driftMeters / 1000.0f : driftMeters;
        if (precisionCoarse)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f),
                               "Origin drift %.1f %s  (~%.2f m steps) - press H",
                               driftValue, driftUnit, floatStep);
        }
        else
        {
            ImGui::TextDisabled("Origin drift %.1f %s", driftValue, driftUnit);
        }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Speed slider — right-edge floating overlay
//
//  Mouse-driven alternative to the +/- keys (which are keyboard-layout
//  dependent — '+' lives on different physical keys on Danish/German/etc.
//  layouts and the positional fallback caused phantom decreases when both
//  keys fired simultaneously).  This overlay is always reachable, never
//  obstructs the gizmo (which sits in the top-right region), and can be
//  hidden via the On-Screen Overlays panel.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderSpeedSliderOverlay()
{
    if (!m_ShowSpeedSlider)
    {
        return;
    }

    constexpr float kSliderWidth   = 220.0f;
    constexpr float kRightPadding  = 16.0f;
    constexpr float kVerticalAnchorFraction = 0.5f; // pinned vertically centred

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + vp->Size.x - kRightPadding,
               vp->Pos.y + vp->Size.y * kVerticalAnchorFraction),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.5f)); // pivot: right-centre of window
    ImGui::SetNextWindowBgAlpha(0.62f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("##speed_slider_overlay", nullptr, flags))
    {
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 0.95f, 1.0f), "Speed");
        ImGui::PushItemWidth(kSliderWidth);
        // Logarithmic mapping gives fine control near the low end (walking
        // speeds, 1–20 m/s) without making the high end (mapping fly-over
        // speeds, hundreds of m/s) tedious to reach.
        if (ImGui::SliderFloat("##base_move_speed",
                               &m_BaseMoveSpeed,
                               1.0f, 3000.0f,
                               "%.1f m/s",
                               ImGuiSliderFlags_Logarithmic))
        {
            m_BaseMoveSpeed = std::clamp(m_BaseMoveSpeed, 0.5f, 3000.0f);
        }
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Current: %.1f m/s", m_FPSController.GetCurrentSpeed());
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  On-Screen Overlays panel — single place to hide/show every persistent HUD
//  element.  Opened from the Panels menu.  Deliberately compact so it can
//  stay docked alongside the other tool windows without dominating them.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderOnScreenOverlaysPanel()
{
    if (!m_ShowOnScreenOverlaysPanel)
    {
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 60.0f, vp->Pos.y + 90.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("On-Screen Overlays", &m_ShowOnScreenOverlaysPanel,
                      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Toggle persistent HUD elements:");
    ImGui::Separator();
    ImGui::Checkbox("Camera HUD (top-left)",      &m_ShowCameraHud);
    ImGui::Checkbox("Speed slider (right edge)",  &m_ShowSpeedSlider);
    ImGui::Checkbox("Orientation gizmo",          &m_ShowOrientationGizmo);
    ImGui::Checkbox("Stats overlay (bottom-left)",&m_Diagnostics.showOverlay);
    ImGui::Spacing();
    ImGui::TextDisabled("Each toggle is also available in Panels menu.");
    ImGui::End();
}

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
    // Snap yaws are chosen so that clicking +Y (the world up axis) gives a
    // top-down view with NORTH at the top of the screen — matching the
    // GeoConverter's north → −Z convention used everywhere else.  The +Z
    // arm naturally aligns with looking north (forward = −Z).
    const std::array<AxisDef, 3> axes = {{
        {{1, 0, 0}, 180.0f,   0.0f},
        {{0, 1, 0}, -90.0f, -89.0f},
        {{0, 0, 1}, -90.0f,   0.0f},
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
    // Keep snap yaws synchronised with the input-handler axes table in
    // ProcessOrientationGizmoInput().  See comment there for why +Y is
    // yaw=−90 (top-down with north on top, after the GeoConverter Z-flip).
    const std::array<AxisDef, 3> axes = {{
        { { 1, 0, 0}, IM_COL32(220,  70,  70, 255), IM_COL32(220,  70,  70, 100), "X", 180.0f,   0.0f },
        { { 0, 1, 0}, IM_COL32( 70, 205, 100, 255), IM_COL32( 70, 205, 100, 100), "Y", -90.0f, -89.0f },
        { { 0, 0, 1}, IM_COL32( 75, 140, 225, 255), IM_COL32( 75, 140, 225, 100), "Z", -90.0f,   0.0f },
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
            for (int i = 0; i < static_cast<int>(m_Terrain.datasets().size()); ++i)
            {
                TerrainDataset& dataset = m_Terrain.datasets()[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::Checkbox("##workspace_terrain_visible", &dataset.visible);
                ImGui::SameLine();
                if (ImGui::Selectable(dataset.name.c_str(), i == m_Terrain.activeIndex()))
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
                StartTerrainBuildJob(m_Terrain.activeIndex());
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Dataset", ImVec2(120.0f, 34.0f)))
            {
                TerrainDataset dataset;
                dataset.name = "Terrain " + std::to_string(m_Terrain.datasets().size() + 1);
                dataset.path = activeTerrain->path;
                dataset.settings = activeTerrain->settings;
                dataset.geoReference = activeTerrain->geoReference;
                dataset.overlays.push_back(OverlayEntry {});
                m_Terrain.datasets().push_back(std::move(dataset));
                ActivateTerrainDataset(static_cast<int>(m_Terrain.datasets().size()) - 1);
                activeTerrain = GetActiveTerrainDataset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Dataset", ImVec2(130.0f, 34.0f)))
            {
                DeleteTerrainDataset(m_Terrain.activeIndex());
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
            for (TerrainDataset& dataset : m_Terrain.datasets())
            {
                if (dataset.visible && (dataset.loaded || StartTerrainBuildJob(static_cast<int>(&dataset - m_Terrain.datasets().data()))))
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
        ImGui::Text("Profiles: %zu", m_Profiles.profiles().size());
        if (m_Profiles.profiles().empty() && ImGui::Button("Create First Profile", ImVec2(180.0f, 34.0f)))
        {
            TerrainProfile profile;
            profile.name = "Profile 1";
            m_Profiles.profiles().push_back(profile);
            m_Profiles.setActiveIndex(0);
        }
        if (!m_Profiles.profiles().empty())
        {
            m_Profiles.setActiveIndex(std::clamp(m_Profiles.activeIndex(), 0, static_cast<int>(m_Profiles.profiles().size()) - 1));
            TerrainProfile& profile = m_Profiles.profiles()[static_cast<size_t>(m_Profiles.activeIndex())];
            EnsureTerrainProfileHasTerrainSelection(profile);

            if (ImGui::BeginCombo("Active Profile", profile.name.c_str()))
            {
                for (int index = 0; index < static_cast<int>(m_Profiles.profiles().size()); ++index)
                {
                    const bool selected = index == m_Profiles.activeIndex();
                    if (ImGui::Selectable(m_Profiles.profiles()[static_cast<size_t>(index)].name.c_str(), selected))
                    {
                        m_Profiles.setActiveIndex(index);
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
        ImGui::Text("Imported assets: %zu", m_Assets.assets().size());
        if (activeAsset != nullptr)
        {
            for (int i = 0; i < static_cast<int>(m_Assets.assets().size()); ++i)
            {
                ImportedAsset& asset = m_Assets.assets()[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::Checkbox("##workspace_asset_selected", &asset.selected);
                ImGui::SameLine();
                if (ImGui::Selectable(asset.name.c_str(), i == m_Assets.activeIndex()))
                {
                    m_Assets.setActiveIndex(i);
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
                StartImportedAssetLoadJob(m_Assets.activeIndex());
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Asset", ImVec2(120.0f, 34.0f)))
            {
                ImportedAsset asset;
                asset.name = "Asset " + std::to_string(m_Assets.assets().size() + 1);
                m_Assets.assets().push_back(std::move(asset));
                m_Assets.setActiveIndex(static_cast<int>(m_Assets.assets().size()) - 1);
                activeAsset = GetActiveImportedAsset();
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Asset", ImVec2(120.0f, 34.0f)))
            {
                DeleteImportedAsset(m_Assets.activeIndex());
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
