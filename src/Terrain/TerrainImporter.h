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

struct TerrainImportOptions
{
    int sampleStep {1};
    size_t maxPoints {0};
};

struct TerrainTileManifestEntry
{
    std::string path;
    int row {0};
    int col {0};
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    size_t pointCount {0};
};

struct TerrainTileManifest
{
    std::string name;
    std::string coordinateMode {"geographic"};
    std::string crs {"EPSG:4326"};
    double originLatitude {0.0};
    double originLongitude {0.0};
    double originHeight {0.0};
    double tileSizeDegrees {0.01};
    int tileOverlapSamples {1};
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    size_t pointCount {0};
    std::vector<TerrainTileManifestEntry> tiles;
};

class TerrainImporter
{
  public:
    static bool LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints);
    static bool LoadCSV(const std::string& path,
                        const TerrainImportOptions& options,
                        std::vector<TerrainPoint>& outPoints);
    static bool LoadTileManifest(const std::string& path,
                                 TerrainTileManifest& outManifest,
                                 std::string& errorMessage);
};
} // namespace GeoFPS
