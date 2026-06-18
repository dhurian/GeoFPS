#include "Core/IsolineSystem.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <limits>

namespace GeoFPS
{
void IsolineSystem::PatchSampleGridForTile(const TerrainHeightGrid& tileGrid,
                                           bool tileLoaded,
                                           bool tileBoundsValid,
                                           double tileMinLatitude,
                                           double tileMaxLatitude,
                                           double tileMinLongitude,
                                           double tileMaxLongitude)
{
    // If the sample grid hasn't been built yet, fall back to a full rebuild.
    if (!m_SampleGrid.IsValid())
    {
        MarkSampleGridDirty();
        return;
    }

    // Tile must have usable height data.
    if (!tileLoaded || !tileGrid.IsValid() || !tileBoundsValid)
        return;

    TerrainIsolineSampleGrid& grid = m_SampleGrid;
    const int resX = grid.resolutionX;
    const int resZ = grid.resolutionZ;

    // Patch only the grid cells whose lat/lon falls inside this tile's bounds.
    // For a 64×64 grid over 260 equal tiles, this touches ~16 cells per tile —
    // orders of magnitude cheaper than the full O(resX × resZ × numTiles) rebuild.
    bool updated = false;
    for (int z = 0; z < resZ; ++z)
    {
        const double v        = static_cast<double>(z) / static_cast<double>(resZ - 1);
        const double latitude = grid.minLatitude + (grid.maxLatitude - grid.minLatitude) * v;
        if (latitude < tileMinLatitude || latitude > tileMaxLatitude)
            continue;

        for (int x = 0; x < resX; ++x)
        {
            const double u         = static_cast<double>(x) / static_cast<double>(resX - 1);
            const double longitude = grid.minLongitude + (grid.maxLongitude - grid.minLongitude) * u;
            if (longitude < tileMinLongitude || longitude > tileMaxLongitude)
                continue;

            // Sample directly from this tile — no multi-tile scan.
            grid.heights[static_cast<size_t>(z * resX + x)] =
                static_cast<float>(tileGrid.SampleHeight(latitude, longitude));
            updated = true;
        }
    }

    if (!updated)
        return;

    // Recompute global height extents from the patched grid (cheap: just floats).
    grid.minHeight = std::numeric_limits<double>::max();
    grid.maxHeight = std::numeric_limits<double>::lowest();
    for (const float h : grid.heights)
    {
        grid.minHeight = std::min(grid.minHeight, static_cast<double>(h));
        grid.maxHeight = std::max(grid.maxHeight, static_cast<double>(h));
    }

    // Only the isoline segments need regeneration — the sample grid is already correct.
    MarkSegmentsDirty();
}

void IsolineSystem::RefreshSegments(BackgroundJobQueue* jobs)
{
    // ── Harvest a completed async build ─────────────────────────────────────
    // Check this unconditionally so the result is picked up even on frames
    // where the segments are not marked dirty.
    if (m_BuildPending &&
        m_BuildFuture.valid() &&
        m_BuildFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try
        {
            m_Segments = m_BuildFuture.get();
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[GeoFPS] Async isoline build failed: " << ex.what() << '\n';
            m_Segments.clear();
        }
        m_BuildPending = false;
        m_UsedGpu = false; // CPU path was always used
    }

    // ── Submit a new build if the segments are stale ─────────────────────────
    if (!m_SegmentsDirty)
        return;

    if (!m_SampleGrid.IsValid())
    {
        m_Segments.clear();
        m_UsedGpu = false;
        m_SegmentsDirty = false;
        return;
    }

    // If a build is already in flight, let it finish — don't queue another.
    // The dirty flag stays set and will be honoured after the harvest above.
    if (m_BuildPending)
        return;

    // Submit segment generation to a background worker.  We always use the CPU
    // marching-squares path (useGpu = false) because Metal compute cannot be
    // safely dispatched from a worker thread on macOS.  The previous segments
    // stay visible until the result is harvested — no visual gap.
    auto gridCopy     = m_SampleGrid; // value copy (floats + metadata)
    auto settingsCopy = m_Settings;

    if (jobs != nullptr)
    {
        m_BuildFuture = jobs->Enqueue(
            [grid = std::move(gridCopy), settings = std::move(settingsCopy)]() mutable {
                bool usedGpu = false;
                return GenerateTerrainIsolinesAccelerated(grid, settings, /*useGpu=*/false, &usedGpu);
            });
        m_BuildPending = true;
        m_SegmentsDirty = false;
    }
    else
    {
        // No job queue (e.g. early init) — fall back to a synchronous CPU build.
        m_Segments = GenerateTerrainIsolinesAccelerated(
            m_SampleGrid, m_Settings, /*useGpu=*/false, &m_UsedGpu);
        m_SegmentsDirty = false;
        m_UsedGpu = false;
    }
}
} // namespace GeoFPS
