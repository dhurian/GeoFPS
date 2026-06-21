#include "Core/PerformanceLogger.h"
#include "Core/Time.h"

#include <cstdlib>
#include <ctime>
#include <iostream>

namespace GeoFPS
{
std::string PerformanceLogger::Toggle()
{
    if (m_Active)
    {
        // ── Stop ─────────────────────────────────────────────────────────────
        if (m_File.is_open())
        {
            m_File.flush();
            m_File.close();
        }
        m_Active = false;
        std::cout << "[GeoFPS] Performance log STOPPED.  " << m_FrameIdx
                  << " rows written to: " << m_Path << '\n';
        return std::string("Perf log stopped (") + std::to_string(m_FrameIdx) +
               " rows): " + m_Path;
    }

    // ── Start ────────────────────────────────────────────────────────────────
    // Pick a unique filename in the user's home directory (or CWD if HOME is
    // not set).  Ends in .csv so the file is trivially openable in any
    // spreadsheet, and the suffix is a wall-clock timestamp so successive
    // runs don't overwrite each other.
    const char* home = std::getenv("HOME");
    const std::string dir = (home != nullptr) ? std::string(home) : std::string(".");
    char stamp[32] = {0};
    std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", std::localtime(&now));
    m_Path = dir + "/geofps_perf_" + stamp + ".csv";
    m_File.open(m_Path, std::ios::out | std::ios::trunc);
    if (!m_File.is_open())
    {
        std::cerr << "[GeoFPS] Failed to open perf log at " << m_Path << '\n';
        return "Perf log: failed to open " + m_Path;
    }
    // Header: every column we'll write per row.  Order matches WriteRow.
    m_File <<
        "frame,"
        "elapsed_ms,"
        "frame_total_ms,"
        "input_ms,"
        "update_ms,"
        "ui_build_ms,"
        "camera_apply_ms,"
        "terrain_ms,"
        "asset_ms,"
        "sky_ms,"
        "world_overlay_ms,"
        "imgui_draw_ms,"
        "mesh_upload_ms,"
        "swap_ms,"
        "gpu_frame_ms,"
        "visible_terrain_tiles,"
        "visible_terrain_chunks,"
        "terrain_draw_calls,"
        "asset_draw_calls,"
        "sky_draw_calls,"
        "total_draw_calls,"
        "terrain_triangles,"
        "asset_triangles,"
        "total_triangles,"
        "mesh_uploads_this_frame,"
        "tile_chunk_uploads_this_frame,"
        "tile_jobs_pending,"
        "isoline_build_pending,"
        "avg_swap_wait_ms,"
        "render_origin_distance_m,"
        "render_origin_float_step_m,"
        "queued_look_x,"
        "queued_look_y,"
        "applied_look_x,"
        "applied_look_y,"
        "camera_yaw,"
        "camera_pitch,"
        "camera_pos_x,"
        "camera_pos_y,"
        "camera_pos_z,"
        // ── Mouse-input diagnostics ─────────────────────────────────────────
        // The raw kernel-level delta returned by CGGetLastMouseDelta on macOS
        // (or the glfwGetCursorPos diff elsewhere).  Lets us tell whether
        // mouse-look jitter is in the OS event delivery vs. our own pipeline.
        "raw_mouse_dx_px,"
        "raw_mouse_dy_px,"
        // Where the loop's pacer wanted this frame to land vs where it
        // actually started — directly visible "schedule slip".
        "pace_target_ms,"
        "pace_actual_start_ms,"
        "pace_slip_ms"
        "\n";
    m_FrameIdx = 0;
    m_StartMs  = NowMs();
    m_Active   = true;
    std::cout << "[GeoFPS] Performance log STARTED -> " << m_Path << '\n';
    return "Perf log started: " + m_Path;
}

void PerformanceLogger::WriteRow(const DiagnosticsState& diagnostics, const PerfLogExtras& extras)
{
    if (!m_File.is_open())
    {
        return;
    }
    const double elapsedMs = NowMs() - m_StartMs;
    // Each row mirrors the diagnostics panel + a few internals (tile-job
    // queue depth, isoline build pending, EMA swap wait, camera state) so we
    // can correlate spikes with workload.
    m_File
        << m_FrameIdx                                       << ','
        << elapsedMs                                        << ','
        << diagnostics.frameCpuMs                           << ','
        << diagnostics.inputCpuMs                           << ','
        << diagnostics.updateCpuMs                          << ','
        << diagnostics.uiBuildCpuMs                         << ','
        << diagnostics.cameraApplyCpuMs                     << ','
        << diagnostics.terrainCpuMs                         << ','
        << diagnostics.assetCpuMs                           << ','
        << diagnostics.skyCpuMs                             << ','
        << diagnostics.worldOverlayCpuMs                    << ','
        << diagnostics.imguiCpuMs                           << ','
        << diagnostics.meshUploadCpuMs                      << ','
        << diagnostics.swapCpuMs                            << ','
        << diagnostics.gpuFrameMs                           << ','
        << diagnostics.visibleTerrainTiles                  << ','
        << diagnostics.visibleTerrainChunks                 << ','
        << diagnostics.terrainDrawCalls                     << ','
        << diagnostics.assetDrawCalls                       << ','
        << diagnostics.skyDrawCalls                         << ','
        << diagnostics.totalDrawCalls                       << ','
        << diagnostics.terrainTrianglesDrawn                << ','
        << diagnostics.assetTrianglesDrawn                  << ','
        << diagnostics.totalTrianglesDrawn                  << ','
        << diagnostics.meshUploadsThisFrame                 << ','
        << diagnostics.tileChunkUploadsThisFrame            << ','
        << extras.tileJobsPending                           << ','
        << (extras.isolineBuildPending ? 1 : 0)             << ','
        << extras.avgSwapWaitMs                             << ','
        << diagnostics.renderOriginDistanceMeters           << ','
        << diagnostics.renderOriginFloatStepMeters          << ','
        << diagnostics.queuedLookDeltaDegrees.x             << ','
        << diagnostics.queuedLookDeltaDegrees.y             << ','
        << diagnostics.appliedLookDeltaDegrees.x            << ','
        << diagnostics.appliedLookDeltaDegrees.y            << ','
        << extras.cameraYaw                                 << ','
        << extras.cameraPitch                               << ','
        << extras.cameraPosition.x                          << ','
        << extras.cameraPosition.y                          << ','
        << extras.cameraPosition.z                          << ','
        << extras.rawMouseDelta.x                           << ','
        << extras.rawMouseDelta.y                           << ','
        // pace_target_ms = where the pacer was aiming for this frame.
        // pace_actual_start_ms = when this frame actually started.
        // slip = actual - target (positive => late, negative => early).
        << extras.paceTargetMs                              << ','
        << extras.paceActualStartMs                         << ','
        << (extras.paceActualStartMs - extras.paceTargetMs)
        << '\n';
    ++m_FrameIdx;
    // Flush every ~120 rows (~1s @ 120fps) so the file isn't lost if the
    // app crashes mid-recording.
    if ((m_FrameIdx & 127) == 0)
    {
        m_File.flush();
    }
}
} // namespace GeoFPS
