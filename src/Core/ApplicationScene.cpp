#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>

namespace GeoFPS
{
using namespace ApplicationInternal;
namespace
{
constexpr double kPersistenceEpsilon = 1e-10;

double NowMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

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

void ReleasePixelBuffer(std::vector<unsigned char>& pixels)
{
    std::vector<unsigned char>().swap(pixels);
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

glm::dvec3 TerrainCoordinateToLocal(const TerrainDataset& dataset, double latitude, double longitude, double height)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {latitude, height, longitude};
    }

    return GeoConverter(dataset.geoReference).ToLocal(latitude, longitude, height);
}

glm::dvec3 LocalToTerrainCoordinate(const TerrainDataset& dataset, const glm::dvec3& localPosition)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {localPosition.x, localPosition.z, localPosition.y};
    }

    return GeoConverter(dataset.geoReference).ToGeographic(localPosition);
}

float RawTerrainHeightToRenderedLocalHeight(const TerrainDataset& dataset, double terrainHeight)
{
    const double baseHeight = dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters ?
                                  terrainHeight :
                                  terrainHeight - dataset.geoReference.originHeight;
    return static_cast<float>(baseHeight * static_cast<double>(dataset.settings.heightScale));
}

bool TerrainDatasetContainsCoordinate(const TerrainDataset& dataset, double latitude, double longitude)
{
    if (!dataset.bounds.valid)
    {
        return false;
    }

    return latitude >= dataset.bounds.minLatitude && latitude <= dataset.bounds.maxLatitude &&
           longitude >= dataset.bounds.minLongitude && longitude <= dataset.bounds.maxLongitude;
}

TerrainProfileVertex ProfileVertexToDatasetCoordinate(const TerrainProfile& profile,
                                                      const TerrainProfileVertex& vertex,
                                                      const TerrainDataset& dataset)
{
    if (!profile.useLocalCoordinates)
    {
        return vertex;
    }

    const glm::dvec3 coordinate = LocalToTerrainCoordinate(dataset, vertex.localPosition);
    TerrainProfileVertex converted = vertex;
    converted.latitude = coordinate.x;
    converted.longitude = coordinate.y;
    return converted;
}

Application::TerrainBuildResult BuildTerrainOnWorker(std::string path,
                                                      GeoReference geoReference,
                                                      TerrainBuildSettings settings)
{
    Application::TerrainBuildResult result;
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

Application::TerrainTileBuildResult BuildTerrainTileOnWorker(int terrainIndex,
                                                             int tileIndex,
                                                             std::string path,
                                                             GeoReference geoReference,
                                                             TerrainBuildSettings settings)
{
    Application::TerrainTileBuildResult result;
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

Application::AssetLoadResult LoadAssetOnWorker(std::string path)
{
    Application::AssetLoadResult result;
    if (path.empty())
    {
        result.statusMessage = "Asset path is empty.";
        return result;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(path, errorCode))
    {
        result.statusMessage = "Asset file does not exist: " + path;
        return result;
    }

    const size_t extensionOffset = path.find_last_of('.');
    const std::string extension = extensionOffset == std::string::npos ? std::string() : path.substr(extensionOffset);
    std::string errorMessage;
    if (extension == ".glb" || extension == ".gltf" || extension == ".GLB" || extension == ".GLTF")
    {
        result.success = GltfImporter::Load(path, result.assetData, errorMessage);
    }
    else if (extension == ".obj" || extension == ".OBJ")
    {
        ImportedPrimitiveData primitive;
        result.success = ObjImporter::Load(path, primitive.meshData, errorMessage);
        if (result.success)
        {
            primitive.materialName = "OBJ Material";
            result.assetData.primitives.push_back(std::move(primitive));
        }
    }
    else
    {
        result.statusMessage = "Unsupported asset format. Use .glb, .gltf, or .obj.";
        return result;
    }

    result.statusMessage = result.success ? "Loaded asset data in background: " + path :
                                            "Failed to import asset: " + errorMessage;
    return result;
}

struct ProfileSamplingTerrainSnapshot
{
    std::string name;
    GeoReference geoReference {};
    TerrainCoordinateMode coordinateMode {TerrainCoordinateMode::Geographic};
    TerrainHeightGrid heightGrid;
    bool loaded {false};
};

bool ProfileIncludesTerrainName(const TerrainProfile& profile,
                                const ProfileSamplingTerrainSnapshot& terrain,
                                int terrainIndex,
                                int activeTerrainIndex)
{
    if (profile.includedTerrainNames.empty())
    {
        return activeTerrainIndex < 0 || terrainIndex == activeTerrainIndex;
    }

    return std::find(profile.includedTerrainNames.begin(), profile.includedTerrainNames.end(), terrain.name) !=
           profile.includedTerrainNames.end();
}

const ProfileSamplingTerrainSnapshot* PrimaryTerrainForProfileSnapshot(
    const TerrainProfile& profile,
    const std::vector<ProfileSamplingTerrainSnapshot>& terrains,
    int activeTerrainIndex)
{
    for (int terrainIndex = 0; terrainIndex < static_cast<int>(terrains.size()); ++terrainIndex)
    {
        const ProfileSamplingTerrainSnapshot& terrain = terrains[static_cast<size_t>(terrainIndex)];
        if (terrain.loaded && terrain.heightGrid.IsValid() &&
            ProfileIncludesTerrainName(profile, terrain, terrainIndex, activeTerrainIndex))
        {
            return &terrain;
        }
    }

    if (activeTerrainIndex >= 0 && activeTerrainIndex < static_cast<int>(terrains.size()))
    {
        const ProfileSamplingTerrainSnapshot& activeTerrain = terrains[static_cast<size_t>(activeTerrainIndex)];
        if (activeTerrain.loaded && activeTerrain.heightGrid.IsValid())
        {
            return &activeTerrain;
        }
    }

    return nullptr;
}

Application::ProfileSampleBuildResult BuildProfileSamplesOnWorker(
    std::vector<TerrainProfile> profiles,
    std::vector<ProfileSamplingTerrainSnapshot> terrains,
    int activeTerrainIndex)
{
    Application::ProfileSampleBuildResult result;
    result.profiles = std::move(profiles);
    if (result.profiles.empty())
    {
        result.success = true;
        result.statusMessage = "No terrain profiles to sample.";
        return result;
    }

    for (TerrainProfile& profile : result.profiles)
    {
        const ProfileSamplingTerrainSnapshot* terrain =
            PrimaryTerrainForProfileSnapshot(profile, terrains, activeTerrainIndex);
        if (terrain == nullptr)
        {
            profile.samples.clear();
            continue;
        }

        GeoConverter converter(terrain->geoReference);
        profile.samples = SampleTerrainProfile(profile.vertices,
                                               terrain->heightGrid,
                                               converter,
                                               profile.sampleSpacingMeters,
                                               profile.useLocalCoordinates,
                                               terrain->coordinateMode);
    }

    result.success = true;
    result.statusMessage = "Rebuilt terrain profile samples in background.";
    return result;
}

void UploadImportedPrimitiveTextures(ImportedPrimitiveData& primitive)
{
    primitive.hasBaseColorTexture =
        !primitive.baseColorPixels.empty() && primitive.baseColorTexture.LoadFromMemory(primitive.baseColorPixels.data(),
                                                                                        primitive.baseColorWidth,
                                                                                        primitive.baseColorHeight,
                                                                                        primitive.baseColorChannels);
    ReleasePixelBuffer(primitive.baseColorPixels);

    primitive.hasMetallicRoughnessTexture =
        !primitive.metallicRoughnessPixels.empty() &&
        primitive.metallicRoughnessTexture.LoadFromMemory(primitive.metallicRoughnessPixels.data(),
                                                          primitive.metallicRoughnessWidth,
                                                          primitive.metallicRoughnessHeight,
                                                          primitive.metallicRoughnessChannels);
    ReleasePixelBuffer(primitive.metallicRoughnessPixels);

    primitive.hasNormalTexture =
        !primitive.normalPixels.empty() && primitive.normalTexture.LoadFromMemory(primitive.normalPixels.data(),
                                                                                  primitive.normalWidth,
                                                                                  primitive.normalHeight,
                                                                                  primitive.normalChannels);
    ReleasePixelBuffer(primitive.normalPixels);

    primitive.hasEmissiveTexture =
        !primitive.emissivePixels.empty() && primitive.emissiveTexture.LoadFromMemory(primitive.emissivePixels.data(),
                                                                                      primitive.emissiveWidth,
                                                                                      primitive.emissiveHeight,
                                                                                      primitive.emissiveChannels);
    ReleasePixelBuffer(primitive.emissivePixels);
}
} // namespace

TerrainDataset* Application::GetActiveTerrainDataset()
{
    if (m_ActiveTerrainIndex < 0 || m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return nullptr;
    }

    return &m_TerrainDatasets[static_cast<size_t>(m_ActiveTerrainIndex)];
}

const TerrainDataset* Application::GetActiveTerrainDataset() const
{
    if (m_ActiveTerrainIndex < 0 || m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return nullptr;
    }

    return &m_TerrainDatasets[static_cast<size_t>(m_ActiveTerrainIndex)];
}

OverlayEntry* Application::GetActiveOverlayEntry()
{
    TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->activeOverlayIndex < 0 ||
        dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return nullptr;
    }

    return &dataset->overlays[static_cast<size_t>(dataset->activeOverlayIndex)];
}

const OverlayEntry* Application::GetActiveOverlayEntry() const
{
    const TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr || dataset->activeOverlayIndex < 0 ||
        dataset->activeOverlayIndex >= static_cast<int>(dataset->overlays.size()))
    {
        return nullptr;
    }

    return &dataset->overlays[static_cast<size_t>(dataset->activeOverlayIndex)];
}

ImportedAsset* Application::GetActiveImportedAsset()
{
    if (m_ActiveImportedAssetIndex < 0 || m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        return nullptr;
    }

    return &m_ImportedAssets[static_cast<size_t>(m_ActiveImportedAssetIndex)];
}

const ImportedAsset* Application::GetActiveImportedAsset() const
{
    if (m_ActiveImportedAssetIndex < 0 || m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        return nullptr;
    }

    return &m_ImportedAssets[static_cast<size_t>(m_ActiveImportedAssetIndex)];
}

size_t Application::GetSelectedImportedAssetCount() const
{
    return static_cast<size_t>(std::count_if(m_ImportedAssets.begin(), m_ImportedAssets.end(), [](const ImportedAsset& asset) {
        return asset.selected;
    }));
}

void Application::CopySelectedImportedAssets()
{
    m_AssetClipboard.clear();
    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.selected)
        {
            continue;
        }

        AssetClipboardEntry entry;
        entry.name = asset.name;
        entry.path = asset.path;
        entry.useGeographicPlacement = asset.useGeographicPlacement;
        entry.latitude = asset.latitude;
        entry.longitude = asset.longitude;
        entry.height = asset.height;
        entry.position = asset.position;
        entry.rotationZDegrees = asset.rotationDegrees.z;
        entry.scale = asset.scale;
        entry.tint = asset.tint;
        entry.showLabel = asset.showLabel;
        m_AssetClipboard.push_back(entry);
    }

    m_StatusMessage = m_AssetClipboard.empty() ? "No selected assets to copy." :
                                               "Copied " + std::to_string(m_AssetClipboard.size()) + " asset(s).";
}

void Application::PasteCopiedImportedAssets()
{
    if (m_AssetClipboard.empty())
    {
        m_StatusMessage = "Clipboard has no copied assets.";
        return;
    }

    for (ImportedAsset& asset : m_ImportedAssets)
    {
        asset.selected = false;
    }

    for (size_t index = 0; index < m_AssetClipboard.size(); ++index)
    {
        const AssetClipboardEntry& entry = m_AssetClipboard[index];
        ImportedAsset asset;
        asset.name = entry.name + " Copy";
        asset.path = entry.path;
        asset.useGeographicPlacement = entry.useGeographicPlacement;
        asset.latitude = entry.latitude;
        asset.longitude = entry.longitude;
        asset.height = entry.height;
        asset.position = entry.position;
        asset.rotationDegrees.z = entry.rotationZDegrees;
        asset.scale = entry.scale;
        asset.tint = entry.tint;
        asset.showLabel = entry.showLabel;
        asset.selected = true;
        const glm::vec3 offset = m_AssetPasteOffset * static_cast<float>(index + 1);
        if (asset.useGeographicPlacement)
        {
            GeoConverter converter(m_GeoReference);
            const glm::dvec3 localPosition = converter.ToLocal(asset.latitude, asset.longitude, asset.height) +
                                             glm::dvec3(offset);
            const glm::dvec3 geographic = converter.ToGeographic(localPosition);
            asset.latitude = geographic.x;
            asset.longitude = geographic.y;
            asset.height = geographic.z;
            UpdateImportedAssetPositionFromGeographic(asset);
        }
        else
        {
            asset.position += offset;
        }
        if (!asset.path.empty())
        {
            LoadImportedAsset(asset);
        }
        m_ImportedAssets.push_back(std::move(asset));
    }

    m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
    m_StatusMessage = "Pasted " + std::to_string(m_AssetClipboard.size()) + " asset(s).";
}

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
    if (!m_BackgroundJobs || terrainIndex < 0 || terrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return false;
    }

    TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(terrainIndex)];
    if (tileIndex < 0 || tileIndex >= static_cast<int>(dataset.tiles.size()))
    {
        return false;
    }

    TerrainTile& tile = dataset.tiles[static_cast<size_t>(tileIndex)];
    if ((tile.loaded && tile.meshLoaded) || tile.loading)
    {
        return true;
    }

    for (const TerrainTileBuildJob& job : m_TerrainTileBuildJobs)
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
    m_TerrainTileBuildJobs.push_back(std::move(job));
    return true;
}

bool Application::StartTerrainBuildJob(int terrainIndex)
{
    if (terrainIndex < 0 || terrainIndex >= static_cast<int>(m_TerrainDatasets.size()) || !m_BackgroundJobs)
    {
        return false;
    }

    for (const TerrainBuildJob& job : m_TerrainBuildJobs)
    {
        if (job.terrainIndex == terrainIndex)
        {
            m_StatusMessage = "Terrain build already running: " + m_TerrainDatasets[static_cast<size_t>(terrainIndex)].name;
            return false;
        }
    }

    TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(terrainIndex)];
    if (dataset.hasTileManifest)
    {
        const bool loaded = LoadTerrainDataset(dataset);
        if (loaded && terrainIndex == m_ActiveTerrainIndex)
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
    m_TerrainBuildJobs.push_back(std::move(job));
    m_StatusMessage = "Queued background terrain load: " + dataset.name;
    return true;
}

void Application::ProcessBackgroundJobs()
{
    m_Diagnostics.meshUploadsThisFrame = 0;
    m_Diagnostics.tileChunkUploadsThisFrame = 0;
    m_Diagnostics.meshUploadCpuMs = 0.0f;

    // Limit terrain tile GPU uploads to one chunk per frame. A single tile can
    // contain many render chunks, and uploading all of them together is enough
    // to make mouse look feel jerky.
    int tileChunkUploadsThisFrame = 0;
    constexpr int kMaxTileChunkUploadsPerFrame = 1;

    for (auto iterator = m_TerrainTileBuildJobs.begin(); iterator != m_TerrainTileBuildJobs.end();)
    {
        if (iterator->uploadStarted)
        {
            TerrainTileBuildResult& result = iterator->result;
            if (result.terrainIndex < 0 || result.terrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
            {
                iterator = m_TerrainTileBuildJobs.erase(iterator);
                continue;
            }

            TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(result.terrainIndex)];
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
                m_StatusMessage = result.statusMessage;
                iterator = m_TerrainTileBuildJobs.erase(iterator);
                continue;
            }

            if (iterator->nextChunkUploadIndex < result.chunks.size())
            {
                if (tileChunkUploadsThisFrame >= kMaxTileChunkUploadsPerFrame)
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
                m_Diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                ++m_Diagnostics.meshUploadsThisFrame;
                ++m_Diagnostics.tileChunkUploadsThisFrame;
                tile.chunks.push_back(std::move(chunk));
                ++iterator->nextChunkUploadIndex;
                ++tileChunkUploadsThisFrame;
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
            if (result.terrainIndex == m_ActiveTerrainIndex)
            {
                MarkTerrainIsolineSampleGridDirty();
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

        if (result.terrainIndex >= 0 && result.terrainIndex < static_cast<int>(m_TerrainDatasets.size()))
        {
            TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(result.terrainIndex)];
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
                    m_StatusMessage = result.statusMessage;
                }
            }
        }

        iterator = m_TerrainTileBuildJobs.erase(iterator);
    }

    // Two-phase loop: Phase 1 resolves the future and uploads the main mesh;
    // Phase 2 drains render chunks one per frame (same kMaxTileChunkUploadsPerFrame
    // budget shared with the tile path above) to prevent a single large terrain
    // dataset from stalling the frame for tens of milliseconds.
    for (auto iterator = m_TerrainBuildJobs.begin(); iterator != m_TerrainBuildJobs.end();)
    {
        // ── Phase 2: drain one chunk per frame ────────────────────────────────
        if (iterator->uploadStarted)
        {
            if (iterator->nextChunkIndex < iterator->pendingChunks.size())
            {
                if (tileChunkUploadsThisFrame >= kMaxTileChunkUploadsPerFrame)
                {
                    break;
                }
                if (iterator->terrainIndex >= 0 &&
                    iterator->terrainIndex < static_cast<int>(m_TerrainDatasets.size()))
                {
                    TerrainDataset& dataset =
                        m_TerrainDatasets[static_cast<size_t>(iterator->terrainIndex)];
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
                    m_Diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                    ++m_Diagnostics.meshUploadsThisFrame;
                    ++m_Diagnostics.tileChunkUploadsThisFrame;
                    dataset.chunks.push_back(std::move(chunk));
                }
                ++iterator->nextChunkIndex;
                ++tileChunkUploadsThisFrame;
                ++iterator;
                continue;
            }

            // All chunks uploaded — mark loaded and run post-upload callbacks.
            if (iterator->terrainIndex >= 0 &&
                iterator->terrainIndex < static_cast<int>(m_TerrainDatasets.size()))
            {
                TerrainDataset& dataset =
                    m_TerrainDatasets[static_cast<size_t>(iterator->terrainIndex)];
                dataset.loaded = true;
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
                if (iterator->terrainIndex == m_ActiveTerrainIndex)
                {
                    LoadActiveTerrainIntoScene();
                }
                else
                {
                    RebuildAllTerrainProfileSamples();
                }
                m_StatusMessage = iterator->statusMessage;
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

        if (iterator->terrainIndex >= 0 && iterator->terrainIndex < static_cast<int>(m_TerrainDatasets.size()))
        {
            TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(iterator->terrainIndex)];
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
                m_Diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                ++m_Diagnostics.meshUploadsThisFrame;
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
                m_StatusMessage = result.statusMessage;
            }
        }
        else
        {
            m_StatusMessage = result.statusMessage.empty()
                ? "Background terrain job finished for a removed dataset."
                : result.statusMessage;
        }

        iterator = m_TerrainBuildJobs.erase(iterator);
    }

    for (auto iterator = m_AssetLoadJobs.begin(); iterator != m_AssetLoadJobs.end();)
    {
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        AssetLoadResult result;
        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background asset load failed: ") + exception.what();
        }

        if (iterator->assetIndex >= 0 && iterator->assetIndex < static_cast<int>(m_ImportedAssets.size()))
        {
            ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(iterator->assetIndex)];
            if (result.success)
            {
                asset.assetData = std::move(result.assetData);
                size_t totalVerts = 0, totalTris = 0;
                for (ImportedPrimitiveData& primitive : asset.assetData.primitives)
                {
                    const double uploadStartMs = NowMs();
                    primitive.mesh = std::make_unique<Mesh>(primitive.meshData);
                    m_Diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                    ++m_Diagnostics.meshUploadsThisFrame;
                    if (primitive.isSkinned && !primitive.skinMeshData.vertices.empty())
                    {
                        const double skinUploadStartMs = NowMs();
                        primitive.skinnedMesh = std::make_unique<AnimatedMesh>(primitive.skinMeshData);
                        m_Diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - skinUploadStartMs);
                        ++m_Diagnostics.meshUploadsThisFrame;
                    }
                    UploadImportedPrimitiveTextures(primitive);
                    totalVerts += primitive.meshData.vertices.size();
                    totalTris  += primitive.meshData.indices.size() / 3u;
                }
                // Reset animation state so it matches the new asset data.
                asset.animState = AnimationState{};
                asset.loaded = true;
                std::cout << "[GeoFPS] Asset '" << asset.name << "' loaded: "
                          << asset.assetData.primitives.size() << " primitives, "
                          << totalVerts << " vertices, " << totalTris << " triangles";
                if (asset.assetData.hasSkin)
                    std::cout << ", skinned (" << asset.assetData.skeleton.joints.size() << " joints, "
                              << asset.assetData.animations.size() << " clips)";
                if (asset.assetData.hasNodeAnimation)
                    std::cout << ", node-anim (" << asset.assetData.nodeAnimations.size() << " clips)";
                std::cout << '\n';

                // Compute AABB for raycast picking (only on background-loaded assets)
                asset.aabbMin   = glm::vec3( std::numeric_limits<float>::max());
                asset.aabbMax   = glm::vec3(-std::numeric_limits<float>::max());
                asset.aabbValid = false;
                for (const ImportedPrimitiveData& prim : asset.assetData.primitives)
                {
                    for (const Vertex& v : prim.meshData.vertices)
                    {
                        asset.aabbMin = glm::min(asset.aabbMin, v.position);
                        asset.aabbMax = glm::max(asset.aabbMax, v.position);
                    }
                }
                if (asset.aabbMin.x <= asset.aabbMax.x)
                {
                    asset.aabbMin  *= asset.scale;
                    asset.aabbMax  *= asset.scale;
                    asset.aabbValid = true;
                }

                m_StatusMessage = result.statusMessage;
            }
            else
            {
                std::cerr << "[GeoFPS] Asset '" << asset.name << "' FAILED: " << result.statusMessage << '\n';
                asset.loaded = false;
                asset.assetData.primitives.clear();
                m_StatusMessage = result.statusMessage;
            }
        }
        else
        {
            m_StatusMessage = result.statusMessage.empty() ? "Background asset job finished for a removed asset." :
                                                             result.statusMessage;
        }

        iterator = m_AssetLoadJobs.erase(iterator);
    }

    for (auto iterator = m_ProfileSampleBuildJobs.begin(); iterator != m_ProfileSampleBuildJobs.end();)
    {
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        ProfileSampleBuildResult result;
        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background profile sampling failed: ") + exception.what();
        }

        if (result.success)
        {
            size_t totalSamples = 0;
            for (const auto& p : result.profiles) totalSamples += p.samples.size();
            std::cout << "[GeoFPS] Profile samples rebuilt: " << result.profiles.size()
                      << " profiles, " << totalSamples << " total samples\n";
            m_TerrainProfiles = std::move(result.profiles);
            m_ActiveTerrainProfileIndex = m_TerrainProfiles.empty() ?
                                              -1 :
                                              std::clamp(m_ActiveTerrainProfileIndex,
                                                         0,
                                                         static_cast<int>(m_TerrainProfiles.size()) - 1);
        }
        m_StatusMessage = result.statusMessage;
        iterator = m_ProfileSampleBuildJobs.erase(iterator);
    }
}

bool Application::ActivateTerrainDataset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_TerrainDatasets.size()))
    {
        return false;
    }

    m_ActiveTerrainIndex = index;
    TerrainDataset& dataset = m_TerrainDatasets[static_cast<size_t>(index)];
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
    if (index < 0 || index >= static_cast<int>(m_TerrainDatasets.size()) || m_TerrainDatasets.size() <= 1)
    {
        return false;
    }

    m_TerrainDatasets.erase(m_TerrainDatasets.begin() + index);
    if (m_ActiveTerrainIndex >= static_cast<int>(m_TerrainDatasets.size()))
    {
        m_ActiveTerrainIndex = static_cast<int>(m_TerrainDatasets.size()) - 1;
    }

    m_StatusMessage = "Deleted terrain dataset.";
    return ActivateTerrainDataset(m_ActiveTerrainIndex);
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

bool Application::LoadImportedAsset(ImportedAsset& asset)
{
    if (asset.path.empty())
    {
        asset.loaded = false;
        asset.assetData.primitives.clear();
        m_StatusMessage = "Asset path is empty.";
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(asset.path, errorCode))
    {
        asset.loaded = false;
        asset.assetData.primitives.clear();
        m_StatusMessage = "Asset file does not exist: " + asset.path;
        return false;
    }

    asset.assetData.primitives.clear();

    const size_t extensionOffset = asset.path.find_last_of('.');
    const std::string extension = extensionOffset == std::string::npos ? std::string() : ToLower(asset.path.substr(extensionOffset));
    std::string errorMessage;

    if (extension == ".glb" || extension == ".gltf")
    {
        if (!GltfImporter::Load(asset.path, asset.assetData, errorMessage))
        {
            asset.loaded = false;
            m_StatusMessage = "Failed to load asset: " + (errorMessage.empty() ? asset.path : errorMessage);
            return false;
        }
    }
    else if (extension == ".obj")
    {
        ImportedPrimitiveData primitiveData;
        if (!ObjImporter::Load(asset.path, primitiveData.meshData, errorMessage))
        {
            asset.loaded = false;
            m_StatusMessage = "Failed to load asset: " + (errorMessage.empty() ? asset.path : errorMessage);
            return false;
        }

        primitiveData.materialName = "OBJ Material";
        primitiveData.baseColorFactor = glm::vec4(0.82f, 0.74f, 0.66f, 1.0f);
        asset.assetData.primitives.push_back(std::move(primitiveData));
    }
    else
    {
        asset.loaded = false;
        m_StatusMessage = "Unsupported asset format. Use .glb, .gltf, or .obj.";
        return false;
    }

    for (ImportedPrimitiveData& primitive : asset.assetData.primitives)
    {
        primitive.mesh = std::make_unique<Mesh>(primitive.meshData);
        if (primitive.isSkinned && !primitive.skinMeshData.vertices.empty())
            primitive.skinnedMesh = std::make_unique<AnimatedMesh>(primitive.skinMeshData);
        UploadImportedPrimitiveTextures(primitive);
    }
    asset.animState = AnimationState{};

    // ── Compute AABB for raycast picking ──────────────────────────────────────
    asset.aabbMin   = glm::vec3( std::numeric_limits<float>::max());
    asset.aabbMax   = glm::vec3(-std::numeric_limits<float>::max());
    asset.aabbValid = false;
    for (const ImportedPrimitiveData& prim : asset.assetData.primitives)
    {
        for (const Vertex& v : prim.meshData.vertices)
        {
            asset.aabbMin = glm::min(asset.aabbMin, v.position);
            asset.aabbMax = glm::max(asset.aabbMax, v.position);
        }
    }
    if (asset.aabbMin.x <= asset.aabbMax.x)
    {
        // Scale AABB to world units (asset.scale applied at render time, so match it)
        asset.aabbMin *= asset.scale;
        asset.aabbMax *= asset.scale;
        asset.aabbValid = true;
    }

    // Count stats for the terminal log
    size_t totalVerts = 0, totalTris = 0;
    for (const ImportedPrimitiveData& prim : asset.assetData.primitives)
    {
        totalVerts += prim.meshData.vertices.size();
        totalTris  += prim.meshData.indices.size() / 3u;
    }
    std::cout << "[GeoFPS] Asset '" << asset.name << "' loaded (sync): "
              << asset.assetData.primitives.size() << " primitives, "
              << totalVerts << " vertices, " << totalTris << " triangles\n";

    asset.loaded = true;
    m_StatusMessage = "Loaded asset: " + asset.name;
    return true;
}

bool Application::StartImportedAssetLoadJob(int assetIndex)
{
    if (assetIndex < 0 || assetIndex >= static_cast<int>(m_ImportedAssets.size()) || !m_BackgroundJobs)
    {
        return false;
    }

    for (const AssetLoadJob& job : m_AssetLoadJobs)
    {
        if (job.assetIndex == assetIndex)
        {
            m_StatusMessage = "Asset import already running: " + m_ImportedAssets[static_cast<size_t>(assetIndex)].name;
            return false;
        }
    }

    ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(assetIndex)];
    const std::string path = asset.path;
    asset.loaded = false;
    asset.assetData.primitives.clear();

    AssetLoadJob job;
    job.assetIndex = assetIndex;
    job.future = m_BackgroundJobs->Enqueue([path]() { return LoadAssetOnWorker(path); });
    m_AssetLoadJobs.push_back(std::move(job));
    m_StatusMessage = "Queued background asset import: " + asset.name;
    return true;
}

bool Application::DeleteImportedAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_ImportedAssets.size()) || m_ImportedAssets.size() <= 1)
    {
        return false;
    }

    m_ImportedAssets.erase(m_ImportedAssets.begin() + index);
    if (m_ActiveImportedAssetIndex >= static_cast<int>(m_ImportedAssets.size()))
    {
        m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
    }

    m_StatusMessage = "Deleted imported asset.";
    return true;
}

size_t Application::DeleteSelectedImportedAssets()
{
    if (m_ImportedAssets.size() <= 1)
    {
        m_StatusMessage = "At least one asset slot must remain.";
        return 0;
    }

    const size_t originalCount = m_ImportedAssets.size();
    m_ImportedAssets.erase(std::remove_if(m_ImportedAssets.begin(),
                                          m_ImportedAssets.end(),
                                          [](const ImportedAsset& asset) { return asset.selected; }),
                           m_ImportedAssets.end());

    if (m_ImportedAssets.empty())
    {
        ImportedAsset asset;
        asset.name = "Asset 1";
        m_ImportedAssets.push_back(std::move(asset));
    }

    const size_t deletedCount = originalCount - m_ImportedAssets.size();
    m_ActiveImportedAssetIndex =
        std::clamp(m_ActiveImportedAssetIndex, 0, static_cast<int>(m_ImportedAssets.size()) - 1);
    m_StatusMessage = deletedCount == 0 ? "No selected assets to delete." :
                                          "Deleted " + std::to_string(deletedCount) + " selected asset(s).";
    return deletedCount;
}

void Application::SelectAllImportedAssets(bool selected)
{
    for (ImportedAsset& asset : m_ImportedAssets)
    {
        asset.selected = selected;
    }
    m_StatusMessage = selected ? "Selected all imported assets." : "Cleared imported asset selection.";
}

size_t Application::SnapSelectedImportedAssetsToTerrain()
{
    size_t snappedCount = 0;
    for (ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.selected)
        {
            continue;
        }

        if (asset.useGeographicPlacement)
        {
            asset.height = SampleTerrainHeightAt(asset.latitude, asset.longitude);
            UpdateImportedAssetPositionFromGeographic(asset);
            ++snappedCount;
            continue;
        }

        const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
        if (activeTerrain == nullptr || !activeTerrain->loaded)
        {
            continue;
        }
        const glm::dvec3 terrainCoordinate = LocalToTerrainCoordinate(
            *activeTerrain,
            {static_cast<double>(asset.position.x), static_cast<double>(asset.position.y), static_cast<double>(asset.position.z)});
        asset.position.y = SampleRenderedTerrainLocalHeightAt(*activeTerrain, terrainCoordinate.x, terrainCoordinate.y);
        ++snappedCount;
    }

    m_StatusMessage = snappedCount == 0 ? "No selected assets to snap." :
                                         "Snapped " + std::to_string(snappedCount) + " selected asset(s) to terrain.";
    return snappedCount;
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
    m_ProfileMapViewInitialized = false;
    MarkTerrainIsolineSampleGridDirty();
    RebuildAllTerrainProfileSamples();
}

void Application::RebuildTerrainProfileSamples(TerrainProfile& profile)
{
    const TerrainDataset* dataset = GetPrimaryTerrainForProfile(profile);
    if (dataset == nullptr)
    {
        profile.samples.clear();
        return;
    }

    if (dataset->hasTileManifest)
    {
        profile.samples = BuildTerrainProfileSamplesForDataset(profile, *dataset);
        return;
    }

    if (!dataset->heightGrid.IsValid())
    {
        profile.samples.clear();
        return;
    }

    GeoConverter converter(dataset->geoReference);
    profile.samples = SampleTerrainProfile(profile.vertices,
                                           dataset->heightGrid,
                                           converter,
                                           profile.sampleSpacingMeters,
                                           profile.useLocalCoordinates,
                                           dataset->settings.coordinateMode);
}

std::vector<TerrainProfileSample> Application::BuildTerrainProfileSamplesForDataset(const TerrainProfile& profile,
                                                                                   const TerrainDataset& dataset) const
{
    std::vector<TerrainProfileSample> samples;
    if (profile.vertices.size() < 2 || !dataset.bounds.valid)
    {
        return samples;
    }

    const double sampleSpacingMeters = std::max(static_cast<double>(profile.sampleSpacingMeters), 0.1);
    double accumulatedDistance = 0.0;

    for (size_t vertexIndex = 0; vertexIndex + 1 < profile.vertices.size(); ++vertexIndex)
    {
        const TerrainProfileVertex& start = profile.vertices[vertexIndex];
        const TerrainProfileVertex& end = profile.vertices[vertexIndex + 1];
        const TerrainProfileVertex startCoordinate = ProfileVertexToDatasetCoordinate(profile, start, dataset);
        const TerrainProfileVertex endCoordinate = ProfileVertexToDatasetCoordinate(profile, end, dataset);
        const glm::dvec3 startLocal =
            TerrainCoordinateToLocal(dataset, startCoordinate.latitude, startCoordinate.longitude, dataset.geoReference.originHeight);
        const glm::dvec3 endLocal =
            TerrainCoordinateToLocal(dataset, endCoordinate.latitude, endCoordinate.longitude, dataset.geoReference.originHeight);
        const double segmentLength = glm::length(glm::dvec2(endLocal.x - startLocal.x, endLocal.z - startLocal.z));
        const double dx = endLocal.x - startLocal.x;
        const double dz = endLocal.z - startLocal.z;
        double lineAngleDegrees = (std::abs(dx) <= 1e-10 && std::abs(dz) <= 1e-10) ?
                                      0.0 :
                                      std::atan2(dz, dx) * (180.0 / 3.14159265358979323846);
        if (lineAngleDegrees < 0.0)
        {
            lineAngleDegrees += 360.0;
        }

        const int stepCount = std::max(1, static_cast<int>(std::ceil(segmentLength / sampleSpacingMeters)));
        const int firstStep = samples.empty() ? 0 : 1;

        for (int step = firstStep; step <= stepCount; ++step)
        {
            const double t = static_cast<double>(step) / static_cast<double>(stepCount);
            const double latitude =
                startCoordinate.latitude + ((endCoordinate.latitude - startCoordinate.latitude) * t);
            const double longitude =
                startCoordinate.longitude + ((endCoordinate.longitude - startCoordinate.longitude) * t);
            const bool valid = TerrainDatasetContainsCoordinate(dataset, latitude, longitude);
            const double height = valid ? static_cast<double>(SampleTerrainHeightAt(dataset, latitude, longitude)) : 0.0;

            TerrainProfileSample sample;
            sample.distanceMeters = accumulatedDistance + (segmentLength * t);
            sample.latitude = latitude;
            sample.longitude = longitude;
            sample.height = height;
            sample.lineAngleDegrees = lineAngleDegrees;
            sample.localPosition = TerrainCoordinateToLocal(dataset, latitude, longitude, height);
            sample.valid = valid;
            samples.push_back(sample);
        }

        accumulatedDistance += segmentLength;
    }

    return samples;
}

void Application::RebuildAllTerrainProfileSamplesNow()
{
    for (TerrainProfile& profile : m_TerrainProfiles)
    {
        RebuildTerrainProfileSamples(profile);
    }
}

bool Application::StartTerrainProfileSampleJob()
{
    if (!m_BackgroundJobs)
    {
        return false;
    }

    if (!m_ProfileSampleBuildJobs.empty())
    {
        m_StatusMessage = "Terrain profile sampling already running.";
        return false;
    }

    std::vector<ProfileSamplingTerrainSnapshot> terrainSnapshots;
    terrainSnapshots.reserve(m_TerrainDatasets.size());
    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        ProfileSamplingTerrainSnapshot snapshot;
        snapshot.name = dataset.name;
        snapshot.geoReference = dataset.geoReference;
        snapshot.coordinateMode = dataset.settings.coordinateMode;
        snapshot.heightGrid = dataset.heightGrid;
        snapshot.loaded = dataset.loaded;
        terrainSnapshots.push_back(std::move(snapshot));
    }

    ProfileSampleBuildJob job;
    job.future = m_BackgroundJobs->Enqueue([profiles = m_TerrainProfiles,
                                            terrains = std::move(terrainSnapshots),
                                            activeTerrainIndex = m_ActiveTerrainIndex]() mutable {
        return BuildProfileSamplesOnWorker(std::move(profiles), std::move(terrains), activeTerrainIndex);
    });
    m_ProfileSampleBuildJobs.push_back(std::move(job));
    m_StatusMessage = "Queued background terrain profile sampling.";
    return true;
}

void Application::RebuildAllTerrainProfileSamples()
{
    if (!m_ProfileSampleBuildJobs.empty())
    {
        m_StatusMessage = "Terrain profile sampling already running.";
        return;
    }

    const bool hasTiledTerrain = std::any_of(m_TerrainDatasets.begin(), m_TerrainDatasets.end(), [](const TerrainDataset& dataset) {
        return dataset.loaded && dataset.hasTileManifest;
    });
    if (hasTiledTerrain)
    {
        RebuildAllTerrainProfileSamplesNow();
        return;
    }

    if (!StartTerrainProfileSampleJob())
    {
        RebuildAllTerrainProfileSamplesNow();
    }
}

void Application::MarkTerrainIsolinesDirty()
{
    m_TerrainIsolinesDirty = true;
}

void Application::MarkTerrainIsolineSampleGridDirty()
{
    m_TerrainIsolineSampleGridDirty = true;
    MarkTerrainIsolinesDirty();
}

void Application::RebuildTerrainIsolineSampleGridIfNeeded()
{
    if (!m_TerrainIsolineSampleGridDirty)
    {
        return;
    }

    const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    if (activeTerrain != nullptr && activeTerrain->loaded && activeTerrain->bounds.valid)
    {
        if (!activeTerrain->hasTileManifest && activeTerrain->heightGrid.IsValid())
        {
            m_TerrainIsolineSampleGrid = BuildTerrainIsolineSampleGrid(activeTerrain->heightGrid, m_IsolineSettings);
            m_TerrainIsolineSampleGridDirty = false;
            return;
        }

        // For tiled terrain, SampleTerrainHeightAt() scans all tiles linearly —
        // an O(resX × resZ × numTiles) operation.  While tiles are still streaming
        // in, every tile completion marks the grid dirty again anyway, so there is
        // no value in rebuilding mid-stream.  Defer until the last tile settles.
        if (!m_TerrainTileBuildJobs.empty())
        {
            return;
        }

        TerrainIsolineSampleGrid sampleGrid;
        sampleGrid.resolutionX = std::clamp(m_IsolineSettings.resolutionX, 2, 512);
        sampleGrid.resolutionZ = std::clamp(m_IsolineSettings.resolutionZ, 2, 512);
        sampleGrid.minLatitude = activeTerrain->bounds.minLatitude;
        sampleGrid.maxLatitude = activeTerrain->bounds.maxLatitude;
        sampleGrid.minLongitude = activeTerrain->bounds.minLongitude;
        sampleGrid.maxLongitude = activeTerrain->bounds.maxLongitude;
        sampleGrid.heights.assign(static_cast<size_t>(sampleGrid.resolutionX * sampleGrid.resolutionZ), 0.0f);
        sampleGrid.minHeight = std::numeric_limits<double>::max();
        sampleGrid.maxHeight = std::numeric_limits<double>::lowest();

        for (int z = 0; z < sampleGrid.resolutionZ; ++z)
        {
            const double v = static_cast<double>(z) / static_cast<double>(sampleGrid.resolutionZ - 1);
            const double latitude =
                sampleGrid.minLatitude + ((sampleGrid.maxLatitude - sampleGrid.minLatitude) * v);
            for (int x = 0; x < sampleGrid.resolutionX; ++x)
            {
                const double u = static_cast<double>(x) / static_cast<double>(sampleGrid.resolutionX - 1);
                const double longitude =
                    sampleGrid.minLongitude + ((sampleGrid.maxLongitude - sampleGrid.minLongitude) * u);
                const double height = static_cast<double>(SampleTerrainHeightAt(*activeTerrain, latitude, longitude));
                sampleGrid.heights[static_cast<size_t>(z * sampleGrid.resolutionX + x)] = static_cast<float>(height);
                sampleGrid.minHeight = std::min(sampleGrid.minHeight, height);
                sampleGrid.maxHeight = std::max(sampleGrid.maxHeight, height);
            }
        }

        if (sampleGrid.minHeight == std::numeric_limits<double>::max())
        {
            sampleGrid.minHeight = activeTerrain->bounds.minHeight;
            sampleGrid.maxHeight = activeTerrain->bounds.maxHeight;
        }

        m_TerrainIsolineSampleGrid = std::move(sampleGrid);
    }
    else
    {
        m_TerrainIsolineSampleGrid = BuildTerrainIsolineSampleGrid(m_TerrainHeightGrid, m_IsolineSettings);
    }
    m_TerrainIsolineSampleGridDirty = false;
}

void Application::RebuildTerrainIsolines()
{
    RebuildTerrainIsolineSampleGridIfNeeded();
    if (!m_TerrainIsolineSampleGrid.IsValid())
    {
        m_TerrainIsolines.clear();
        m_TerrainIsolinesUsedGpu = false;
        m_TerrainIsolinesDirty = false;
        return;
    }

    m_TerrainIsolines = GenerateTerrainIsolinesAccelerated(m_TerrainIsolineSampleGrid,
                                                           m_IsolineSettings,
                                                           m_UseGpuIsolineGeneration,
                                                           &m_TerrainIsolinesUsedGpu);
    m_TerrainIsolinesDirty = false;
}

void Application::RebuildTerrainIsolinesIfNeeded()
{
    if (!m_TerrainIsolinesDirty)
    {
        return;
    }

    RebuildTerrainIsolines();
}

size_t Application::CountSceneTriangles() const
{
    size_t triangleCount = 0;
    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        if (!dataset.visible || !dataset.loaded)
        {
            continue;
        }
        if (dataset.hasTileManifest)
        {
            for (const TerrainTile& tile : dataset.tiles)
            {
                for (const TerrainMeshChunk& chunk : tile.chunks)
                {
                    if (chunk.mesh)
                    {
                        triangleCount += chunk.mesh->GetTriangleCount();
                    }
                }
            }
        }
        else if (dataset.mesh)
        {
            triangleCount += dataset.mesh->GetTriangleCount();
        }
    }

    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.loaded)
        {
            continue;
        }
        for (const ImportedPrimitiveData& primitive : asset.assetData.primitives)
        {
            if (primitive.mesh)
            {
                triangleCount += primitive.mesh->GetTriangleCount();
            }
        }
    }

    return triangleCount;
}

float Application::SampleTerrainHeightAt(double latitude, double longitude) const
{
    if (!m_TerrainHeightGrid.IsValid())
    {
        return static_cast<float>(m_GeoReference.originHeight);
    }

    return static_cast<float>(m_TerrainHeightGrid.SampleHeight(latitude, longitude));
}

float Application::SampleTerrainHeightAt(const TerrainDataset& dataset, double latitude, double longitude) const
{
    if (dataset.hasTileManifest)
    {
        for (const TerrainTile& tile : dataset.tiles)
        {
            if (!tile.bounds.valid)
            {
                continue;
            }
            if (latitude >= tile.bounds.minLatitude && latitude <= tile.bounds.maxLatitude &&
                longitude >= tile.bounds.minLongitude && longitude <= tile.bounds.maxLongitude)
            {
                if (tile.loaded && tile.heightGrid.IsValid())
                {
                    return static_cast<float>(tile.heightGrid.SampleHeight(latitude, longitude));
                }
                return static_cast<float>(0.5 * (tile.bounds.minHeight + tile.bounds.maxHeight));
            }
        }
        return static_cast<float>(dataset.geoReference.originHeight);
    }

    if (!dataset.heightGrid.IsValid())
    {
        return static_cast<float>(dataset.geoReference.originHeight);
    }

    return static_cast<float>(dataset.heightGrid.SampleHeight(latitude, longitude));
}

float Application::SampleRenderedTerrainLocalHeightAt(const TerrainDataset& dataset, double latitude, double longitude) const
{
    const MeshData& meshData = dataset.terrainMeshData;
    const int resolutionX = dataset.settings.gridResolutionX;
    const int resolutionZ = dataset.settings.gridResolutionZ;
    if (resolutionX < 2 || resolutionZ < 2 ||
        meshData.vertices.size() != static_cast<size_t>(resolutionX * resolutionZ))
    {
        const double terrainHeight = static_cast<double>(SampleTerrainHeightAt(dataset, latitude, longitude));
        return RawTerrainHeightToRenderedLocalHeight(dataset, terrainHeight);
    }

    const glm::dvec3 local = TerrainCoordinateToLocal(dataset, latitude, longitude, dataset.geoReference.originHeight);
    const Vertex& bottomLeft = meshData.vertices.front();
    const Vertex& topRight = meshData.vertices.back();
    const float minX = std::min(bottomLeft.position.x, topRight.position.x);
    const float maxX = std::max(bottomLeft.position.x, topRight.position.x);
    const float minZ = std::min(bottomLeft.position.z, topRight.position.z);
    const float maxZ = std::max(bottomLeft.position.z, topRight.position.z);
    const float xSpan = std::max(maxX - minX, 0.0001f);
    const float zSpan = std::max(maxZ - minZ, 0.0001f);
    const float u = std::clamp((static_cast<float>(local.x) - minX) / xSpan, 0.0f, 1.0f);
    const float v = std::clamp((static_cast<float>(local.z) - minZ) / zSpan, 0.0f, 1.0f);
    const float gridX = u * static_cast<float>(resolutionX - 1);
    const float gridZ = v * static_cast<float>(resolutionZ - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(gridX)), 0, resolutionX - 1);
    const int z0 = std::clamp(static_cast<int>(std::floor(gridZ)), 0, resolutionZ - 1);
    const int x1 = std::min(x0 + 1, resolutionX - 1);
    const int z1 = std::min(z0 + 1, resolutionZ - 1);
    const float tx = gridX - static_cast<float>(x0);
    const float tz = gridZ - static_cast<float>(z0);

    const auto vertexHeight = [&](int x, int z) {
        return meshData.vertices[static_cast<size_t>(z * resolutionX + x)].position.y;
    };

    const float h00 = vertexHeight(x0, z0);
    const float h10 = vertexHeight(x1, z0);
    const float h01 = vertexHeight(x0, z1);
    const float h11 = vertexHeight(x1, z1);
    const float lower = h00 + ((h10 - h00) * tx);
    const float upper = h01 + ((h11 - h01) * tx);
    return lower + ((upper - lower) * tz);
}

bool Application::TerrainProfileIncludesTerrain(const TerrainProfile& profile, const TerrainDataset& dataset) const
{
    if (profile.includedTerrainNames.empty())
    {
        const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
        return activeTerrain == nullptr || &dataset == activeTerrain;
    }

    return std::find(profile.includedTerrainNames.begin(), profile.includedTerrainNames.end(), dataset.name) !=
           profile.includedTerrainNames.end();
}

void Application::SetTerrainProfileIncludesTerrain(TerrainProfile& profile, const TerrainDataset& dataset, bool included)
{
    auto iterator = std::find(profile.includedTerrainNames.begin(), profile.includedTerrainNames.end(), dataset.name);
    if (included && iterator == profile.includedTerrainNames.end())
    {
        profile.includedTerrainNames.push_back(dataset.name);
    }
    else if (!included && iterator != profile.includedTerrainNames.end())
    {
        profile.includedTerrainNames.erase(iterator);
    }
}

void Application::EnsureTerrainProfileHasTerrainSelection(TerrainProfile& profile)
{
    if (!profile.includedTerrainNames.empty())
    {
        return;
    }

    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        if (dataset.visible)
        {
            profile.includedTerrainNames.push_back(dataset.name);
        }
    }

    if (profile.includedTerrainNames.empty())
    {
        const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
        if (activeTerrain != nullptr)
        {
            profile.includedTerrainNames.push_back(activeTerrain->name);
        }
    }
}

const TerrainDataset* Application::GetPrimaryTerrainForProfile(const TerrainProfile& profile) const
{
    for (const TerrainDataset& dataset : m_TerrainDatasets)
    {
        if (dataset.loaded && TerrainProfileIncludesTerrain(profile, dataset))
        {
            return &dataset;
        }
    }

    const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    if (activeTerrain != nullptr && activeTerrain->loaded)
    {
        return activeTerrain;
    }

    return nullptr;
}

void Application::UpdateImportedAssetPositionFromGeographic(ImportedAsset& asset) const
{
    if (!asset.useGeographicPlacement)
    {
        return;
    }

    GeoConverter converter(m_GeoReference);
    const glm::dvec3 localPosition = converter.ToLocal(asset.latitude, asset.longitude, asset.height);
    asset.position = glm::vec3(static_cast<float>(localPosition.x),
                               static_cast<float>(localPosition.y),
                               static_cast<float>(localPosition.z));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Navigate camera to active imported asset
// ─────────────────────────────────────────────────────────────────────────────
void Application::GoToActiveAsset()
{
    const ImportedAsset* asset = GetActiveImportedAsset();
    if (asset == nullptr || !asset->loaded)
    {
        m_StatusMessage = "No loaded asset to navigate to.";
        return;
    }

    // Determine a comfortable view distance from the AABB extents
    float viewDist  = 20.0f;
    float halfH     = 3.0f;
    if (asset->aabbValid)
    {
        const glm::vec3 extent = (asset->aabbMax - asset->aabbMin) * 0.5f;
        viewDist = std::max(glm::length(extent) * 2.5f, 5.0f);
        halfH    = extent.y;
    }

    // Place camera to the "north" side (–Z) and slightly above the asset centre,
    // then compute the yaw/pitch so it looks at the asset.
    const glm::vec3 assetCentre = asset->position + glm::vec3(0.0f, halfH, 0.0f);
    const glm::vec3 camPos      = assetCentre + glm::vec3(0.0f, halfH * 0.4f + 2.0f, -viewDist);

    // Direction from camera → asset centre
    const glm::vec3 dir   = glm::normalize(assetCentre - camPos);
    // Yaw: atan2(x, -z) for FPS convention where yaw=0 looks in –Z
    const float yaw   = glm::degrees(std::atan2(dir.x, -dir.z));
    const float pitch = glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f)));

    QueueCameraTeleport(camPos);
    SnapCameraView(yaw, pitch);

    m_StatusMessage = "Camera moved to: " + asset->name;
    std::cout << "[GeoFPS] Camera teleported to asset '" << asset->name
              << "' at (" << asset->position.x << ", " << asset->position.y
              << ", " << asset->position.z << "), view dist=" << viewDist << " m\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Gizmo snap helper
// ─────────────────────────────────────────────────────────────────────────────
void Application::SnapCameraView(float yaw, float pitch)
{
    m_PendingCameraCommand.hasSnapTarget = true;
    m_PendingCameraCommand.snapTargetYaw = yaw;
    m_PendingCameraCommand.snapTargetPitch = std::clamp(pitch, -89.0f, 89.0f);
    m_FPSController.ResetMouseState();
}

void Application::QueueCameraTeleport(const glm::vec3& position)
{
    m_PendingCameraCommand.hasTeleport = true;
    m_PendingCameraCommand.teleportPosition = position;
    m_FPSController.ResetMouseState();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature: Shared terrain height helper
// ─────────────────────────────────────────────────────────────────────────────
float Application::GetTerrainLocalHeightAt(float x, float z) const
{
    const TerrainDataset* terrain = GetActiveTerrainDataset();
    if (!terrain) return 0.0f;
    const GeoConverter converter(m_GeoReference);
    const glm::dvec3 geo = converter.ToGeographic({static_cast<double>(x), 0.0, static_cast<double>(z)});
    return SampleRenderedTerrainLocalHeightAt(*terrain, geo.x, geo.y);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature: Raycast asset picking
// ─────────────────────────────────────────────────────────────────────────────
void Application::PickAssetAtScreenPos(float pixelX, float pixelY)
{
    const int W = m_Window.GetWidth();
    const int H = m_Window.GetHeight();
    if (W <= 0 || H <= 0) return;

    // Unproject screen pixel → world-space ray
    const glm::mat4 invProj = glm::inverse(m_Camera.GetProjectionMatrix());
    const glm::mat4 invView = glm::inverse(m_Camera.GetViewMatrix());

    const float ndcX =  (2.0f * pixelX / static_cast<float>(W)) - 1.0f;
    const float ndcY =  1.0f - (2.0f * pixelY / static_cast<float>(H));
    const glm::vec4 rayClip  = {ndcX, ndcY, -1.0f, 1.0f};
    glm::vec4 rayEye         = invProj * rayClip;
    rayEye.z = -1.0f; rayEye.w = 0.0f;
    const glm::vec3 rayDir   = glm::normalize(glm::vec3(invView * rayEye));
    const glm::vec3 rayOrigin = m_Camera.GetPosition();

    float bestT   = std::numeric_limits<float>::max();
    int   bestIdx = -1;

    for (int i = 0; i < static_cast<int>(m_ImportedAssets.size()); ++i)
    {
        const ImportedAsset& asset = m_ImportedAssets[static_cast<size_t>(i)];
        if (!asset.loaded || !asset.aabbValid) continue;

        // Transform AABB to world space (scale already baked; just add position)
        const glm::vec3 worldMin = asset.position + asset.aabbMin;
        const glm::vec3 worldMax = asset.position + asset.aabbMax;

        // Slab-method ray/AABB intersection
        glm::vec3 tMin = (worldMin - rayOrigin) / (rayDir + glm::vec3(1e-12f));
        glm::vec3 tMax = (worldMax - rayOrigin) / (rayDir + glm::vec3(1e-12f));
        glm::vec3 t1 = glm::min(tMin, tMax);
        glm::vec3 t2 = glm::max(tMin, tMax);
        const float tNear = std::max({t1.x, t1.y, t1.z});
        const float tFar  = std::min({t2.x, t2.y, t2.z});
        if (tNear > tFar || tFar < 0.0f) continue;
        const float t = tNear >= 0.0f ? tNear : tFar;
        if (t > 0.0f && t < bestT)
        {
            bestT   = t;
            bestIdx = i;
        }
    }

    if (bestIdx >= 0)
    {
        for (ImportedAsset& a : m_ImportedAssets) a.selected = false;
        m_ImportedAssets[static_cast<size_t>(bestIdx)].selected = true;
        m_ActiveImportedAssetIndex = bestIdx;
        m_StatusMessage = "Selected: " + m_ImportedAssets[static_cast<size_t>(bestIdx)].name;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature: In-world asset label rendering
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderAssetLabels()
{
    if (!m_AssetLabelSettings.visible) return;
    if (m_ImportedAssets.empty()) return;

    const glm::mat4 vp      = GetRenderViewProjectionMatrix();
    const float W = static_cast<float>(m_Window.GetWidth());
    const float H = static_cast<float>(m_Window.GetHeight());
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.loaded || !asset.showLabel || asset.name.empty()) continue;

        const glm::vec3 wp = ToRenderRelative(asset.position +
                                              glm::vec3(0.0f, m_AssetLabelSettings.verticalOffsetMeters, 0.0f));
        const float dist   = glm::length(wp);
        if (dist > m_AssetLabelSettings.maxDistanceMeters) continue;

        const glm::vec4 clip = vp * glm::vec4(wp, 1.0f);
        if (clip.w <= 0.0f) continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) continue;

        const float sx = (ndc.x * 0.5f + 0.5f) * W;
        const float sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * H;
        const float alpha = 1.0f - std::clamp(dist / m_AssetLabelSettings.maxDistanceMeters, 0.0f, 1.0f);
        const ImU32 shadow = IM_COL32(0,   0,   0,   static_cast<int>(alpha * 160.0f));
        const ImU32 text   = IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f));

        dl->AddText(ImVec2(sx + 1.0f, sy + 1.0f), shadow, asset.name.c_str());
        dl->AddText(ImVec2(sx,         sy        ), text,   asset.name.c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature: Terrain profile CSV / KML export
// ─────────────────────────────────────────────────────────────────────────────
bool Application::ExportActiveProfileAsCsv(const std::string& path)
{
    if (m_ActiveTerrainProfileIndex < 0 ||
        m_ActiveTerrainProfileIndex >= static_cast<int>(m_TerrainProfiles.size()))
    {
        m_StatusMessage = "No active profile to export.";
        return false;
    }
    const TerrainProfile& profile = m_TerrainProfiles[static_cast<size_t>(m_ActiveTerrainProfileIndex)];
    if (profile.samples.empty())
    {
        m_StatusMessage = "Profile has no samples — rebuild it first.";
        return false;
    }
    std::ofstream file(path);
    if (!file)
    {
        m_StatusMessage = "Could not open file for writing: " + path;
        return false;
    }
    file << "latitude,longitude,height_m,distance_m\n";
    for (const TerrainProfileSample& s : profile.samples)
        file << s.latitude << ',' << s.longitude << ',' << s.height << ',' << s.distanceMeters << '\n';
    m_StatusMessage = "Exported profile CSV: " + path;
    return file.good();
}

bool Application::ExportActiveProfileAsKml(const std::string& path)
{
    if (m_ActiveTerrainProfileIndex < 0 ||
        m_ActiveTerrainProfileIndex >= static_cast<int>(m_TerrainProfiles.size()))
    {
        m_StatusMessage = "No active profile to export.";
        return false;
    }
    const TerrainProfile& profile = m_TerrainProfiles[static_cast<size_t>(m_ActiveTerrainProfileIndex)];
    if (profile.samples.empty())
    {
        m_StatusMessage = "Profile has no samples — rebuild it first.";
        return false;
    }
    std::ofstream file(path);
    if (!file)
    {
        m_StatusMessage = "Could not open file for writing: " + path;
        return false;
    }
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
         << "<Document>\n"
         << "  <name>" << profile.name << "</name>\n"
         << "  <Placemark>\n"
         << "    <name>" << profile.name << "</name>\n"
         << "    <LineString>\n"
         << "      <altitudeMode>absolute</altitudeMode>\n"
         << "      <coordinates>\n";
    for (const TerrainProfileSample& s : profile.samples)
        file << "        " << s.longitude << ',' << s.latitude << ',' << s.height << '\n';
    file << "      </coordinates>\n"
         << "    </LineString>\n"
         << "  </Placemark>\n"
         << "</Document>\n"
         << "</kml>\n";
    m_StatusMessage = "Exported profile KML: " + path;
    return file.good();
}


} // namespace GeoFPS
