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

void AccumulateBounds(Bounds& bounds, const glm::dvec3& point)
{
    bounds.minX = std::min(bounds.minX, point.x);
    bounds.maxX = std::max(bounds.maxX, point.x);
    bounds.minZ = std::min(bounds.minZ, point.z);
    bounds.maxZ = std::max(bounds.maxZ, point.z);
}

float EstimateHeight(const std::vector<glm::dvec3>& points, double x, double z)
{
    double weightedHeight = 0.0;
    double totalWeight = 0.0;

    for (const auto& point : points)
    {
        const double dx = point.x - x;
        const double dz = point.z - z;
        const double distanceSquared = dx * dx + dz * dz;
        const double weight = 1.0 / std::max(distanceSquared, 0.0001);
        weightedHeight += point.y * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0)
    {
        return 0.0f;
    }

    return static_cast<float>(weightedHeight / totalWeight);
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

    meshData.vertices.reserve(static_cast<size_t>(settings.gridResolutionX * settings.gridResolutionZ));

    for (int zIndex = 0; zIndex < settings.gridResolutionZ; ++zIndex)
    {
        const double zT = static_cast<double>(zIndex) / static_cast<double>(settings.gridResolutionZ - 1);
        const double z = bounds.minZ + zT * depth;
        for (int xIndex = 0; xIndex < settings.gridResolutionX; ++xIndex)
        {
            const double xT = static_cast<double>(xIndex) / static_cast<double>(settings.gridResolutionX - 1);
            const double x = bounds.minX + xT * width;
            const float height = EstimateHeight(localPoints, x, z) * settings.heightScale;

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
