#include "Core/Application.h"
#include "Core/ApplicationInternal.h"
#include "Core/TerrainCoordinateHelpers.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationInternal;
namespace
{
constexpr double kPersistenceEpsilon = 1e-10;

bool IsUnsetCoordinate(const GeoImageCoordinate& coordinate)
{
    return std::abs(coordinate.latitude) <= kPersistenceEpsilon && std::abs(coordinate.longitude) <= kPersistenceEpsilon;
}

bool IsOverlayPlacementUnset(const GeoImageDefinition& image)
{
    return IsUnsetCoordinate(image.topLeft) && IsUnsetCoordinate(image.topRight) &&
           IsUnsetCoordinate(image.bottomLeft) && IsUnsetCoordinate(image.bottomRight);
}

bool IsGeoReferenceUnset(const GeoReference& reference)
{
    return std::abs(reference.originLatitude) <= kPersistenceEpsilon &&
           std::abs(reference.originLongitude) <= kPersistenceEpsilon &&
           std::abs(reference.originHeight) <= kPersistenceEpsilon;
}

void ConvertProjectedTerrainPointsToGeographic(const TerrainBuildSettings& settings,
                                               std::vector<TerrainPoint>& points)
{
    if (settings.coordinateMode != TerrainCoordinateMode::Projected || settings.crs.kind == CrsKind::LocalMeters)
    {
        return;
    }

    for (TerrainPoint& point : points)
    {
        const glm::dvec3 geographic =
            GeoConverter::SourceToGeographic({point.latitude, point.longitude, point.height}, settings.crs);
        point.latitude = geographic.x;
        point.longitude = geographic.y;
        point.height = geographic.z;
    }
}

TerrainImportOptions ImportOptionsForSettings(const TerrainBuildSettings& settings)
{
    TerrainImportOptions options;
    options.sampleStep = std::max(settings.importSampleStep, 1);
    return options;
}

TerrainCoordinateMode CoordinateModeFromManifestText(const std::string& text)
{
    if (text == "local_meters" || text == "local")
    {
        return TerrainCoordinateMode::LocalMeters;
    }
    if (text == "projected")
    {
        return TerrainCoordinateMode::Projected;
    }
    return TerrainCoordinateMode::Geographic;
}

size_t UniqueTerrainCoordinateCount(const std::vector<TerrainPoint>& points, bool latitude)
{
    std::vector<double> values;
    values.reserve(points.size());
    for (const TerrainPoint& point : points)
    {
        values.push_back(latitude ? point.latitude : point.longitude);
    }
    std::sort(values.begin(), values.end());
    const auto end = std::unique(values.begin(), values.end(), [](double a, double b) {
        return std::abs(a - b) <= 1e-10;
    });
    return static_cast<size_t>(std::distance(values.begin(), end));
}

TerrainBuildSettings TileBuildSettings(const TerrainBuildSettings& datasetSettings,
                                       const std::vector<TerrainPoint>& points)
{
    TerrainBuildSettings tileSettings = datasetSettings;
    const int sourceResolutionX = static_cast<int>(UniqueTerrainCoordinateCount(points, false));
    const int sourceResolutionZ = static_cast<int>(UniqueTerrainCoordinateCount(points, true));
    tileSettings.gridResolutionX = std::clamp(sourceResolutionX, 16, 192);
    tileSettings.gridResolutionZ = std::clamp(sourceResolutionZ, 16, 192);
    tileSettings.chunkResolution = std::clamp(tileSettings.chunkResolution, 16, 96);
    return tileSettings;
}

TerrainBuildResult BuildTerrainOnWorker(std::string path,
                                        GeoReference geoReference,
                                        TerrainBuildSettings settings)
{
    TerrainBuildResult result;
    result.geoReference = geoReference;
    result.settings = settings;

    if (path.empty())
    {
        result.statusMessage = "Terrain CSV path is empty.";
        return result;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(path, errorCode))
    {
        result.statusMessage = "Terrain CSV does not exist: " + path;
        return result;
    }

    if (!TerrainImporter::LoadCSV(path, ImportOptionsForSettings(settings), result.points))
    {
        result.statusMessage = "Failed to load terrain: " + path;
        return result;
    }

    if (result.points.empty())
    {
        result.statusMessage = "Terrain file contained no valid points.";
        return result;
    }

    ConvertProjectedTerrainPointsToGeographic(result.settings, result.points);

    if (IsGeoReferenceUnset(result.geoReference))
    {
        result.geoReference.originLatitude = result.points.front().latitude;
        result.geoReference.originLongitude = result.points.front().longitude;
        result.geoReference.originHeight = result.points.front().height;
    }

    result.heightGrid.Build(result.points);
    const TerrainBounds bounds = ComputeTerrainBounds(result.points);
    result.bounds.minLatitude = bounds.minLatitude;
    result.bounds.maxLatitude = bounds.maxLatitude;
    result.bounds.minLongitude = bounds.minLongitude;
    result.bounds.maxLongitude = bounds.maxLongitude;
    result.bounds.minHeight = bounds.minHeight;
    result.bounds.maxHeight = bounds.maxHeight;
    result.bounds.valid = true;

    TerrainMeshBuilder builder;
    GeoConverter converter(result.geoReference);
    result.meshData = builder.BuildFromGeographicPoints(result.points, converter, result.settings);
    result.chunks = builder.BuildChunksFromGeographicPoints(result.points, converter, result.settings);
    if (result.meshData.vertices.empty() || result.meshData.indices.empty())
    {
        result.statusMessage = "Terrain loaded but mesh generation failed: " + path;
        return result;
    }

    result.success = true;
    result.statusMessage = "Loaded terrain in background: " + path;
    return result;
}

TerrainTileBuildResult BuildTerrainTileOnWorker(int terrainIndex,
                                                int tileIndex,
                                                std::string path,
                                                GeoReference geoReference,
                                                TerrainBuildSettings settings)
{
    TerrainTileBuildResult result;
    result.terrainIndex = terrainIndex;
    result.tileIndex = tileIndex;
    result.path = path;

    if (path.empty())
    {
        result.statusMessage = "Terrain tile path is empty.";
        return result;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(path, errorCode))
    {
        result.statusMessage = "Terrain tile does not exist: " + path;
        return result;
    }

    if (!TerrainImporter::LoadCSV(path, ImportOptionsForSettings(settings), result.points))
    {
        result.statusMessage = "Failed to load terrain tile: " + path;
        return result;
    }

    ConvertProjectedTerrainPointsToGeographic(settings, result.points);
    result.heightGrid.Build(result.points);

    const TerrainBuildSettings tileSettings = TileBuildSettings(settings, result.points);
    TerrainMeshBuilder builder;
    GeoConverter converter(geoReference);
    result.meshData = builder.BuildFromGeographicPoints(result.points, converter, tileSettings);
    result.chunks = builder.BuildChunksFromGeographicPoints(result.points, converter, tileSettings);
    if (result.chunks.empty() && (result.meshData.vertices.empty() || result.meshData.indices.empty()))
    {
        result.statusMessage = "Terrain tile loaded but mesh generation failed: " + path;
        return result;
    }

    result.success = true;
    result.statusMessage = "Loaded terrain tile: " + path;
    return result;
}
} // namespace

bool Application::LoadTerrainDataset(TerrainDataset& dataset)
{
    dataset.points.clear();
    dataset.mesh.reset();
    dataset.terrainMeshData = {};
    dataset.chunks.clear();
    dataset.tiles.clear();
    dataset.bounds = {};
    dataset.loaded = false;
    dataset.heightGrid.Build(dataset.points);
    if (dataset.hasTileManifest && !dataset.tileManifestPath.empty())
    {
        TerrainTileManifest manifest;
        std::string errorMessage;
        if (!TerrainImporter::LoadTileManifest(dataset.tileManifestPath, manifest, errorMessage))
        {
            m_StatusMessage = errorMessage.empty() ? "Failed to load terrain tile manifest." : errorMessage;
            return false;
        }

        if (dataset.name.empty() && !manifest.name.empty())
        {
            dataset.name = manifest.name;
        }
        if (IsGeoReferenceUnset(dataset.geoReference))
        {
            dataset.geoReference.originLatitude = manifest.originLatitude;
            dataset.geoReference.originLongitude = manifest.originLongitude;
            dataset.geoReference.originHeight = manifest.originHeight;
        }
        dataset.settings.coordinateMode = CoordinateModeFromManifestText(manifest.coordinateMode);
        dataset.settings.crs = GeoConverter::ParseCrs(manifest.crs);
        dataset.bounds.minLatitude = manifest.minLatitude;
        dataset.bounds.maxLatitude = manifest.maxLatitude;
        dataset.bounds.minLongitude = manifest.minLongitude;
        dataset.bounds.maxLongitude = manifest.maxLongitude;
        dataset.bounds.minHeight = manifest.minHeight;
        dataset.bounds.maxHeight = manifest.maxHeight;
        dataset.bounds.valid = true;
        dataset.tiles.reserve(manifest.tiles.size());
        for (const TerrainTileManifestEntry& entry : manifest.tiles)
        {
            TerrainTile tile;
            tile.path = entry.path;
            tile.row = entry.row;
            tile.col = entry.col;
            tile.pointCount = entry.pointCount;
            tile.bounds.minLatitude = entry.minLatitude;
            tile.bounds.maxLatitude = entry.maxLatitude;
            tile.bounds.minLongitude = entry.minLongitude;
            tile.bounds.maxLongitude = entry.maxLongitude;
            tile.bounds.minHeight = entry.minHeight;
            tile.bounds.maxHeight = entry.maxHeight;
            tile.bounds.valid = true;
            dataset.tiles.push_back(std::move(tile));
        }

        for (auto& overlay : dataset.overlays)
        {
            if (IsOverlayPlacementUnset(overlay.image) && dataset.bounds.valid)
            {
                overlay.image.topLeft = {dataset.bounds.maxLatitude, dataset.bounds.minLongitude};
                overlay.image.topRight = {dataset.bounds.maxLatitude, dataset.bounds.maxLongitude};
                overlay.image.bottomLeft = {dataset.bounds.minLatitude, dataset.bounds.minLongitude};
                overlay.image.bottomRight = {dataset.bounds.minLatitude, dataset.bounds.maxLongitude};
            }
        }

        dataset.loaded = true;
        m_StatusMessage = "Loaded tiled terrain manifest: " + dataset.name + " (" +
                          std::to_string(dataset.tiles.size()) + " tiles)";
        return true;
    }

    if (dataset.path.empty())
    {
        m_StatusMessage = "Terrain CSV path is empty.";
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(dataset.path, errorCode))
    {
        m_StatusMessage = "Terrain CSV does not exist: " + dataset.path;
        return false;
    }

    if (!TerrainImporter::LoadCSV(dataset.path, ImportOptionsForSettings(dataset.settings), dataset.points))
    {
        std::cerr << "Could not load terrain CSV: " << dataset.path << '\n';
        m_StatusMessage = "Failed to load terrain: " + dataset.path;
        return false;
    }

    if (dataset.points.empty())
    {
        m_StatusMessage = "Terrain file contained no valid points.";
        return false;
    }

    ConvertProjectedTerrainPointsToGeographic(dataset.settings, dataset.points);

    if (IsGeoReferenceUnset(dataset.geoReference))
    {
        dataset.geoReference.originLatitude = dataset.points.front().latitude;
        dataset.geoReference.originLongitude = dataset.points.front().longitude;
        dataset.geoReference.originHeight = dataset.points.front().height;
    }
    dataset.loaded = true;
    dataset.heightGrid.Build(dataset.points);
    const TerrainBounds bounds = ComputeTerrainBounds(dataset.points);
    dataset.bounds.minLatitude = bounds.minLatitude;
    dataset.bounds.maxLatitude = bounds.maxLatitude;
    dataset.bounds.minLongitude = bounds.minLongitude;
    dataset.bounds.maxLongitude = bounds.maxLongitude;
    dataset.bounds.minHeight = bounds.minHeight;
    dataset.bounds.maxHeight = bounds.maxHeight;
    dataset.bounds.valid = true;

    for (auto& overlay : dataset.overlays)
    {
        if (IsOverlayPlacementUnset(overlay.image))
        {
            ResetOverlayToTerrainBounds(overlay.image, dataset.points);
        }
    }

    m_StatusMessage = "Loaded terrain: " + dataset.name;
    return true;
}

bool Application::LoadTerrainTile(TerrainDataset& dataset, TerrainTile& tile)
{
    if (tile.loaded && tile.meshLoaded)
    {
        return true;
    }
    if (tile.path.empty())
    {
        m_StatusMessage = "Terrain tile path is empty.";
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(tile.path, errorCode))
    {
        m_StatusMessage = "Terrain tile does not exist: " + tile.path;
        return false;
    }

    tile.points.clear();
    tile.heightGrid.Build(tile.points);
    if (!TerrainImporter::LoadCSV(tile.path, ImportOptionsForSettings(dataset.settings), tile.points))
    {
        m_StatusMessage = "Failed to load terrain tile: " + tile.path;
        return false;
    }
    ConvertProjectedTerrainPointsToGeographic(dataset.settings, tile.points);
    tile.heightGrid.Build(tile.points);

    const TerrainBuildSettings tileSettings = TileBuildSettings(dataset.settings, tile.points);
    TerrainMeshBuilder builder;
    GeoConverter converter(dataset.geoReference);
    tile.terrainMeshData = builder.BuildFromGeographicPoints(tile.points, converter, tileSettings);
    std::vector<TerrainMeshChunkData> chunkData =
        builder.BuildChunksFromGeographicPoints(tile.points, converter, tileSettings);
    tile.chunks.clear();
    tile.chunks.reserve(chunkData.size());
    for (TerrainMeshChunkData& sourceChunk : chunkData)
    {
        TerrainMeshChunk chunk;
        chunk.minX = sourceChunk.minX;
        chunk.maxX = sourceChunk.maxX;
        chunk.minY = sourceChunk.minY;
        chunk.maxY = sourceChunk.maxY;
        chunk.minZ = sourceChunk.minZ;
        chunk.maxZ = sourceChunk.maxZ;
        chunk.meshData = std::move(sourceChunk.meshData);
        chunk.mesh = std::make_unique<Mesh>(chunk.meshData);
        tile.chunks.push_back(std::move(chunk));
    }

    tile.loaded = true;
    tile.meshLoaded = !tile.chunks.empty() || (!tile.terrainMeshData.vertices.empty() && !tile.terrainMeshData.indices.empty());
    tile.loading = false;
    return tile.meshLoaded;
}

bool Application::StartTerrainTileLoadJob(int terrainIndex, int tileIndex)
{
    if (!m_BackgroundJobs || terrainIndex < 0 || terrainIndex >= static_cast<int>(m_Terrain.datasets().size()))
    {
        return false;
    }

    TerrainDataset& dataset = m_Terrain.datasets()[static_cast<size_t>(terrainIndex)];
    if (tileIndex < 0 || tileIndex >= static_cast<int>(dataset.tiles.size()))
    {
        return false;
    }

    TerrainTile& tile = dataset.tiles[static_cast<size_t>(tileIndex)];
    if ((tile.loaded && tile.meshLoaded) || tile.loading)
    {
        return true;
    }

    for (const TerrainTileBuildJob& job : m_Terrain.terrainTileBuildJobs())
    {
        if (job.terrainIndex == terrainIndex && job.tileIndex == tileIndex)
        {
            tile.loading = true;
            return true;
        }
    }

    tile.loading = true;
    TerrainTileBuildJob job;
    job.terrainIndex = terrainIndex;
    job.tileIndex = tileIndex;
    const std::string path = tile.path;
    const GeoReference geoReference = dataset.geoReference;
    const TerrainBuildSettings settings = dataset.settings;
    job.future = m_BackgroundJobs->Enqueue([terrainIndex, tileIndex, path, geoReference, settings]() {
        return BuildTerrainTileOnWorker(terrainIndex, tileIndex, path, geoReference, settings);
    });
    m_Terrain.terrainTileBuildJobs().push_back(std::move(job));
    return true;
}

bool Application::StartTerrainBuildJob(int terrainIndex)
{
    if (terrainIndex < 0 || terrainIndex >= static_cast<int>(m_Terrain.datasets().size()) || !m_BackgroundJobs)
    {
        return false;
    }

    for (const TerrainBuildJob& job : m_Terrain.terrainBuildJobs())
    {
        if (job.terrainIndex == terrainIndex)
        {
            m_StatusMessage = "Terrain build already running: " + m_Terrain.datasets()[static_cast<size_t>(terrainIndex)].name;
            return false;
        }
    }

    TerrainDataset& dataset = m_Terrain.datasets()[static_cast<size_t>(terrainIndex)];
    if (dataset.hasTileManifest)
    {
        const bool loaded = LoadTerrainDataset(dataset);
        if (loaded && terrainIndex == m_Terrain.activeIndex())
        {
            LoadActiveTerrainIntoScene();
        }
        return loaded;
    }

    const std::string path = dataset.path;
    const GeoReference geoReference = dataset.geoReference;
    const TerrainBuildSettings settings = dataset.settings;

    TerrainBuildJob job;
    job.terrainIndex = terrainIndex;
    job.future = m_BackgroundJobs->Enqueue([path, geoReference, settings]() {
        return BuildTerrainOnWorker(path, geoReference, settings);
    });
    m_Terrain.terrainBuildJobs().push_back(std::move(job));
    m_StatusMessage = "Queued background terrain load: " + dataset.name;
    return true;
}

void Application::ProcessBackgroundJobs()
{
    m_Diagnostics.meshUploadsThisFrame = 0;
    m_Diagnostics.tileChunkUploadsThisFrame = 0;
    m_Diagnostics.meshUploadCpuMs = 0.0f;

    // The terrain/tile build-job draining lives in TerrainSystem now.  It needs
    // the shared upload budget, the diagnostics/status sinks, the isoline grid,
    // and three hooks back into Application (overlay finalisation, plus scene or
    // profile refresh when a dataset finishes).  The diagnostics counters were
    // reset just above and the asset loop below also accumulates into them, so
    // the reset stays here rather than moving into ProcessBuildJobs.
    TerrainJobContext terrainCtx {
        m_FrameStartMs,
        m_AvgSwapWaitMs,
        m_AvgChunkUploadMs,
        m_Diagnostics,
        m_StatusMessage,
        m_Isolines,
        [this](TerrainDataset& dataset) {
            for (OverlayEntry& overlay : dataset.overlays)
            {
                if (IsOverlayPlacementUnset(overlay.image))
                {
                    ResetOverlayToTerrainBounds(overlay.image, dataset.points);
                }
                if (overlay.image.enabled)
                {
                    LoadOverlayImage(overlay);
                }
            }
        },
        [this] { LoadActiveTerrainIntoScene(); },
        [this] { RebuildAllTerrainProfileSamples(); },
    };
    m_Terrain.ProcessBuildJobs(terrainCtx);

    // The asset-load draining lives in AssetSystem now.  Unlike the terrain loop
    // it has no orchestration callbacks and no upload budget, so the context is
    // just the diagnostics sink (counters were reset above) and the status string.
    AssetJobContext assetCtx { m_Diagnostics, m_StatusMessage };
    m_Assets.ProcessLoadJobs(assetCtx);

    // Profile-sample draining lives in ProfileSystem now; its context is just the
    // status string (it owns the profiles, active index, and job vector).
    ProfileJobContext profileCtx { m_StatusMessage };
    m_Profiles.ProcessSampleJobs(profileCtx);
}

bool Application::ActivateTerrainDataset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Terrain.datasets().size()))
    {
        return false;
    }

    m_Terrain.setActiveIndex(index);
    TerrainDataset& dataset = m_Terrain.datasets()[static_cast<size_t>(index)];
    if (!dataset.loaded && !LoadTerrainDataset(dataset))
    {
        return false;
    }

    LoadActiveTerrainIntoScene();
    for (OverlayEntry& overlay : dataset.overlays)
    {
        if (overlay.image.enabled && !overlay.texture.IsLoaded())
        {
            LoadOverlayImage(overlay);
        }
    }

    if (dataset.hasTileManifest)
    {
        m_StatusMessage = "Loaded tiled terrain manifest. Visible tiles will stream in while you move.";
        return true;
    }

    return RebuildTerrain();
}

bool Application::LoadOverlayImage(OverlayEntry& overlay)
{
    if (overlay.image.imagePath.empty())
    {
        overlay.image.loaded = false;
        overlay.texture.Reset();
        m_StatusMessage = "Overlay path is empty.";
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(overlay.image.imagePath, errorCode))
    {
        overlay.image.loaded = false;
        overlay.texture.Reset();
        m_StatusMessage = "Overlay image does not exist: " + overlay.image.imagePath;
        return false;
    }

    overlay.image.loaded = overlay.texture.LoadFromFile(overlay.image.imagePath);
    if (!overlay.image.loaded)
    {
        m_StatusMessage = "Failed to load overlay: " + overlay.image.imagePath;
        return false;
    }

    m_StatusMessage = "Loaded overlay: " + overlay.name;
    return true;
}

bool Application::LoadActiveOverlayImage()
{
    OverlayEntry* overlay = GetActiveOverlayEntry();
    if (overlay == nullptr)
    {
        return false;
    }

    return LoadOverlayImage(*overlay);
}

bool Application::DeleteTerrainDataset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Terrain.datasets().size()) || m_Terrain.datasets().size() <= 1)
    {
        return false;
    }

    m_Terrain.datasets().erase(m_Terrain.datasets().begin() + index);
    if (m_Terrain.activeIndex() >= static_cast<int>(m_Terrain.datasets().size()))
    {
        m_Terrain.setActiveIndex(static_cast<int>(m_Terrain.datasets().size()) - 1);
    }

    m_StatusMessage = "Deleted terrain dataset.";
    return ActivateTerrainDataset(m_Terrain.activeIndex());
}

bool Application::DeleteActiveOverlay()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->overlays.size() <= 1)
    {
        return false;
    }

    if (dataset->activeOverlayIndex < 0 || dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return false;
    }

    dataset->overlays.erase(dataset->overlays.begin() + dataset->activeOverlayIndex);
    if (dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        dataset->activeOverlayIndex = static_cast<int>(dataset->overlays.size()) - 1;
    }

    m_StatusMessage = "Deleted overlay.";
    LoadActiveOverlayImage();
    return true;
}

bool Application::RebuildTerrainMesh(TerrainDataset& dataset)
{
    if (dataset.hasTileManifest)
    {
        size_t rebuiltTiles = 0;
        for (TerrainTile& tile : dataset.tiles)
        {
            if (!tile.loaded && !tile.meshLoaded)
            {
                continue;
            }
            tile.loaded = false;
            tile.meshLoaded = false;
            tile.loading = false;
            tile.points.clear();
            tile.heightGrid.Build(tile.points);
            tile.terrainMeshData = {};
            tile.chunks.clear();
            if (LoadTerrainTile(dataset, tile))
            {
                ++rebuiltTiles;
            }
        }
        m_StatusMessage = rebuiltTiles == 0u ?
                              "Tiled terrain ready; visible tiles will load while rendering: " + dataset.name :
                              "Loaded tiled terrain meshes rebuilt: " + dataset.name + " (" +
                                  std::to_string(rebuiltTiles) + " tiles)";
        return true;
    }

    if (!dataset.loaded || dataset.points.empty())
    {
        return false;
    }

    GeoConverter converter(dataset.geoReference);
    TerrainMeshBuilder builder;
    MeshData meshData = builder.BuildFromGeographicPoints(dataset.points, converter, dataset.settings);
    std::vector<TerrainMeshChunkData> chunkData = builder.BuildChunksFromGeographicPoints(dataset.points, converter, dataset.settings);

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        return false;
    }

    dataset.terrainMeshData = meshData;
    dataset.mesh = std::make_unique<Mesh>(meshData);
    dataset.chunks.clear();
    dataset.chunks.reserve(chunkData.size());
    for (TerrainMeshChunkData& sourceChunk : chunkData)
    {
        TerrainMeshChunk chunk;
        chunk.minX = sourceChunk.minX;
        chunk.maxX = sourceChunk.maxX;
        chunk.minY = sourceChunk.minY;
        chunk.maxY = sourceChunk.maxY;
        chunk.minZ = sourceChunk.minZ;
        chunk.maxZ = sourceChunk.maxZ;
        chunk.meshData = std::move(sourceChunk.meshData);
        chunk.mesh = std::make_unique<Mesh>(chunk.meshData);
        dataset.chunks.push_back(std::move(chunk));
    }
    m_StatusMessage = "Terrain mesh rebuilt: " + dataset.name + " (" + std::to_string(meshData.vertices.size()) +
                      " vertices, " + std::to_string(meshData.indices.size() / 3u) + " triangles, " +
                      std::to_string(dataset.chunks.size()) + " chunks)";
    std::cout << "[GeoFPS] " << m_StatusMessage << '\n';
    return true;
}

bool Application::RebuildTerrain()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr)
    {
        return false;
    }

    const bool rebuilt = RebuildTerrainMesh(*dataset);
    return rebuilt;
}

void Application::ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition,
                                              const std::vector<TerrainPoint>& points) const
{
    if (points.empty())
    {
        return;
    }

    double minLatitude = std::numeric_limits<double>::max();
    double maxLatitude = std::numeric_limits<double>::lowest();
    double minLongitude = std::numeric_limits<double>::max();
    double maxLongitude = std::numeric_limits<double>::lowest();

    for (const auto& point : points)
    {
        minLatitude = std::min(minLatitude, point.latitude);
        maxLatitude = std::max(maxLatitude, point.latitude);
        minLongitude = std::min(minLongitude, point.longitude);
        maxLongitude = std::max(maxLongitude, point.longitude);
    }

    imageDefinition.topLeft = {maxLatitude, minLongitude};
    imageDefinition.topRight = {maxLatitude, maxLongitude};
    imageDefinition.bottomLeft = {minLatitude, minLongitude};
    imageDefinition.bottomRight = {minLatitude, maxLongitude};
}

void Application::ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition) const
{
    // For non-tiled terrain the points are in m_TerrainPoints — use them directly.
    if (!m_TerrainPoints.empty())
    {
        ResetOverlayToTerrainBounds(imageDefinition, m_TerrainPoints);
        return;
    }

    // For tiled terrain m_TerrainPoints is empty; fall back to the active dataset's
    // geographic bounds which are always populated from the manifest.
    const TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset != nullptr && dataset->bounds.valid)
    {
        imageDefinition.topLeft     = {dataset->bounds.maxLatitude, dataset->bounds.minLongitude};
        imageDefinition.topRight    = {dataset->bounds.maxLatitude, dataset->bounds.maxLongitude};
        imageDefinition.bottomLeft  = {dataset->bounds.minLatitude, dataset->bounds.minLongitude};
        imageDefinition.bottomRight = {dataset->bounds.minLatitude, dataset->bounds.maxLongitude};
    }
}

void Application::LoadActiveTerrainIntoScene()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr)
    {
        return;
    }

    m_TerrainPoints = dataset->points;
    m_GeoReference = dataset->geoReference;
    m_TerrainSettings = dataset->settings;
    if (!dataset->heightGrid.IsValid())
    {
        dataset->heightGrid.Build(dataset->points);
    }
    m_TerrainHeightGrid = dataset->heightGrid;
    m_ProfileView.profileMapViewInitialized = false;
    m_Isolines.MarkSampleGridDirty();
    RebuildAllTerrainProfileSamples();
    // NOTE: we deliberately do NOT translate the camera into the new frame
    // here.  An earlier version did, on the theory that preserving the
    // camera's *world* position would make a dataset switch invisible.  In
    // practice users want the opposite: when they load Nepal, they want the
    // camera near the Nepal data — not preserved at an old global position
    // that happens to be 9 M m from the new origin.  Preserving caused a
    // "black screen after Nepal load" because the camera ended up far
    // beyond the 50 km far plane.  Recovery from drift is handled instead by
    // the H-key recenter, which puts the camera back above the active dataset.
}
} // namespace GeoFPS
