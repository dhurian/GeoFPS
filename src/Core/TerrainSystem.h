#pragma once

#include "Core/TerrainDataset.h"
#include "Core/TerrainJobs.h"

#include <vector>

namespace GeoFPS
{
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

  private:
    std::vector<TerrainDataset> m_Datasets;
    int m_ActiveIndex {0};
    std::vector<TerrainBuildJob> m_TerrainBuildJobs;
    std::vector<TerrainTileBuildJob> m_TerrainTileBuildJobs;
};
} // namespace GeoFPS
