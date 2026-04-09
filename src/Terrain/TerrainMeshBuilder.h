#pragma once

#include "Math/GeoConverter.h"
#include "Renderer/Mesh.h"
#include "Terrain/TerrainImporter.h"
#include <vector>

namespace GeoFPS
{
struct TerrainBuildSettings
{
    int gridResolutionX {64};
    int gridResolutionZ {64};
    float heightScale {1.0f};
};

class TerrainMeshBuilder
{
  public:
    MeshData BuildFromGeographicPoints(const std::vector<TerrainPoint>& points,
                                       const GeoConverter& converter,
                                       const TerrainBuildSettings& settings) const;
};
} // namespace GeoFPS
