#pragma once

#include "Math/GeoConverter.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GeoFPS
{
struct TerrainProfileVertex
{
    double latitude {0.0};
    double longitude {0.0};
    bool auxiliary {false};
    glm::dvec3 localPosition {0.0};
};

struct TerrainProfileSample
{
    double distanceMeters {0.0};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    double lineAngleDegrees {0.0};
    glm::dvec3 localPosition {0.0};
    bool valid {true};
};

struct TerrainProfile
{
    std::string name {"Profile 1"};
    std::vector<TerrainProfileVertex> vertices;
    std::vector<TerrainProfileSample> samples;
    glm::vec4 color {1.0f, 0.2f, 0.1f, 1.0f};
    float thickness {3.0f};
    float worldThicknessMeters {8.0f};
    float worldGroundOffsetMeters {5.0f};
    float sampleSpacingMeters {10.0f};
    std::vector<std::string> includedTerrainNames;
    bool visible {true};
    bool showInWorld {false};
    bool useLocalCoordinates {false};
};

struct TerrainIsolineSegment
{
    double levelHeight {0.0};
    TerrainProfileVertex start;
    TerrainProfileVertex end;
    glm::vec4 color {1.0f};
};

struct TerrainIsolineSettings
{
    bool enabled {false};
    int resolutionX {64};
    int resolutionZ {64};
    double contourIntervalMeters {0.0};
    bool autoInterval {true};
    float thickness {1.5f};
    float opacity {0.75f};
};

struct TerrainIsolineSampleGrid
{
    int resolutionX {0};
    int resolutionZ {0};
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    std::vector<float> heights;

    [[nodiscard]] bool IsValid() const;
};

class TerrainHeightGrid
{
  public:
    bool Build(const std::vector<TerrainPoint>& points);
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] bool Contains(double latitude, double longitude) const;
    [[nodiscard]] double SampleHeight(double latitude, double longitude) const;
    [[nodiscard]] double MinHeight() const;
    [[nodiscard]] double MaxHeight() const;
    [[nodiscard]] double MinLatitude() const { return m_Latitudes.empty() ? 0.0 : m_Latitudes.front(); }
    [[nodiscard]] double MaxLatitude() const { return m_Latitudes.empty() ? 0.0 : m_Latitudes.back(); }
    [[nodiscard]] double MinLongitude() const { return m_Longitudes.empty() ? 0.0 : m_Longitudes.front(); }
    [[nodiscard]] double MaxLongitude() const { return m_Longitudes.empty() ? 0.0 : m_Longitudes.back(); }

  private:
    struct HeightCell
    {
        double totalHeight {0.0};
        int count {0};
    };

    [[nodiscard]] size_t CellIndex(size_t longitudeIndex, size_t latitudeIndex) const;
    [[nodiscard]] double ResolveCellHeight(size_t longitudeIndex, size_t latitudeIndex) const;

    std::vector<double> m_Latitudes;
    std::vector<double> m_Longitudes;
    std::vector<HeightCell> m_Cells;
    double m_GlobalAverageHeight {0.0};
};

[[nodiscard]] std::vector<TerrainProfileSample> SampleTerrainProfile(const std::vector<TerrainProfileVertex>& vertices,
                                                                     const TerrainHeightGrid& heightGrid,
                                                                     const GeoConverter& converter,
                                                                     double sampleSpacingMeters,
                                                                     bool useLocalCoordinates = false,
                                                                     TerrainCoordinateMode coordinateMode = TerrainCoordinateMode::Geographic);
[[nodiscard]] double TerrainProfileLineAngleDegrees(const TerrainProfileVertex& start,
                                                    const TerrainProfileVertex& end,
                                                    const GeoConverter& converter,
                                                    bool useLocalCoordinates = false,
                                                    TerrainCoordinateMode coordinateMode = TerrainCoordinateMode::Geographic);
[[nodiscard]] std::vector<TerrainIsolineSegment> GenerateTerrainIsolines(const TerrainHeightGrid& heightGrid,
                                                                        const TerrainIsolineSettings& settings);
[[nodiscard]] TerrainIsolineSampleGrid BuildTerrainIsolineSampleGrid(const TerrainHeightGrid& heightGrid,
                                                                     const TerrainIsolineSettings& settings);
[[nodiscard]] std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesFromSampleGrid(const TerrainIsolineSampleGrid& sampleGrid,
                                                                                      const TerrainIsolineSettings& settings);
[[nodiscard]] std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesAccelerated(const TerrainHeightGrid& heightGrid,
                                                                                   const TerrainIsolineSettings& settings,
                                                                                   bool* usedGpu = nullptr);
[[nodiscard]] std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesAccelerated(const TerrainIsolineSampleGrid& sampleGrid,
                                                                                   const TerrainIsolineSettings& settings,
                                                                                   bool useGpu,
                                                                                   bool* usedGpu = nullptr);
[[nodiscard]] glm::vec4 IsolineColorForHeight(double height, double minHeight, double maxHeight, float opacity);
[[nodiscard]] double ResolveContourInterval(double minHeight, double maxHeight, const TerrainIsolineSettings& settings);

bool ExportTerrainProfiles(const std::string& path, const std::vector<TerrainProfile>& profiles);
bool ImportTerrainProfiles(const std::string& path, std::vector<TerrainProfile>& profiles, std::string& errorMessage);

} // namespace GeoFPS
