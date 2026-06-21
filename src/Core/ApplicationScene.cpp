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
#include <utility>

namespace GeoFPS
{
using namespace ApplicationInternal;
namespace
{
float RawTerrainHeightToRenderedLocalHeight(const TerrainDataset& dataset, double terrainHeight)
{
    const double baseHeight = dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters ?
                                  terrainHeight :
                                  terrainHeight - dataset.geoReference.originHeight;
    return static_cast<float>(baseHeight * static_cast<double>(dataset.settings.heightScale));
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

AssetLoadResult LoadAssetOnWorker(std::string path)
{
    AssetLoadResult result;
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

ProfileSampleBuildResult BuildProfileSamplesOnWorker(
    std::vector<TerrainProfile> profiles,
    std::vector<ProfileSamplingTerrainSnapshot> terrains,
    int activeTerrainIndex)
{
    ProfileSampleBuildResult result;
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

} // namespace

TerrainDataset* Application::GetActiveTerrainDataset()
{
    if (m_Terrain.activeIndex() < 0 || m_Terrain.activeIndex() >= static_cast<int>(m_Terrain.datasets().size()))
    {
        return nullptr;
    }

    return &m_Terrain.datasets()[static_cast<size_t>(m_Terrain.activeIndex())];
}

const TerrainDataset* Application::GetActiveTerrainDataset() const
{
    if (m_Terrain.activeIndex() < 0 || m_Terrain.activeIndex() >= static_cast<int>(m_Terrain.datasets().size()))
    {
        return nullptr;
    }

    return &m_Terrain.datasets()[static_cast<size_t>(m_Terrain.activeIndex())];
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
    if (m_Assets.activeIndex() < 0 || m_Assets.activeIndex() >= static_cast<int>(m_Assets.assets().size()))
    {
        return nullptr;
    }

    return &m_Assets.assets()[static_cast<size_t>(m_Assets.activeIndex())];
}

const ImportedAsset* Application::GetActiveImportedAsset() const
{
    if (m_Assets.activeIndex() < 0 || m_Assets.activeIndex() >= static_cast<int>(m_Assets.assets().size()))
    {
        return nullptr;
    }

    return &m_Assets.assets()[static_cast<size_t>(m_Assets.activeIndex())];
}

size_t Application::GetSelectedImportedAssetCount() const
{
    return static_cast<size_t>(std::count_if(m_Assets.assets().begin(), m_Assets.assets().end(), [](const ImportedAsset& asset) {
        return asset.selected;
    }));
}

void Application::CopySelectedImportedAssets()
{
    m_Assets.clipboard().clear();
    for (const ImportedAsset& asset : m_Assets.assets())
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
        m_Assets.clipboard().push_back(entry);
    }

    m_StatusMessage = m_Assets.clipboard().empty() ? "No selected assets to copy." :
                                               "Copied " + std::to_string(m_Assets.clipboard().size()) + " asset(s).";
}

void Application::PasteCopiedImportedAssets()
{
    if (m_Assets.clipboard().empty())
    {
        m_StatusMessage = "Clipboard has no copied assets.";
        return;
    }

    for (ImportedAsset& asset : m_Assets.assets())
    {
        asset.selected = false;
    }

    for (size_t index = 0; index < m_Assets.clipboard().size(); ++index)
    {
        const AssetClipboardEntry& entry = m_Assets.clipboard()[index];
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
        const glm::vec3 offset = m_Assets.pasteOffset() * static_cast<float>(index + 1);
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
        m_Assets.assets().push_back(std::move(asset));
    }

    m_Assets.setActiveIndex(static_cast<int>(m_Assets.assets().size()) - 1);
    m_StatusMessage = "Pasted " + std::to_string(m_Assets.clipboard().size()) + " asset(s).";
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
    if (assetIndex < 0 || assetIndex >= static_cast<int>(m_Assets.assets().size()) || !m_BackgroundJobs)
    {
        return false;
    }

    for (const AssetLoadJob& job : m_Assets.loadJobs())
    {
        if (job.assetIndex == assetIndex)
        {
            m_StatusMessage = "Asset import already running: " + m_Assets.assets()[static_cast<size_t>(assetIndex)].name;
            return false;
        }
    }

    ImportedAsset& asset = m_Assets.assets()[static_cast<size_t>(assetIndex)];
    const std::string path = asset.path;
    asset.loaded = false;
    asset.assetData.primitives.clear();

    AssetLoadJob job;
    job.assetIndex = assetIndex;
    job.future = m_BackgroundJobs->Enqueue([path]() { return LoadAssetOnWorker(path); });
    m_Assets.loadJobs().push_back(std::move(job));
    m_StatusMessage = "Queued background asset import: " + asset.name;
    return true;
}

bool Application::DeleteImportedAsset(int index)
{
    if (index < 0 || index >= static_cast<int>(m_Assets.assets().size()) || m_Assets.assets().size() <= 1)
    {
        return false;
    }

    m_Assets.assets().erase(m_Assets.assets().begin() + index);
    if (m_Assets.activeIndex() >= static_cast<int>(m_Assets.assets().size()))
    {
        m_Assets.setActiveIndex(static_cast<int>(m_Assets.assets().size()) - 1);
    }

    m_StatusMessage = "Deleted imported asset.";
    return true;
}

size_t Application::DeleteSelectedImportedAssets()
{
    if (m_Assets.assets().size() <= 1)
    {
        m_StatusMessage = "At least one asset slot must remain.";
        return 0;
    }

    const size_t originalCount = m_Assets.assets().size();
    m_Assets.assets().erase(std::remove_if(m_Assets.assets().begin(),
                                          m_Assets.assets().end(),
                                          [](const ImportedAsset& asset) { return asset.selected; }),
                           m_Assets.assets().end());

    if (m_Assets.assets().empty())
    {
        ImportedAsset asset;
        asset.name = "Asset 1";
        m_Assets.assets().push_back(std::move(asset));
    }

    const size_t deletedCount = originalCount - m_Assets.assets().size();
    m_Assets.setActiveIndex(
        std::clamp(m_Assets.activeIndex(), 0, static_cast<int>(m_Assets.assets().size()) - 1));
    m_StatusMessage = deletedCount == 0 ? "No selected assets to delete." :
                                          "Deleted " + std::to_string(deletedCount) + " selected asset(s).";
    return deletedCount;
}

void Application::SelectAllImportedAssets(bool selected)
{
    for (ImportedAsset& asset : m_Assets.assets())
    {
        asset.selected = selected;
    }
    m_StatusMessage = selected ? "Selected all imported assets." : "Cleared imported asset selection.";
}

size_t Application::SnapSelectedImportedAssetsToTerrain()
{
    size_t snappedCount = 0;
    for (ImportedAsset& asset : m_Assets.assets())
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
    for (TerrainProfile& profile : m_Profiles.profiles())
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

    if (!m_Profiles.sampleBuildJobs().empty())
    {
        m_StatusMessage = "Terrain profile sampling already running.";
        return false;
    }

    std::vector<ProfileSamplingTerrainSnapshot> terrainSnapshots;
    terrainSnapshots.reserve(m_Terrain.datasets().size());
    for (const TerrainDataset& dataset : m_Terrain.datasets())
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
    job.future = m_BackgroundJobs->Enqueue([profiles = m_Profiles.profiles(),
                                            terrains = std::move(terrainSnapshots),
                                            activeTerrainIndex = m_Terrain.activeIndex()]() mutable {
        return BuildProfileSamplesOnWorker(std::move(profiles), std::move(terrains), activeTerrainIndex);
    });
    m_Profiles.sampleBuildJobs().push_back(std::move(job));
    m_StatusMessage = "Queued background terrain profile sampling.";
    return true;
}

void Application::RebuildAllTerrainProfileSamples()
{
    if (!m_Profiles.sampleBuildJobs().empty())
    {
        m_StatusMessage = "Terrain profile sampling already running.";
        return;
    }

    const bool hasTiledTerrain = std::any_of(m_Terrain.datasets().begin(), m_Terrain.datasets().end(), [](const TerrainDataset& dataset) {
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

void Application::RebuildIsolineSampleGridIfNeeded()
{
    if (!m_Isolines.sampleGridDirty())
    {
        return;
    }

    // Gather every visible, loaded, bounds-valid dataset.  We build a single
    // sample grid spanning the *union* of their bounds and sample each cell
    // from whichever dataset contains it.  This matches the visible domain of
    // both the world atlas and the terrain-profile map (both render the union
    // of all visible terrains) so isolines are no longer cut off at the
    // active terrain's boundary when a second terrain is loaded next to it.
    std::vector<const TerrainDataset*> contributingDatasets;
    contributingDatasets.reserve(m_Terrain.datasets().size());
    for (const TerrainDataset& dataset : m_Terrain.datasets())
    {
        if (!dataset.visible || !dataset.loaded || !dataset.bounds.valid)
        {
            continue;
        }
        contributingDatasets.push_back(&dataset);
    }

    // Fast path: exactly one non-tiled dataset with a built height grid.
    // BuildTerrainIsolineSampleGrid is much cheaper than the union sampler
    // because it reads the grid directly without per-cell containment tests.
    if (contributingDatasets.size() == 1 &&
        !contributingDatasets[0]->hasTileManifest &&
        contributingDatasets[0]->heightGrid.IsValid())
    {
        m_Isolines.SetSampleGrid(BuildTerrainIsolineSampleGrid(contributingDatasets[0]->heightGrid,
                                                               m_Isolines.settings()));
        return;
    }

    if (contributingDatasets.empty())
    {
        // No usable datasets — fall back to the global height grid (legacy behaviour).
        m_Isolines.SetSampleGrid(BuildTerrainIsolineSampleGrid(m_TerrainHeightGrid, m_Isolines.settings()));
        return;
    }

    // Adapt the contributing datasets into the pure union builder's input:
    // each source carries its bounds plus a callback that samples its height
    // (walking tiles / the dataset's height grid as appropriate).  The builder
    // owns the union-bounds + per-cell ownership logic and is unit-tested.
    std::vector<IsolineUnionSource> sources;
    sources.reserve(contributingDatasets.size());
    for (const TerrainDataset* dataset : contributingDatasets)
    {
        IsolineUnionSource source;
        source.minLatitude  = dataset->bounds.minLatitude;
        source.maxLatitude  = dataset->bounds.maxLatitude;
        source.minLongitude = dataset->bounds.minLongitude;
        source.maxLongitude = dataset->bounds.maxLongitude;
        source.minHeight    = dataset->bounds.minHeight;
        source.maxHeight    = dataset->bounds.maxHeight;
        source.sampleHeight = [this, dataset](double latitude, double longitude) {
            return SampleTerrainHeightAt(*dataset, latitude, longitude);
        };
        sources.push_back(std::move(source));
    }

    m_Isolines.SetSampleGrid(BuildUnionIsolineSampleGrid(
        sources,
        std::clamp(m_Isolines.settings().resolutionX, 2, 512),
        std::clamp(m_Isolines.settings().resolutionZ, 2, 512)));
}

void Application::RefreshIsolinesIfNeeded()
{
    // First rebuild the terrain-sampled grid if the terrain footprint changed
    // (terrain-coupled, so it lives here), then let the IsolineSystem harvest
    // any finished async segment build and submit a new one if needed.
    RebuildIsolineSampleGridIfNeeded();
    m_Isolines.RefreshSegments(m_BackgroundJobs.get());
}

size_t Application::CountSceneTriangles() const
{
    size_t triangleCount = 0;
    for (const TerrainDataset& dataset : m_Terrain.datasets())
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

    for (const ImportedAsset& asset : m_Assets.assets())
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
    // m_TerrainHeightGrid is the *monolithic* height grid for the active
    // dataset.  It's only populated when the active dataset isn't tiled —
    // tile-streamed datasets (Nepal) keep their height data inside each
    // TerrainTile and leave m_TerrainHeightGrid invalid.  If we returned
    // m_GeoReference.originHeight in that case, callers would silently get
    // a flat plane (e.g. SnapSelectedImportedAssetsToTerrain would snap
    // every asset to the manifest's origin elevation instead of the actual
    // terrain).  Delegate to the dataset-aware overload, which properly
    // walks the active dataset's tile bounds and per-tile heightGrids.
    if (m_TerrainHeightGrid.IsValid())
    {
        return static_cast<float>(m_TerrainHeightGrid.SampleHeight(latitude, longitude));
    }

    const TerrainDataset* activeTerrain = GetActiveTerrainDataset();
    if (activeTerrain != nullptr && activeTerrain->loaded)
    {
        return SampleTerrainHeightAt(*activeTerrain, latitude, longitude);
    }

    return static_cast<float>(m_GeoReference.originHeight);
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

    for (const TerrainDataset& dataset : m_Terrain.datasets())
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
    for (const TerrainDataset& dataset : m_Terrain.datasets())
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
//  Navigate camera to a point picked on a terrain profile
// ─────────────────────────────────────────────────────────────────────────────
void Application::JumpCameraToProfileSample(const TerrainProfile& profile, const TerrainProfileSample& sample)
{
    if (!sample.valid)
    {
        m_StatusMessage = "That profile point is outside terrain coverage.";
        return;
    }

    // Activate the profile's primary terrain first so the active coordinate
    // frame (m_GeoReference) matches the frame the sample's localPosition was
    // computed in — otherwise the teleport would land in the wrong frame, the
    // same megametre-drift bug the atlas-click fix addressed.
    const TerrainDataset* primary = GetPrimaryTerrainForProfile(profile);
    if (primary != nullptr)
    {
        int primaryIndex = -1;
        for (size_t i = 0; i < m_Terrain.datasets().size(); ++i)
        {
            if (&m_Terrain.datasets()[i] == primary)
            {
                primaryIndex = static_cast<int>(i);
                break;
            }
        }
        if (primaryIndex >= 0 && primaryIndex != m_Terrain.activeIndex())
        {
            ActivateTerrainDataset(primaryIndex);
        }
    }

    // Stand the camera on the picked ground point at eye height, looking at a
    // gentle downward angle while keeping the current heading.
    const glm::vec3 ground(static_cast<float>(sample.localPosition.x),
                           static_cast<float>(sample.localPosition.y),
                           static_cast<float>(sample.localPosition.z));
    const float eyeHeight = std::max(m_GravitySettings.playerHeightMeters, 1.6f);
    QueueCameraTeleport(ground + glm::vec3(0.0f, eyeHeight, 0.0f));
    SnapCameraView(m_Camera.GetYaw(), -8.0f);

    char status[160];
    std::snprintf(status, sizeof(status),
                  "Jumped to profile point: %.6f, %.6f at %.1f m",
                  sample.latitude, sample.longitude, sample.height);
    m_StatusMessage = status;
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
    // Informational: a teleport of >100 km from origin means the target lies
    // outside the active local frame's "sweet spot" — float precision will be
    // coarse there and any terrain rendered in a different frame will not be
    // visible.  This is often legitimate (e.g. user typed coordinates from
    // another dataset into the live "Go to coordinates" input on purpose),
    // so we log it as info rather than warning.
    const float magnitude = std::sqrt(position.x * position.x +
                                      position.y * position.y +
                                      position.z * position.z);
    if (magnitude > 100000.0f)
    {
        std::cout << "[GeoFPS] info: large camera teleport queued -> ("
                  << position.x << ", " << position.y << ", " << position.z
                  << ")  |distance from origin| = " << magnitude / 1000.0f
                  << " km (outside active frame's sweet spot).\n";
    }
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

    for (int i = 0; i < static_cast<int>(m_Assets.assets().size()); ++i)
    {
        const ImportedAsset& asset = m_Assets.assets()[static_cast<size_t>(i)];
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
        for (ImportedAsset& a : m_Assets.assets()) a.selected = false;
        m_Assets.assets()[static_cast<size_t>(bestIdx)].selected = true;
        m_Assets.setActiveIndex(bestIdx);
        m_StatusMessage = "Selected: " + m_Assets.assets()[static_cast<size_t>(bestIdx)].name;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Feature: In-world asset label rendering
// ─────────────────────────────────────────────────────────────────────────────
void Application::RenderAssetLabels()
{
    if (!m_AssetLabelSettings.visible) return;
    if (m_Assets.assets().empty()) return;

    const glm::mat4 vp      = GetRenderViewProjectionMatrix();
    const float W = static_cast<float>(m_Window.GetWidth());
    const float H = static_cast<float>(m_Window.GetHeight());
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (const ImportedAsset& asset : m_Assets.assets())
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
    if (m_Profiles.activeIndex() < 0 ||
        m_Profiles.activeIndex() >= static_cast<int>(m_Profiles.profiles().size()))
    {
        m_StatusMessage = "No active profile to export.";
        return false;
    }
    const TerrainProfile& profile = m_Profiles.profiles()[static_cast<size_t>(m_Profiles.activeIndex())];
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
    if (m_Profiles.activeIndex() < 0 ||
        m_Profiles.activeIndex() >= static_cast<int>(m_Profiles.profiles().size()))
    {
        m_StatusMessage = "No active profile to export.";
        return false;
    }
    const TerrainProfile& profile = m_Profiles.profiles()[static_cast<size_t>(m_Profiles.activeIndex())];
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
