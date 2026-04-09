#pragma once

#include <string>
#include <vector>

namespace GeoFPS
{
struct TerrainPoint
{
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
};

class TerrainImporter
{
  public:
    static bool LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints);
};
} // namespace GeoFPS
