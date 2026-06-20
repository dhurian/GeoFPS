#pragma once

#include "Core/TerrainDataset.h"

#include <future>
#include <string>
#include <vector>

namespace GeoFPS
{
// Async terrain-build pipeline state — the result payloads produced on worker
// threads and the in-flight jobs that drain them onto the GPU one chunk per
// frame.  Lifted out of class Application (where they had to be public so the
// worker free functions could construct them) so a TerrainSystem can own the
// job vectors without pulling in the whole Application header.
//
// Only the *terrain* jobs live here; asset- and profile-sampling jobs stay in
// Application until their own systems are extracted.
struct TerrainBuildResult
{
    bool success {false};
    std::string statusMessage;
    std::vector<TerrainPoint> points;
    GeoReference geoReference {};
    TerrainBuildSettings settings {};
    TerrainHeightGrid heightGrid;
    MeshData meshData;
    std::vector<TerrainMeshChunkData> chunks;
    TerrainDatasetBounds bounds;
};

struct TerrainBuildJob
{
    int terrainIndex {-1};
    std::future<TerrainBuildResult> future;
    // Chunk-draining state (mirrors TerrainTileBuildJob).
    // Populated when the future resolves; chunks are uploaded one per frame.
    std::vector<TerrainMeshChunkData> pendingChunks;
    size_t nextChunkIndex {0};
    bool uploadStarted {false};
    std::string statusMessage;
};

struct TerrainTileBuildResult
{
    bool success {false};
    std::string statusMessage;
    int terrainIndex {-1};
    int tileIndex {-1};
    std::string path;
    std::vector<TerrainPoint> points;
    TerrainHeightGrid heightGrid;
    MeshData meshData;
    std::vector<TerrainMeshChunkData> chunks;
};

struct TerrainTileBuildJob
{
    int terrainIndex {-1};
    int tileIndex {-1};
    std::future<TerrainTileBuildResult> future;
    TerrainTileBuildResult result;
    size_t nextChunkUploadIndex {0};
    bool uploadStarted {false};
};
} // namespace GeoFPS
