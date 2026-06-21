#pragma once

#include "Core/DiagnosticsState.h"
#include "Core/IsolineSystem.h"
#include "Core/TerrainDataset.h"
#include "Core/TerrainJobs.h"

#include <functional>
#include <string>
#include <vector>

namespace GeoFPS
{
// Everything ProcessBuildJobs needs from outside the terrain store: the shared
// per-frame upload-budget state, the diagnostics/status sinks it writes, the
// isoline grid it patches, and the three Application-side hooks it fires when a
// dataset finishes loading.  Built fresh by Application each frame and passed by
// reference; the reference members make "all wired up" a compile-time fact.
struct TerrainJobContext
{
    // Shared upload budget.  frameStartMs is a GeoFPS::NowMs() reading taken at
    // the top of the frame (same monotonic clock ProcessBuildJobs measures
    // against), so elapsed-this-frame is meaningful.  avgChunkUploadMs is the
    // read/write EMA of a single Mesh upload.
    double            frameStartMs;
    float&            avgSwapWaitMs;
    float&            avgChunkUploadMs;
    // Sinks written during the loop.
    DiagnosticsState& diagnostics;
    std::string&      statusMessage;
    IsolineSystem&    isolines;
    // Fired when a freshly built dataset's overlays need (re)placing/loading.
    std::function<void(TerrainDataset&)> finalizeOverlaysForLoadedTerrain;
    // Fired when the *active* dataset finishes loading (reload the scene)…
    std::function<void()> onActiveTerrainLoadedIntoScene;
    // …or an inactive one finishes (its profiles may need resampling).
    std::function<void()> onInactiveTerrainProfilesNeedRebuild;
};

// Owns the terrain dataset store: every loaded TerrainDataset and which one is
// currently active.  This is the first step of lifting terrain out of the
// Application god-class.
//
// Scope is deliberately narrow for now.  Application still drives all the
// orchestration — loading datasets/tiles, building meshes, activating a dataset
// into the scene — and still owns the active scene coordinate frame
// (m_GeoReference).  This class is the storage spine those operations read and
// mutate; tightening the interface (moving the load/activate logic in here)
// comes in later steps once the store has a single owner.
class TerrainSystem
{
  public:
    [[nodiscard]] std::vector<TerrainDataset>& datasets() { return m_Datasets; }
    [[nodiscard]] const std::vector<TerrainDataset>& datasets() const { return m_Datasets; }

    [[nodiscard]] int activeIndex() const { return m_ActiveIndex; }
    void setActiveIndex(int index) { m_ActiveIndex = index; }

    // In-flight async build jobs.  Application drives the upload loop
    // (ProcessBackgroundJobs) and mutates these directly for now; the storage
    // lives here so all terrain async state has one owner.
    [[nodiscard]] std::vector<TerrainBuildJob>& terrainBuildJobs() { return m_TerrainBuildJobs; }
    [[nodiscard]] const std::vector<TerrainBuildJob>& terrainBuildJobs() const { return m_TerrainBuildJobs; }
    [[nodiscard]] std::vector<TerrainTileBuildJob>& terrainTileBuildJobs() { return m_TerrainTileBuildJobs; }
    [[nodiscard]] const std::vector<TerrainTileBuildJob>& terrainTileBuildJobs() const { return m_TerrainTileBuildJobs; }

    // Drain the in-flight terrain/tile build jobs onto the GPU, one chunk per
    // frame within the shared upload budget.  Resolves ready futures, uploads
    // meshes, patches the isoline grid for active-dataset tiles, and fires the
    // ctx callbacks when a dataset finishes loading.  Everything it needs from
    // Application arrives through ctx — it never reaches back into the
    // god-class.  Called once per frame from Application::ProcessBackgroundJobs
    // (which keeps the asset/profile-sampling loops).
    void ProcessBuildJobs(TerrainJobContext& ctx);

  private:
    std::vector<TerrainDataset> m_Datasets;
    int m_ActiveIndex {0};
    std::vector<TerrainBuildJob> m_TerrainBuildJobs;
    std::vector<TerrainTileBuildJob> m_TerrainTileBuildJobs;
};
} // namespace GeoFPS
