#include "Core/TerrainSystem.h"
#include "Core/Time.h"

#include "Renderer/Mesh.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace GeoFPS
{
void TerrainSystem::ProcessBuildJobs(TerrainJobContext& ctx)
{
    // Adaptive time-budget for GPU chunk uploads.
    //
    // The budget is the MIN of two independent estimates:
    //
    //   1. slackBudget — last frame's actual SwapBuffers wait time (EMA),
    //      minus a safety margin.  This is the directly measured slack the
    //      GPU/vsync gave us.  On a 120Hz display where we're rendering at
    //      ~9ms per frame, swap wait is near zero and slackBudget is 0 —
    //      meaning "you have no headroom, upload only the mandatory chunk."
    //      On a 60Hz display where rendering is ~5ms, swap wait is ~10ms
    //      and we get a generous slackBudget.  This adapts to display rate
    //      WITHOUT us having to know what the rate is.
    //
    //   2. timeBudget — the 60Hz frame target minus elapsed-this-frame.  A
    //      hard ceiling so a single bad frame doesn't blow past 16.67ms,
    //      regardless of what the EMA says.
    //
    // Taking the MIN of the two means: we only upload as much as BOTH the
    // measured slack AND the worst-case frame budget allow.  A previous
    // version used only timeBudget, which on 120Hz incorrectly authorised
    // 7-10ms of uploads on every frame and caused vsync misses.  A version
    // before that used only slackBudget (lagged by one frame).
    //
    // We always do at least one chunk so streaming progresses even when both
    // budgets are zero.
    constexpr double kTargetFrameMs      = 1000.0 / 60.0; // hard ceiling
    constexpr double kBudgetSafetyMargin = 1.5;
    const double elapsedThisFrameMs = NowMs() - ctx.frameStartMs;
    const double timeBudget  = std::max(0.0,
        kTargetFrameMs - elapsedThisFrameMs - kBudgetSafetyMargin);
    const double slackBudget = std::max(0.0,
        static_cast<double>(ctx.avgSwapWaitMs) - kBudgetSafetyMargin);
    const double uploadBudgetMs    = std::min(timeBudget, slackBudget);
    const double uploadWindowStart = NowMs();
    int chunksUploaded = 0;

    // Returns true when the budget still has headroom (first chunk always allowed).
    auto canUploadChunk = [&]() -> bool {
        if (chunksUploaded == 0) return true;
        return (NowMs() - uploadWindowStart) < uploadBudgetMs;
    };

    // Call after every successful Mesh() upload to update the EMA and counters.
    auto onChunkUploaded = [&](double chunkMs) {
        constexpr float kAlpha = 0.1f; // ~10-frame window
        ctx.avgChunkUploadMs = ctx.avgChunkUploadMs * (1.0f - kAlpha) +
                               static_cast<float>(chunkMs) * kAlpha;
        ++chunksUploaded;
        ++ctx.diagnostics.meshUploadsThisFrame;
        ++ctx.diagnostics.tileChunkUploadsThisFrame;
    };

    for (auto iterator = m_TerrainTileBuildJobs.begin(); iterator != m_TerrainTileBuildJobs.end();)
    {
        if (iterator->uploadStarted)
        {
            TerrainTileBuildResult& result = iterator->result;
            if (result.terrainIndex < 0 || result.terrainIndex >= static_cast<int>(m_Datasets.size()))
            {
                iterator = m_TerrainTileBuildJobs.erase(iterator);
                continue;
            }

            TerrainDataset& dataset = m_Datasets[static_cast<size_t>(result.terrainIndex)];
            if (result.tileIndex < 0 || result.tileIndex >= static_cast<int>(dataset.tiles.size()))
            {
                iterator = m_TerrainTileBuildJobs.erase(iterator);
                continue;
            }

            TerrainTile& tile = dataset.tiles[static_cast<size_t>(result.tileIndex)];
            if (!result.success || tile.path != result.path)
            {
                tile.loading = false;
                std::cerr << "[GeoFPS] Terrain tile FAILED: " << result.statusMessage << '\n';
                ctx.statusMessage = result.statusMessage;
                iterator = m_TerrainTileBuildJobs.erase(iterator);
                continue;
            }

            if (iterator->nextChunkUploadIndex < result.chunks.size())
            {
                if (!canUploadChunk())
                {
                    break;
                }

                TerrainMeshChunkData& chunkData = result.chunks[iterator->nextChunkUploadIndex];
                TerrainMeshChunk chunk;
                chunk.minX = chunkData.minX;
                chunk.maxX = chunkData.maxX;
                chunk.minY = chunkData.minY;
                chunk.maxY = chunkData.maxY;
                chunk.minZ = chunkData.minZ;
                chunk.maxZ = chunkData.maxZ;
                chunk.meshData = std::move(chunkData.meshData);
                const double uploadStartMs = NowMs();
                chunk.mesh = std::make_unique<Mesh>(chunk.meshData);
                const double chunkMs = NowMs() - uploadStartMs;
                ctx.diagnostics.meshUploadCpuMs += static_cast<float>(chunkMs);
                onChunkUploaded(chunkMs);
                tile.chunks.push_back(std::move(chunk));
                ++iterator->nextChunkUploadIndex;
            }

            if (iterator->nextChunkUploadIndex < result.chunks.size())
            {
                ++iterator;
                continue;
            }

            tile.loaded = true;
            tile.meshLoaded = !tile.chunks.empty() ||
                              (!tile.terrainMeshData.vertices.empty() && !tile.terrainMeshData.indices.empty());
            tile.loading = false;
            if (tile.meshLoaded)
            {
                std::cout << "[GeoFPS] Terrain tile loaded (row=" << tile.row
                          << " col=" << tile.col << "): " << tile.path << '\n';
            }
            if (result.terrainIndex == m_ActiveIndex)
            {
                // Patch only this tile's grid cells instead of rebuilding the
                // entire sample grid — the heightmap stays correct in real time.
                ctx.isolines.PatchSampleGridForTile(tile.heightGrid,
                                                    tile.loaded,
                                                    tile.bounds.valid,
                                                    tile.bounds.minLatitude,
                                                    tile.bounds.maxLatitude,
                                                    tile.bounds.minLongitude,
                                                    tile.bounds.maxLongitude);
            }

            iterator = m_TerrainTileBuildJobs.erase(iterator);
            continue;
        }

        TerrainTileBuildResult result;
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background terrain tile load failed: ") + exception.what();
        }

        if (result.terrainIndex >= 0 && result.terrainIndex < static_cast<int>(m_Datasets.size()))
        {
            TerrainDataset& dataset = m_Datasets[static_cast<size_t>(result.terrainIndex)];
            if (result.tileIndex >= 0 && result.tileIndex < static_cast<int>(dataset.tiles.size()))
            {
                TerrainTile& tile = dataset.tiles[static_cast<size_t>(result.tileIndex)];
                if (result.success && tile.path == result.path)
                {
                    tile.points = std::move(result.points);
                    tile.heightGrid = std::move(result.heightGrid);
                    tile.terrainMeshData = std::move(result.meshData);
                    tile.chunks.clear();
                    tile.chunks.reserve(result.chunks.size());
                    iterator->result = std::move(result);
                    iterator->nextChunkUploadIndex = 0;
                    iterator->uploadStarted = true;
                    continue;
                }
                else
                {
                    tile.loading = false;
                    std::cerr << "[GeoFPS] Terrain tile FAILED: " << result.statusMessage << '\n';
                    ctx.statusMessage = result.statusMessage;
                }
            }
        }

        iterator = m_TerrainTileBuildJobs.erase(iterator);
    }

    // Two-phase loop: Phase 1 resolves the future and uploads the main mesh;
    // Phase 2 drains render chunks (shared adaptive time-budget with the tile
    // path above) to prevent a large terrain dataset from stalling the frame.
    for (auto iterator = m_TerrainBuildJobs.begin(); iterator != m_TerrainBuildJobs.end();)
    {
        // ── Phase 2: drain chunks within budget ───────────────────────────────
        if (iterator->uploadStarted)
        {
            if (iterator->nextChunkIndex < iterator->pendingChunks.size())
            {
                if (!canUploadChunk())
                {
                    break;
                }
                if (iterator->terrainIndex >= 0 &&
                    iterator->terrainIndex < static_cast<int>(m_Datasets.size()))
                {
                    TerrainDataset& dataset =
                        m_Datasets[static_cast<size_t>(iterator->terrainIndex)];
                    TerrainMeshChunkData& chunkData =
                        iterator->pendingChunks[iterator->nextChunkIndex];
                    TerrainMeshChunk chunk;
                    chunk.minX = chunkData.minX;
                    chunk.maxX = chunkData.maxX;
                    chunk.minY = chunkData.minY;
                    chunk.maxY = chunkData.maxY;
                    chunk.minZ = chunkData.minZ;
                    chunk.maxZ = chunkData.maxZ;
                    chunk.meshData = std::move(chunkData.meshData);
                    const double uploadStartMs = NowMs();
                    chunk.mesh = std::make_unique<Mesh>(chunk.meshData);
                    const double chunkMs = NowMs() - uploadStartMs;
                    ctx.diagnostics.meshUploadCpuMs += static_cast<float>(chunkMs);
                    onChunkUploaded(chunkMs);
                    dataset.chunks.push_back(std::move(chunk));
                }
                ++iterator->nextChunkIndex;
                ++iterator;
                continue;
            }

            // All chunks uploaded — mark loaded and run post-upload callbacks.
            if (iterator->terrainIndex >= 0 &&
                iterator->terrainIndex < static_cast<int>(m_Datasets.size()))
            {
                TerrainDataset& dataset =
                    m_Datasets[static_cast<size_t>(iterator->terrainIndex)];
                dataset.loaded = true;
                ctx.finalizeOverlaysForLoadedTerrain(dataset);
                if (iterator->terrainIndex == m_ActiveIndex)
                {
                    ctx.onActiveTerrainLoadedIntoScene();
                }
                else
                {
                    ctx.onInactiveTerrainProfilesNeedRebuild();
                }
                ctx.statusMessage = iterator->statusMessage;
            }
            iterator = m_TerrainBuildJobs.erase(iterator);
            continue;
        }

        // ── Phase 1: resolve future, upload main mesh, store pending chunks ───
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        TerrainBuildResult result;
        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background terrain load failed: ") + exception.what();
        }

        if (iterator->terrainIndex >= 0 && iterator->terrainIndex < static_cast<int>(m_Datasets.size()))
        {
            TerrainDataset& dataset = m_Datasets[static_cast<size_t>(iterator->terrainIndex)];
            if (result.success)
            {
                std::cout << "[GeoFPS] Terrain '" << dataset.name << "' loaded: "
                          << result.points.size() << " pts, "
                          << result.meshData.indices.size() / 3u << " tris\n";
                dataset.points         = std::move(result.points);
                dataset.geoReference   = result.geoReference;
                dataset.settings       = result.settings;
                dataset.heightGrid     = std::move(result.heightGrid);
                dataset.terrainMeshData = std::move(result.meshData);
                dataset.bounds         = result.bounds;
                // Upload the unified mesh immediately (single object, non-splittable).
                const double uploadStartMs = NowMs();
                dataset.mesh = std::make_unique<Mesh>(dataset.terrainMeshData);
                ctx.diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                ++ctx.diagnostics.meshUploadsThisFrame;
                // Stash chunks for one-per-frame draining in Phase 2.
                dataset.chunks.clear();
                dataset.chunks.reserve(result.chunks.size());
                iterator->pendingChunks = std::move(result.chunks);
                iterator->statusMessage = result.statusMessage;
                iterator->uploadStarted = true;
                // Fall through to the next loop iteration to start draining.
                ++iterator;
                continue;
            }
            else
            {
                std::cerr << "[GeoFPS] Terrain '" << dataset.name << "' FAILED: " << result.statusMessage << '\n';
                dataset.loaded = false;
                dataset.mesh.reset();
                dataset.terrainMeshData = {};
                dataset.chunks.clear();
                dataset.bounds = {};
                ctx.statusMessage = result.statusMessage;
            }
        }
        else
        {
            ctx.statusMessage = result.statusMessage.empty()
                ? "Background terrain job finished for a removed dataset."
                : result.statusMessage;
        }

        iterator = m_TerrainBuildJobs.erase(iterator);
    }
}
} // namespace GeoFPS
