#pragma once

#include "Core/BackgroundJobQueue.h"
#include "Terrain/TerrainProfile.h"

#include <future>
#include <vector>

namespace GeoFPS
{
// Owns the terrain-isoline pipeline: the contour settings, the cached
// height-sample grid, the generated line segments, their dirty flags, and the
// async segment-generation job.  Application drives it each frame:
//
//   1. it rebuilds the sample grid (terrain-coupled — Application samples the
//      terrain and hands the finished grid back via SetSampleGrid), then
//   2. RefreshSegments() harvests any finished async build and submits a new
//      one when the segments are stale.
//
// The sample-grid *construction* stays in Application because it samples the
// active terrain datasets; everything else — storage, dirty tracking, the
// async marching-squares build, and per-tile grid patching — lives here.
class IsolineSystem
{
  public:
    [[nodiscard]] TerrainIsolineSettings& settings() { return m_Settings; }
    [[nodiscard]] const TerrainIsolineSettings& settings() const { return m_Settings; }
    [[nodiscard]] const TerrainIsolineSampleGrid& sampleGrid() const { return m_SampleGrid; }
    [[nodiscard]] const std::vector<TerrainIsolineSegment>& segments() const { return m_Segments; }
    [[nodiscard]] bool segmentsDirty() const { return m_SegmentsDirty; }
    [[nodiscard]] bool sampleGridDirty() const { return m_SampleGridDirty; }
    [[nodiscard]] bool usedGpu() const { return m_UsedGpu; }
    // True while an async segment build is in flight (diagnostics).
    [[nodiscard]] bool segmentBuildPending() const { return m_BuildPending; }
    [[nodiscard]] bool& useGpuGeneration() { return m_UseGpuGeneration; }
    [[nodiscard]] bool useGpuGeneration() const { return m_UseGpuGeneration; }

    // Mark the line segments stale (terrain heights or settings changed but the
    // sample grid is still valid — only the contours need regenerating).
    void MarkSegmentsDirty() { m_SegmentsDirty = true; }
    // Mark the sample grid stale (terrain footprint changed) — implies the
    // segments are stale too.
    void MarkSampleGridDirty()
    {
        m_SampleGridDirty = true;
        m_SegmentsDirty = true;
    }

    // Install a freshly built sample grid (Application builds it from terrain).
    // Clears the sample-grid dirty flag and marks the segments stale.
    void SetSampleGrid(TerrainIsolineSampleGrid grid)
    {
        m_SampleGrid = std::move(grid);
        m_SampleGridDirty = false;
        m_SegmentsDirty = true;
    }

    // Patch the existing sample grid in place from a single tile's height data
    // (cheap incremental update as tiles stream in).  If no valid grid exists
    // yet, marks the grid stale for a full rebuild instead.  Marks the segments
    // stale when any cell is patched.
    void PatchSampleGridForTile(const TerrainHeightGrid& tileGrid,
                                bool tileLoaded,
                                bool tileBoundsValid,
                                double tileMinLatitude,
                                double tileMaxLatitude,
                                double tileMinLongitude,
                                double tileMaxLongitude);

    // Per-frame: harvest a finished async segment build, then submit a new one
    // when the segments are stale and the sample grid is valid.  jobs may be
    // null (early init) — the build then runs synchronously.
    void RefreshSegments(BackgroundJobQueue* jobs);

  private:
    TerrainIsolineSettings m_Settings {};
    TerrainIsolineSampleGrid m_SampleGrid {};
    std::vector<TerrainIsolineSegment> m_Segments {};
    bool m_SegmentsDirty {true};
    bool m_SampleGridDirty {true};
    bool m_UseGpuGeneration {true};
    bool m_UsedGpu {false};
    std::future<std::vector<TerrainIsolineSegment>> m_BuildFuture {};
    bool m_BuildPending {false};
};
} // namespace GeoFPS
