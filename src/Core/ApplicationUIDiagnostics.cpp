#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include <algorithm>
#include <array>

namespace GeoFPS
{
using namespace ApplicationInternal;

// ─────────────────────────────────────────────────────────────────────────────
//  Stats overlay — always-visible bottom-left HUD, shown whenever the toggle
//  is on (regardless of which workspace tab is open).
//  Called from RenderCameraHud() unconditionally.
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderDiagnosticsOverlay()
{
    if (!m_Diagnostics.showOverlay)
        return;

    // Collect a few live values
    size_t loadedTiles = 0, totalTiles = 0;
    for (const TerrainDataset& ds : m_TerrainDatasets)
    {
        totalTiles += ds.tiles.size();
        for (const TerrainTile& t : ds.tiles)
            loadedTiles += t.loaded ? 1u : 0u;
    }
    size_t pendingJobs = m_TerrainBuildJobs.size() + m_TerrainTileBuildJobs.size() + m_AssetLoadJobs.size();

    constexpr float PAD = 14.0f;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav             |
        ImGuiWindowFlags_NoMove;

    // ── Position: bottom-left, just above the menu bar height from bottom ─────
    // The camera HUD sits top-left; the gizmo is top-right.
    // Bottom-left is clean, visible, and out of the way of both.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + PAD, vp->Pos.y + vp->Size.y - PAD),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f));  // pivot bottom-left
    ImGui::SetNextWindowBgAlpha(0.62f);

    if (ImGui::Begin("##stats_overlay", nullptr, flags))
    {
        // ── Performance row ───────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f),
            "%.0f FPS", m_Diagnostics.avgFpsDisplay);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f ms", m_Diagnostics.avgFrameTimeMs);
        ImGui::SameLine();
        ImGui::TextDisabled("(min %.1f  max %.1f)", m_Diagnostics.minFrameTimeMs, m_Diagnostics.maxFrameTimeMs);
        ImGui::Text("CPU %.1f  GPU %.1f  Swap %.1f ms",
                    m_Diagnostics.frameCpuMs,
                    m_Diagnostics.gpuFrameMs,
                    m_Diagnostics.swapCpuMs);

        ImGui::Separator();

        // ── Camera row ────────────────────────────────────────────────────────
        const glm::vec3 pos = m_Camera.GetPosition();
        ImGui::Text("Pos  %.1f  %.1f  %.1f", pos.x, pos.y, pos.z);
        ImGui::Text("Yaw %.1f°  Pitch %.1f°  |  Spd %.0f m/s",
            m_Camera.GetYaw(), m_Camera.GetPitch(),
            m_FPSController.GetCurrentSpeed());

        // Gravity indicator (only shown when enabled)
        if (m_GravitySettings.enabled)
        {
            ImGui::SameLine();
            ImGui::TextColored(
                m_OnGround ? ImVec4(0.55f,0.90f,0.65f,1.0f) : ImVec4(0.85f,0.70f,0.35f,1.0f),
                m_OnGround ? "  ▣ ground" : "  ↑ air");
        }

        ImGui::Separator();

        // ── Scene row ────────────────────────────────────────────────────────
        ImGui::Text("Tiles %zu/%zu  |  Assets %zu  |  Tris %zu",
            loadedTiles, totalTiles,
            m_ImportedAssets.size(),
            CountSceneTriangles());
        ImGui::Text("Draws %zu  Visible chunks %zu  Uploads %zu",
                    m_Diagnostics.totalDrawCalls,
                    m_Diagnostics.visibleTerrainChunks,
                    m_Diagnostics.meshUploadsThisFrame);
        ImGui::Text("Origin %.0f m  step %.3f m",
                    m_Diagnostics.renderOriginDistanceMeters,
                    m_Diagnostics.renderOriginFloatStepMeters);

        // Jobs badge (only shown when work is in flight)
        if (pendingJobs > 0)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.70f, 0.35f, 1.0f),
                "  ⟳ %zu job%s", pendingJobs, pendingJobs == 1 ? "" : "s");
        }
    }
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Full diagnostics panel (Workspace → Diagnostics tab)
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderDiagnosticsPanel()
{
    // ── Frame-time graph ──────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "Performance");
    ImGui::Text("FPS: %.1f   avg: %.2f ms   min: %.2f   max: %.2f",
        m_Diagnostics.avgFpsDisplay,
        m_Diagnostics.avgFrameTimeMs,
        m_Diagnostics.minFrameTimeMs,
        m_Diagnostics.maxFrameTimeMs);
    if (m_Diagnostics.gpuTimingAvailable)
    {
        ImGui::Text("CPU frame: %.2f ms   GPU frame: %.2f ms   Swap: %.2f ms",
                    m_Diagnostics.frameCpuMs,
                    m_Diagnostics.gpuFrameMs,
                    m_Diagnostics.swapCpuMs);
    }
    else
    {
        ImGui::Text("CPU frame: %.2f ms   GPU frame: n/a   Swap: %.2f ms",
                    m_Diagnostics.frameCpuMs,
                    m_Diagnostics.swapCpuMs);
    }

    std::array<float, DiagnosticsState::kFrameRingSize> plotData {};
    for (int i = 0; i < DiagnosticsState::kFrameRingSize; ++i)
    {
        plotData[static_cast<size_t>(i)] =
            m_Diagnostics.frameTimesMs[static_cast<size_t>(
                (m_Diagnostics.frameRingHead + i) % DiagnosticsState::kFrameRingSize)];
    }
    const float plotMax = std::max(m_Diagnostics.maxFrameTimeMs * 1.5f, 50.0f);
    ImGui::PlotLines("##frametimes", plotData.data(), DiagnosticsState::kFrameRingSize,
                     0, "Frame time (ms)", 0.0f, plotMax, ImVec2(-1.0f, 70.0f));

    ImGui::Text("Input %.2f  Update/jobs %.2f  UI build %.2f  Camera apply %.2f",
                m_Diagnostics.inputCpuMs,
                m_Diagnostics.updateCpuMs,
                m_Diagnostics.uiBuildCpuMs,
                m_Diagnostics.cameraApplyCpuMs);
    ImGui::Text("Terrain %.2f  Assets %.2f  Sky %.2f  World overlays %.2f  ImGui draw %.2f",
                m_Diagnostics.terrainCpuMs,
                m_Diagnostics.assetCpuMs,
                m_Diagnostics.skyCpuMs,
                m_Diagnostics.worldOverlayCpuMs,
                m_Diagnostics.imguiCpuMs);

    ImGui::Separator();

    // ── Terrain ───────────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "Terrain");
    size_t loadedTiles = 0, totalTiles = 0;
    for (const TerrainDataset& ds : m_TerrainDatasets)
    {
        totalTiles += ds.tiles.size();
        for (const TerrainTile& t : ds.tiles)
            loadedTiles += t.loaded ? 1u : 0u;
    }
    ImGui::Text("Datasets: %zu   Tiles loaded: %zu / %zu",
                m_TerrainDatasets.size(), loadedTiles, totalTiles);
    ImGui::Text("Render origin distance: %.0f m   Float step there: %.3f m",
                m_Diagnostics.renderOriginDistanceMeters,
                m_Diagnostics.renderOriginFloatStepMeters);
    ImGui::Text("Largest dataset offset: %.0f m   Largest render offset: %.1f m",
                m_Diagnostics.maxDatasetWorldTranslationMeters,
                m_Diagnostics.maxRenderTranslationMeters);
    if (m_Diagnostics.renderOriginFloatStepMeters >= 0.05f)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.25f, 1.0f),
                           "Floating origin active: raw world-space float precision would be coarse here.");
    }
    ImGui::Text("Visible tiles: %zu   Visible chunks: %zu",
                m_Diagnostics.visibleTerrainTiles,
                m_Diagnostics.visibleTerrainChunks);
    ImGui::Text("Draw calls: terrain %zu   asset %zu   sky %zu   total %zu",
                m_Diagnostics.terrainDrawCalls,
                m_Diagnostics.assetDrawCalls,
                m_Diagnostics.skyDrawCalls,
                m_Diagnostics.totalDrawCalls);
    ImGui::Text("Triangles drawn: terrain %zu   asset %zu   sky %zu   total %zu",
                m_Diagnostics.terrainTrianglesDrawn,
                m_Diagnostics.assetTrianglesDrawn,
                m_Diagnostics.skyTrianglesDrawn,
                m_Diagnostics.totalTrianglesDrawn);
    ImGui::Text("Mesh uploads this frame: %zu   Tile chunks: %zu   Upload CPU: %.2f ms",
                m_Diagnostics.meshUploadsThisFrame,
                m_Diagnostics.tileChunkUploadsThisFrame,
                m_Diagnostics.meshUploadCpuMs);

    ImGui::Separator();

    // ── Assets ────────────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "Assets");
    size_t loadedAssets = 0, meshMemKB = 0;
    for (const ImportedAsset& a : m_ImportedAssets)
    {
        if (!a.loaded) continue;
        ++loadedAssets;
        for (const ImportedPrimitiveData& p : a.assetData.primitives)
        {
            meshMemKB += p.meshData.vertices.size() * sizeof(Vertex) / 1024u;
            meshMemKB += p.meshData.indices.size()  * sizeof(unsigned int) / 1024u;
        }
    }
    ImGui::Text("Assets loaded: %zu / %zu", loadedAssets, m_ImportedAssets.size());
    ImGui::Text("Mesh memory est: %zu KB", meshMemKB);
    ImGui::Text("Scene triangles: %zu", CountSceneTriangles());

    ImGui::Separator();

    // ── Camera ────────────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "Camera");
    const glm::vec3 pos = m_Camera.GetPosition();
    ImGui::Text("Position: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
    ImGui::Text("Yaw: %.1f°   Pitch: %.1f°", m_Camera.GetYaw(), m_Camera.GetPitch());
    ImGui::Text("Base speed: %.1f m/s   Current: %.1f m/s",
                m_BaseMoveSpeed, m_FPSController.GetCurrentSpeed());
    ImGui::Text("Mouse capture: %s   Gravity: %s   On ground: %s",
                m_MouseCaptured ? "on" : "off",
                m_GravitySettings.enabled ? "on" : "off",
                m_OnGround ? "yes" : "no");
    ImGui::Text("Queued look: %.3f, %.3f deg   Applied look: %.3f, %.3f deg",
                m_Diagnostics.queuedLookDeltaDegrees.x,
                m_Diagnostics.queuedLookDeltaDegrees.y,
                m_Diagnostics.appliedLookDeltaDegrees.x,
                m_Diagnostics.appliedLookDeltaDegrees.y);

    ImGui::Separator();

    // ── Background jobs ───────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.65f, 1.0f), "Background Jobs");
    ImGui::Text("Terrain build: %zu   Tile load: %zu   Asset load: %zu",
                m_TerrainBuildJobs.size(),
                m_TerrainTileBuildJobs.size(),
                m_AssetLoadJobs.size());

    ImGui::Separator();

    // ── Controls reminder ────────────────────────────────────────────────────
    ImGui::TextDisabled("Esc = release mouse   Tab = recapture");
    ImGui::TextDisabled("+/- = speed   Shift = sprint   Space = jump (when gravity on)");
    ImGui::TextDisabled("Numpad 1/3/7 = Front/Right/Top view");

    // ── Overlay toggle ────────────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::Checkbox("Show stats overlay (bottom-left)", &m_Diagnostics.showOverlay);
    bool lowLatencyMode = m_Diagnostics.lowLatencyMode;
    if (ImGui::Checkbox("Low latency mode (disable VSync)", &lowLatencyMode))
    {
        m_Diagnostics.lowLatencyMode = lowLatencyMode;
        m_Window.SetSwapInterval(m_Diagnostics.lowLatencyMode ? 0 : 1);
    }

    ImGuiIO& io = ImGui::GetIO();
    bool platformViewports = (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
    if (ImGui::Checkbox("ImGui platform viewports", &platformViewports))
    {
        m_Diagnostics.platformViewportsEnabled = platformViewports;
        if (platformViewports)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        }
        else
        {
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
        }
    }
    ImGui::TextDisabled("Also toggleable via Panels menu.");
}

} // namespace GeoFPS
