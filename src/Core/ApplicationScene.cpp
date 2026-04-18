#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include <iostream>
#include <limits>

namespace GeoFPS
{
using namespace ApplicationInternal;
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
        asset.position = entry.position + glm::vec3(static_cast<float>(index + 1) * 2.0f, 0.0f, static_cast<float>(index + 1) * 2.0f);
        asset.rotationDegrees.z = entry.rotationZDegrees;
        asset.scale = entry.scale;
        asset.tint = entry.tint;
        asset.selected = true;
        if (asset.useGeographicPlacement)
        {
            UpdateImportedAssetPositionFromGeographic(asset);
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
    if (!TerrainImporter::LoadCSV(dataset.path, dataset.points))
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

    dataset.geoReference.originLatitude = dataset.points.front().latitude;
    dataset.geoReference.originLongitude = dataset.points.front().longitude;
    dataset.geoReference.originHeight = dataset.points.front().height;
    dataset.loaded = true;

    for (auto& overlay : dataset.overlays)
    {
        ResetOverlayToTerrainBounds(overlay.image, dataset.points);
    }

    m_StatusMessage = "Loaded terrain: " + dataset.name;
    return true;
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

    if (!LoadActiveOverlayImage())
    {
        m_OverlayTexture.Reset();
    }

    return RebuildTerrain();
}

bool Application::LoadOverlayImage(OverlayEntry& overlay)
{
    if (overlay.image.imagePath.empty())
    {
        overlay.image.loaded = false;
        m_OverlayTexture.Reset();
        m_StatusMessage = "Overlay path is empty.";
        return false;
    }

    overlay.image.loaded = m_OverlayTexture.LoadFromFile(overlay.image.imagePath);
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
        m_OverlayTexture.Reset();
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

    m_OverlayTexture.Reset();
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
        primitive.hasBaseColorTexture =
            !primitive.baseColorPixels.empty() && primitive.baseColorTexture.LoadFromMemory(primitive.baseColorPixels.data(),
                                                                                            primitive.baseColorWidth,
                                                                                            primitive.baseColorHeight,
                                                                                            primitive.baseColorChannels);
    }

    asset.loaded = true;
    m_StatusMessage = "Loaded asset: " + asset.name;
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

bool Application::RebuildTerrain()
{
    GeoConverter converter(m_GeoReference);
    TerrainMeshBuilder builder;
    MeshData meshData = builder.BuildFromGeographicPoints(m_TerrainPoints, converter, m_TerrainSettings);

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        return false;
    }

    m_TerrainMesh = std::make_unique<Mesh>(meshData);
    m_StatusMessage = "Terrain mesh rebuilt.";
    return true;
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
    ResetOverlayToTerrainBounds(imageDefinition, m_TerrainPoints);
}

void Application::LoadActiveTerrainIntoScene()
{
    const TerrainDataset* dataset = GetActiveTerrainDataset();
    if (dataset == nullptr)
    {
        return;
    }

    m_TerrainPoints = dataset->points;
    m_GeoReference = dataset->geoReference;
    m_TerrainSettings = dataset->settings;
}

float Application::SampleTerrainHeightAt(double latitude, double longitude) const
{
    if (m_TerrainPoints.empty())
    {
        return static_cast<float>(m_GeoReference.originHeight);
    }

    double bestDistanceSquared = std::numeric_limits<double>::max();
    double bestHeight = m_TerrainPoints.front().height;
    for (const TerrainPoint& point : m_TerrainPoints)
    {
        const double latitudeDelta = point.latitude - latitude;
        const double longitudeDelta = point.longitude - longitude;
        const double distanceSquared = (latitudeDelta * latitudeDelta) + (longitudeDelta * longitudeDelta);
        if (distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            bestHeight = point.height;
        }
    }

    return static_cast<float>(bestHeight);
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


} // namespace GeoFPS
