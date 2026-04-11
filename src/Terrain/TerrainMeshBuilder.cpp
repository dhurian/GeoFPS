#include "Terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace GeoFPS
{
namespace
{
struct Bounds
{
    double minX {std::numeric_limits<double>::max()};
    double maxX {std::numeric_limits<double>::lowest()};
    double minZ {std::numeric_limits<double>::max()};
    double maxZ {std::numeric_limits<double>::lowest()};
};

struct HeightSample
{
    double totalHeight {0.0};
    int count {0};
};

void AccumulateBounds(Bounds& bounds, const glm::dvec3& point)
{
    bounds.minX = std::min(bounds.minX, point.x);
    bounds.maxX = std::max(bounds.maxX, point.x);
    bounds.minZ = std::min(bounds.minZ, point.z);
    bounds.maxZ = std::max(bounds.maxZ, point.z);
}

float ResolveHeightSample(const std::vector<HeightSample>& heightGrid,
                          int gridResolutionX,
                          int gridResolutionZ,
                          int targetX,
                          int targetZ)
{
    const auto gridIndex = [gridResolutionX](int x, int z) {
        return static_cast<size_t>(z * gridResolutionX + x);
    };

    const HeightSample& directSample = heightGrid[gridIndex(targetX, targetZ)];
    if (directSample.count > 0)
    {
        return static_cast<float>(directSample.totalHeight / static_cast<double>(directSample.count));
    }

    const int maxRadius = std::max(gridResolutionX, gridResolutionZ);
    for (int radius = 1; radius < maxRadius; ++radius)
    {
        double weightedHeight = 0.0;
        double totalWeight = 0.0;

        const int minZ = std::max(targetZ - radius, 0);
        const int maxZ = std::min(targetZ + radius, gridResolutionZ - 1);
        const int minX = std::max(targetX - radius, 0);
        const int maxX = std::min(targetX + radius, gridResolutionX - 1);

        for (int z = minZ; z <= maxZ; ++z)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const HeightSample& sample = heightGrid[gridIndex(x, z)];
                if (sample.count == 0)
                {
                    continue;
                }

                const int dx = x - targetX;
                const int dz = z - targetZ;
                const double distanceSquared = static_cast<double>((dx * dx) + (dz * dz));
                const double weight = 1.0 / std::max(distanceSquared, 1.0);
                weightedHeight += (sample.totalHeight / static_cast<double>(sample.count)) * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight > 0.0)
        {
            return static_cast<float>(weightedHeight / totalWeight);
        }
    }

    return 0.0f;
}
} // namespace

MeshData TerrainMeshBuilder::BuildFromGeographicPoints(const std::vector<TerrainPoint>& points,
                                                       const GeoConverter& converter,
                                                       const TerrainBuildSettings& settings) const
{
    MeshData meshData;
    if (points.size() < 3 || settings.gridResolutionX < 2 || settings.gridResolutionZ < 2)
    {
        return meshData;
    }

    std::vector<glm::dvec3> localPoints;
    localPoints.reserve(points.size());
    Bounds bounds;
    for (const auto& point : points)
    {
        const glm::dvec3 local = converter.ToLocal(point.latitude, point.longitude, point.height);
        localPoints.push_back(local);
        AccumulateBounds(bounds, local);
    }

    const double width = std::max(bounds.maxX - bounds.minX, 1.0);
    const double depth = std::max(bounds.maxZ - bounds.minZ, 1.0);

    const size_t vertexCount = static_cast<size_t>(settings.gridResolutionX * settings.gridResolutionZ);
    meshData.vertices.reserve(vertexCount);

    std::vector<HeightSample> heightGrid(vertexCount);
    const auto gridIndex = [&settings](int x, int z) {
        return static_cast<size_t>(z * settings.gridResolutionX + x);
    };

    for (const auto& point : localPoints)
    {
        const double normalizedX = std::clamp((point.x - bounds.minX) / width, 0.0, 1.0);
        const double normalizedZ = std::clamp((point.z - bounds.minZ) / depth, 0.0, 1.0);
        const int xIndex = std::clamp(
            static_cast<int>(std::round(normalizedX * static_cast<double>(settings.gridResolutionX - 1))),
            0,
            settings.gridResolutionX - 1);
        const int zIndex = std::clamp(
            static_cast<int>(std::round(normalizedZ * static_cast<double>(settings.gridResolutionZ - 1))),
            0,
            settings.gridResolutionZ - 1);

        HeightSample& sample = heightGrid[gridIndex(xIndex, zIndex)];
        sample.totalHeight += point.y;
        sample.count += 1;
    }

    for (int zIndex = 0; zIndex < settings.gridResolutionZ; ++zIndex)
    {
        const double zT = static_cast<double>(zIndex) / static_cast<double>(settings.gridResolutionZ - 1);
        const double z = bounds.minZ + zT * depth;
        for (int xIndex = 0; xIndex < settings.gridResolutionX; ++xIndex)
        {
            const double xT = static_cast<double>(xIndex) / static_cast<double>(settings.gridResolutionX - 1);
            const double x = bounds.minX + xT * width;
            const float height = ResolveHeightSample(
                                     heightGrid, settings.gridResolutionX, settings.gridResolutionZ, xIndex, zIndex) *
                                 settings.heightScale;

            Vertex vertex;
            vertex.position = glm::vec3(static_cast<float>(x), height, static_cast<float>(z));
            vertex.uv = glm::vec2(static_cast<float>(xT), static_cast<float>(zT));
            meshData.vertices.push_back(vertex);
        }
    }

    auto vertexIndex = [&settings](int x, int z) {
        return static_cast<unsigned int>(z * settings.gridResolutionX + x);
    };

    for (int z = 0; z < settings.gridResolutionZ - 1; ++z)
    {
        for (int x = 0; x < settings.gridResolutionX - 1; ++x)
        {
            const unsigned int i0 = vertexIndex(x, z);
            const unsigned int i1 = vertexIndex(x + 1, z);
            const unsigned int i2 = vertexIndex(x, z + 1);
            const unsigned int i3 = vertexIndex(x + 1, z + 1);

            meshData.indices.push_back(i0);
            meshData.indices.push_back(i2);
            meshData.indices.push_back(i1);

            meshData.indices.push_back(i1);
            meshData.indices.push_back(i2);
            meshData.indices.push_back(i3);
        }
    }

    for (size_t i = 0; i < meshData.indices.size(); i += 3)
    {
        Vertex& a = meshData.vertices[meshData.indices[i + 0]];
        Vertex& b = meshData.vertices[meshData.indices[i + 1]];
        Vertex& c = meshData.vertices[meshData.indices[i + 2]];

        const glm::vec3 normal = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
        a.normal += normal;
        b.normal += normal;
        c.normal += normal;
    }

    for (auto& vertex : meshData.vertices)
    {
        vertex.normal = glm::normalize(vertex.normal);
    }

    return meshData;
}
} // namespace GeoFPS
