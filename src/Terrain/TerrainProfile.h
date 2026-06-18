#pragma once

#include "Math/GeoConverter.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include <glm/glm.hpp>
#include <functional>
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

// Per-sample line-of-sight result along a profile, computed from a single
// observer standing at the first valid sample.  `visible[i]` is true when the
// terrain at sample i can be seen by the observer (false for hidden samples
// AND for invalid/out-of-coverage ones).  See ComputeProfileLineOfSight.
struct ProfileLineOfSightResult
{
    std::vector<bool> visible;          // one entry per input sample
    int    observerSampleIndex {-1};    // first valid sample (the observer)
    double observerEyeHeight {0.0};     // absolute eye elevation (ground + offset)
    bool   endpointVisible {false};     // can the observer see the last valid sample
    int    firstBlockedSampleIndex {-1}; // first hidden sample past the observer, or -1
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

// One contributing terrain for a multi-dataset isoline sample grid: its
// geographic bounds plus a callback that samples its height at a lat/lon.
// Decoupled from the (GL-heavy) TerrainDataset type so the union builder is a
// pure function the caller adapts its datasets into.
struct IsolineUnionSource
{
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    std::function<float(double latitude, double longitude)> sampleHeight;
};

// Build a sample grid spanning the *union* of every source's bounds, sampling
// each cell from the first source whose bounds contain it (deterministic
// tie-break on overlap).  Cells outside every source's footprint take a
// sentinel = the union's minimum height, so they don't introduce spurious
// contour lines along the union boundary.  Returns an invalid grid if sources
// is empty or the resolution is degenerate.
[[nodiscard]] TerrainIsolineSampleGrid BuildUnionIsolineSampleGrid(const std::vector<IsolineUnionSource>& sources,
                                                                   int resolutionX,
                                                                   int resolutionZ);
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

// Compute terrain line-of-sight along a profile from an observer standing at
// the first valid sample, whose eye sits observerEyeHeightMeters above the
// ground there.  Uses the running-maximum-elevation-angle method: walking
// outward, a sample is visible iff its elevation angle from the observer eye
// is at least the highest angle of any nearer terrain (anything steeper closer
// in blocks the view past it).  Invalid (out-of-coverage) samples are marked
// not-visible and do not occlude.  Pure function — no engine state.
[[nodiscard]] ProfileLineOfSightResult ComputeProfileLineOfSight(const std::vector<TerrainProfileSample>& samples,
                                                                 double observerEyeHeightMeters);

bool ExportTerrainProfiles(const std::string& path, const std::vector<TerrainProfile>& profiles);
bool ImportTerrainProfiles(const std::string& path, std::vector<TerrainProfile>& profiles, std::string& errorMessage);

} // namespace GeoFPS
