#include "Terrain/TerrainMeshBuilder.h"
#include "Terrain/TerrainProfile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace GeoFPS
{
namespace
{
constexpr int kMaxLiveTerrainResolution = 1024;

struct Bounds
{
    double minX {std::numeric_limits<double>::max()};
    double maxX {std::numeric_limits<double>::lowest()};
    double minZ {std::numeric_limits<double>::max()};
    double maxZ {std::numeric_limits<double>::lowest()};
};

void AccumulateBounds(Bounds& bounds, const glm::dvec3& point)
{
    bounds.minX = std::min(bounds.minX, point.x);
    bounds.maxX = std::max(bounds.maxX, point.x);
    bounds.minZ = std::min(bounds.minZ, point.z);
    bounds.maxZ = std::max(bounds.maxZ, point.z);
}

float SampleSourceHeight(const TerrainHeightGrid& sourceHeightGrid,
                         const GeoConverter& converter,
                         TerrainCoordinateMode coordinateMode,
                         double x,
                         double z,
                         double originHeight,
                         float heightScale)
{
    if (coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return static_cast<float>(sourceHeightGrid.SampleHeight(x, z)) * heightScale;
    }

    const glm::dvec3 geographic = converter.ToGeographic({x, 0.0, z});
    const double localHeight = sourceHeightGrid.SampleHeight(geographic.x, geographic.y) - originHeight;
    return static_cast<float>(localHeight * static_cast<double>(heightScale));
}

glm::vec3 SafeNormalize(const glm::vec3& value, const glm::vec3& fallback)
{
    const float lengthSquared = glm::dot(value, value);
    if (lengthSquared <= 1e-10f || !std::isfinite(lengthSquared))
    {
        return fallback;
    }

    return value / std::sqrt(lengthSquared);
}

void SmoothTerrainHeights(std::vector<Vertex>& vertices, int resolutionX, int resolutionZ, int passes)
{
    if (passes <= 0 || resolutionX < 3 || resolutionZ < 3)
    {
        return;
    }

    std::vector<float> sourceHeights(vertices.size(), 0.0f);
    std::vector<float> smoothedHeights(vertices.size(), 0.0f);
    const auto indexOf = [resolutionX](int x, int z) {
        return static_cast<size_t>(z * resolutionX + x);
    };

    for (int pass = 0; pass < passes; ++pass)
    {
        for (size_t index = 0; index < vertices.size(); ++index)
        {
            sourceHeights[index] = vertices[index].position.y;
            smoothedHeights[index] = sourceHeights[index];
        }

        for (int z = 0; z < resolutionZ; ++z)
        {
            const int z0 = std::max(z - 1, 0);
            const int z1 = std::min(z + 1, resolutionZ - 1);
            for (int x = 0; x < resolutionX; ++x)
            {
                const int x0 = std::max(x - 1, 0);
                const int x1 = std::min(x + 1, resolutionX - 1);
                const size_t center = indexOf(x, z);
                const float weighted =
                    (sourceHeights[center] * 4.0f) +
                    (sourceHeights[indexOf(x0, z)] + sourceHeights[indexOf(x1, z)] +
                     sourceHeights[indexOf(x, z0)] + sourceHeights[indexOf(x, z1)]) *
                        2.0f +
                    (sourceHeights[indexOf(x0, z0)] + sourceHeights[indexOf(x1, z0)] +
                     sourceHeights[indexOf(x0, z1)] + sourceHeights[indexOf(x1, z1)]);
                smoothedHeights[center] = weighted / 16.0f;
            }
        }

        for (size_t index = 0; index < vertices.size(); ++index)
        {
            vertices[index].position.y = smoothedHeights[index];
        }
    }
}

void RecomputeGridNormals(std::vector<Vertex>& vertices, int resolutionX, int resolutionZ)
{
    if (resolutionX < 2 || resolutionZ < 2)
    {
        return;
    }

    const auto indexOf = [resolutionX](int x, int z) {
        return static_cast<size_t>(z * resolutionX + x);
    };

    for (int z = 0; z < resolutionZ; ++z)
    {
        const int z0 = std::max(z - 1, 0);
        const int z1 = std::min(z + 1, resolutionZ - 1);
        for (int x = 0; x < resolutionX; ++x)
        {
            const int x0 = std::max(x - 1, 0);
            const int x1 = std::min(x + 1, resolutionX - 1);

            const glm::vec3 west = vertices[indexOf(x0, z)].position;
            const glm::vec3 east = vertices[indexOf(x1, z)].position;
            const glm::vec3 south = vertices[indexOf(x, z0)].position;
            const glm::vec3 north = vertices[indexOf(x, z1)].position;
            const glm::vec3 tangentX = east - west;
            const glm::vec3 tangentZ = north - south;
            vertices[indexOf(x, z)].normal =
                SafeNormalize(glm::cross(tangentZ, tangentX), glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
}
} // namespace

MeshData TerrainMeshBuilder::BuildFromGeographicPoints(const std::vector<TerrainPoint>& points,
                                                       const GeoConverter& converter,
                                                       const TerrainBuildSettings& settings) const
{
    MeshData meshData;
    const int resolutionX = std::clamp(settings.gridResolutionX, 2, kMaxLiveTerrainResolution);
    const int resolutionZ = std::clamp(settings.gridResolutionZ, 2, kMaxLiveTerrainResolution);
    if (points.size() < 3)
    {
        return meshData;
    }

    std::vector<glm::dvec3> localPoints;
    localPoints.reserve(points.size());
    Bounds bounds;
    for (const auto& point : points)
    {
        const glm::dvec3 local = settings.coordinateMode == TerrainCoordinateMode::LocalMeters ?
                                     glm::dvec3(point.latitude, point.height, point.longitude) :
                                     converter.ToLocal(point.latitude, point.longitude, point.height);
        localPoints.push_back(local);
        AccumulateBounds(bounds, local);
    }

    const double width = std::max(bounds.maxX - bounds.minX, 1.0);
    const double depth = std::max(bounds.maxZ - bounds.minZ, 1.0);

    const size_t vertexCount = static_cast<size_t>(resolutionX * resolutionZ);
    meshData.vertices.reserve(vertexCount);

    TerrainHeightGrid sourceHeightGrid;
    sourceHeightGrid.Build(points);

    for (int zIndex = 0; zIndex < resolutionZ; ++zIndex)
    {
        const double zT = static_cast<double>(zIndex) / static_cast<double>(resolutionZ - 1);
        const double z = bounds.minZ + zT * depth;
        for (int xIndex = 0; xIndex < resolutionX; ++xIndex)
        {
            const double xT = static_cast<double>(xIndex) / static_cast<double>(resolutionX - 1);
            const double x = bounds.minX + xT * width;
            const float height = SampleSourceHeight(sourceHeightGrid,
                                                    converter,
                                                    settings.coordinateMode,
                                                    x,
                                                    z,
                                                    converter.GetReference().originHeight,
                                                    settings.heightScale);

            Vertex vertex;
            vertex.position = glm::vec3(static_cast<float>(x), height, static_cast<float>(z));
            vertex.uv = glm::vec2(static_cast<float>(xT), static_cast<float>(zT));
            meshData.vertices.push_back(vertex);
        }
    }

    SmoothTerrainHeights(meshData.vertices, resolutionX, resolutionZ, std::clamp(settings.smoothingPasses, 0, 4));

    auto vertexIndex = [resolutionX](int x, int z) {
        return static_cast<unsigned int>(z * resolutionX + x);
    };

    for (int z = 0; z < resolutionZ - 1; ++z)
    {
        for (int x = 0; x < resolutionX - 1; ++x)
        {
            const unsigned int i0 = vertexIndex(x, z);
            const unsigned int i1 = vertexIndex(x + 1, z);
            const unsigned int i2 = vertexIndex(x, z + 1);
            const unsigned int i3 = vertexIndex(x + 1, z + 1);
            const float h0 = meshData.vertices[i0].position.y;
            const float h1 = meshData.vertices[i1].position.y;
            const float h2 = meshData.vertices[i2].position.y;
            const float h3 = meshData.vertices[i3].position.y;

            if (std::abs(h0 - h3) <= std::abs(h1 - h2))
            {
                meshData.indices.push_back(i0);
                meshData.indices.push_back(i2);
                meshData.indices.push_back(i1);

                meshData.indices.push_back(i1);
                meshData.indices.push_back(i2);
                meshData.indices.push_back(i3);
            }
            else
            {
                meshData.indices.push_back(i0);
                meshData.indices.push_back(i2);
                meshData.indices.push_back(i3);

                meshData.indices.push_back(i0);
                meshData.indices.push_back(i3);
                meshData.indices.push_back(i1);
            }
        }
    }

    RecomputeGridNormals(meshData.vertices, resolutionX, resolutionZ);

    return meshData;
}

std::vector<TerrainMeshChunkData> TerrainMeshBuilder::BuildChunksFromGeographicPoints(
    const std::vector<TerrainPoint>& points,
    const GeoConverter& converter,
    const TerrainBuildSettings& settings) const
{
    std::vector<TerrainMeshChunkData> chunks;
    MeshData fullMesh = BuildFromGeographicPoints(points, converter, settings);
    const int resolutionX = std::clamp(settings.gridResolutionX, 2, kMaxLiveTerrainResolution);
    const int resolutionZ = std::clamp(settings.gridResolutionZ, 2, kMaxLiveTerrainResolution);
    if (fullMesh.vertices.size() != static_cast<size_t>(resolutionX * resolutionZ) || fullMesh.indices.empty())
    {
        return chunks;
    }

    const int chunkResolution = std::clamp(settings.chunkResolution, 16, kMaxLiveTerrainResolution);
    const auto fullIndex = [resolutionX](int x, int z) {
        return static_cast<size_t>(z * resolutionX + x);
    };

    for (int startZ = 0; startZ < resolutionZ - 1; startZ += chunkResolution - 1)
    {
        const int endZ = std::min(startZ + chunkResolution - 1, resolutionZ - 1);
        for (int startX = 0; startX < resolutionX - 1; startX += chunkResolution - 1)
        {
            const int endX = std::min(startX + chunkResolution - 1, resolutionX - 1);
            const int localResolutionX = endX - startX + 1;
            const int localResolutionZ = endZ - startZ + 1;
            if (localResolutionX < 2 || localResolutionZ < 2)
            {
                continue;
            }

            TerrainMeshChunkData chunk;
            chunk.meshData.vertices.reserve(static_cast<size_t>(localResolutionX * localResolutionZ));
            chunk.minX = std::numeric_limits<float>::max();
            chunk.minY = std::numeric_limits<float>::max();
            chunk.minZ = std::numeric_limits<float>::max();
            chunk.maxX = std::numeric_limits<float>::lowest();
            chunk.maxY = std::numeric_limits<float>::lowest();
            chunk.maxZ = std::numeric_limits<float>::lowest();

            for (int z = startZ; z <= endZ; ++z)
            {
                for (int x = startX; x <= endX; ++x)
                {
                    const Vertex vertex = fullMesh.vertices[fullIndex(x, z)];
                    chunk.meshData.vertices.push_back(vertex);
                    chunk.minX = std::min(chunk.minX, vertex.position.x);
                    chunk.maxX = std::max(chunk.maxX, vertex.position.x);
                    chunk.minY = std::min(chunk.minY, vertex.position.y);
                    chunk.maxY = std::max(chunk.maxY, vertex.position.y);
                    chunk.minZ = std::min(chunk.minZ, vertex.position.z);
                    chunk.maxZ = std::max(chunk.maxZ, vertex.position.z);
                }
            }

            const auto localIndex = [localResolutionX](int x, int z) {
                return static_cast<unsigned int>(z * localResolutionX + x);
            };
            for (int z = 0; z < localResolutionZ - 1; ++z)
            {
                for (int x = 0; x < localResolutionX - 1; ++x)
                {
                    const unsigned int i0 = localIndex(x, z);
                    const unsigned int i1 = localIndex(x + 1, z);
                    const unsigned int i2 = localIndex(x, z + 1);
                    const unsigned int i3 = localIndex(x + 1, z + 1);
                    const float h0 = chunk.meshData.vertices[i0].position.y;
                    const float h1 = chunk.meshData.vertices[i1].position.y;
                    const float h2 = chunk.meshData.vertices[i2].position.y;
                    const float h3 = chunk.meshData.vertices[i3].position.y;
                    if (std::abs(h0 - h3) <= std::abs(h1 - h2))
                    {
                        chunk.meshData.indices.push_back(i0);
                        chunk.meshData.indices.push_back(i2);
                        chunk.meshData.indices.push_back(i1);
                        chunk.meshData.indices.push_back(i1);
                        chunk.meshData.indices.push_back(i2);
                        chunk.meshData.indices.push_back(i3);
                    }
                    else
                    {
                        chunk.meshData.indices.push_back(i0);
                        chunk.meshData.indices.push_back(i2);
                        chunk.meshData.indices.push_back(i3);
                        chunk.meshData.indices.push_back(i0);
                        chunk.meshData.indices.push_back(i3);
                        chunk.meshData.indices.push_back(i1);
                    }
                }
            }

            chunks.push_back(std::move(chunk));
        }
    }

    return chunks;
}
} // namespace GeoFPS
