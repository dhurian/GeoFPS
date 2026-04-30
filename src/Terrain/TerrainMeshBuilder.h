#pragma once

#include "Math/GeoConverter.h"
#include "Renderer/Mesh.h"
#include "Terrain/TerrainImporter.h"
#include <vector>

namespace GeoFPS
{
enum class TerrainCoordinateMode
{
    Geographic,
    LocalMeters,
    Projected
};

struct TerrainBuildSettings
{
    int gridResolutionX {256};
    int gridResolutionZ {256};
    float heightScale {1.0f};
    int smoothingPasses {1};
    int importSampleStep {1};
    int chunkResolution {64};
    bool colorByHeight {true};
    bool autoHeightColorRange {true};
    float heightColorMin {0.0f};
    float heightColorMax {1000.0f};
    glm::vec3 lowHeightColor {0.18f, 0.32f, 0.16f};
    glm::vec3 midHeightColor {0.50f, 0.43f, 0.28f};
    glm::vec3 highHeightColor {0.92f, 0.90f, 0.82f};
    TerrainCoordinateMode coordinateMode {TerrainCoordinateMode::Geographic};
    CrsMetadata crs {};
};

struct TerrainMeshChunkData
{
    MeshData meshData;
    float minX {0.0f};
    float maxX {0.0f};
    float minY {0.0f};
    float maxY {0.0f};
    float minZ {0.0f};
    float maxZ {0.0f};
};

class TerrainMeshBuilder
{
  public:
    MeshData BuildFromGeographicPoints(const std::vector<TerrainPoint>& points,
                                       const GeoConverter& converter,
                                       const TerrainBuildSettings& settings) const;
    std::vector<TerrainMeshChunkData> BuildChunksFromGeographicPoints(const std::vector<TerrainPoint>& points,
                                                                     const GeoConverter& converter,
                                                                     const TerrainBuildSettings& settings) const;
};
} // namespace GeoFPS
