#include "Core/ApplicationInternal.h"
#include "Core/WorldFileParser.h"
#include "Math/GeoConverter.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include "Terrain/TerrainProfile.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
bool Near(double actual, double expected, double epsilon = 1e-6)
{
    return std::abs(actual - expected) <= epsilon;
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<GeoFPS::TerrainPoint> TestGrid()
{
    return {
        {0.0, 0.0, 0.0},
        {0.0, 1.0, 10.0},
        {1.0, 0.0, 20.0},
        {1.0, 1.0, 30.0},
    };
}

void TestTerrainCsvLoading()
{
    std::vector<GeoFPS::TerrainPoint> points;
    Require(GeoFPS::TerrainImporter::LoadCSV("assets/data/sample_terrain.csv", points), "sample terrain CSV should load");
    Require(points.size() == 16, "sample terrain CSV should contain 16 points");
    Require(Near(points.front().latitude, 48.1234), "first sample latitude");
    Require(Near(points.front().longitude, 11.5678), "first sample longitude");
    Require(Near(points.front().height, 520.4), "first sample height");
}

void TestTerrainCsvDecimation()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "geofps_decimation_test.csv";
    {
        std::ofstream file(path);
        file << "latitude,longitude,height\n";
        for (int index = 0; index < 10; ++index)
        {
            file << index << "," << index + 1 << "," << index + 2 << "\n";
        }
    }

    GeoFPS::TerrainImportOptions options;
    options.sampleStep = 3;
    std::vector<GeoFPS::TerrainPoint> points;
    Require(GeoFPS::TerrainImporter::LoadCSV(path.string(), options, points), "decimated terrain CSV should load");
    Require(points.size() == 4, "sample step 3 should keep four rows from ten source rows");
    Require(Near(points[1].latitude, 3.0), "decimated second point");
    std::filesystem::remove(path);
}

void TestSparseTerrainHeightGridDoesNotReturnSeaLevel()
{
    std::vector<GeoFPS::TerrainPoint> points = {
        {0.0, 0.0, 100.0},
        {0.0, 2.0, 200.0},
        {2.0, 0.0, 300.0},
    };

    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(points), "sparse terrain height grid should build");
    const double sampled = grid.SampleHeight(1.0, 1.0);
    Require(sampled > 50.0, "sparse terrain height sampling should not collapse to sea level");
}

void TestTerrainTileManifestParsing()
{
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "geofps_tile_manifest_test";
    std::filesystem::create_directories(directory);
    const std::filesystem::path manifestPath = directory / "tile_manifest.json";
    {
        std::ofstream file(manifestPath);
        file << R"json({
  "name": "tile_test",
  "coordinate_mode": "geographic",
  "crs": "EPSG:4326",
  "origin_latitude": 1.0,
  "origin_longitude": 2.0,
  "origin_height": 3.0,
  "tile_size_degrees": 0.01,
  "tile_overlap_samples": 1,
  "min_latitude": 1.0,
  "max_latitude": 1.02,
  "min_longitude": 2.0,
  "max_longitude": 2.02,
  "min_height": 3.0,
  "max_height": 30.0,
  "point_count": 9,
  "tiles": [
    {
      "path": "tile_0000_0000.csv",
      "row": 0,
      "col": 0,
      "min_latitude": 1.0,
      "max_latitude": 1.01,
      "min_longitude": 2.0,
      "max_longitude": 2.01,
      "min_height": 3.0,
      "max_height": 12.0,
      "point_count": 4
    }
  ]
})json";
    }

    GeoFPS::TerrainTileManifest manifest;
    std::string error;
    Require(GeoFPS::TerrainImporter::LoadTileManifest(manifestPath.string(), manifest, error),
            "terrain tile manifest should parse: " + error);
    Require(manifest.name == "tile_test", "tile manifest name parsed");
    Require(manifest.tiles.size() == 1, "tile manifest tile count parsed");
    Require(manifest.tiles.front().path.find("tile_0000_0000.csv") != std::string::npos,
            "tile manifest relative path resolved");
    Require(Near(manifest.tiles.front().maxHeight, 12.0), "tile manifest height bounds parsed");

    std::filesystem::remove(manifestPath);
    std::filesystem::remove(directory);
}

void TestGeoConverterRoundTrip()
{
    GeoFPS::GeoReference reference;
    reference.originLatitude = 48.1234;
    reference.originLongitude = 11.5678;
    reference.originHeight = 520.4;
    GeoFPS::GeoConverter converter(reference);
    const glm::dvec3 local = converter.ToLocal(48.124, 11.5685, 530.0);
    const glm::dvec3 geographic = converter.ToGeographic(local);
    Require(Near(geographic.x, 48.124), "round trip latitude");
    Require(Near(geographic.y, 11.5685), "round trip longitude");
    Require(Near(geographic.z, 530.0), "round trip height");
}

void TestWebMercatorCrsConversion()
{
    const GeoFPS::CrsMetadata crs = GeoFPS::GeoConverter::ParseCrs("EPSG:3857");
    const glm::dvec3 source = GeoFPS::GeoConverter::GeographicToSource({55.6761, 12.5683, 42.0}, crs);
    const glm::dvec3 geographic = GeoFPS::GeoConverter::SourceToGeographic(source, crs);
    Require(Near(geographic.x, 55.6761, 1e-5), "web mercator latitude round trip");
    Require(Near(geographic.y, 12.5683, 1e-5), "web mercator longitude round trip");
    Require(Near(geographic.z, 42.0), "web mercator height round trip");
}

void TestGridInterpolation()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    Require(Near(grid.SampleHeight(0.5, 0.5), 15.0), "center bilinear height");
    Require(Near(grid.SampleHeight(-10.0, -10.0), 0.0), "out-of-bounds low clamps");
    Require(Near(grid.SampleHeight(10.0, 10.0), 30.0), "out-of-bounds high clamps");
    Require(Near(grid.MinHeight(), 0.0), "grid min height");
    Require(Near(grid.MaxHeight(), 30.0), "grid max height");
}

void TestSingleLineSampling()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);

    const std::vector<GeoFPS::TerrainProfileVertex> vertices = {{0.0, 0.0}, {0.0, 1.0}};
    const auto samples = GeoFPS::SampleTerrainProfile(vertices, grid, converter, 40000.0);
    Require(samples.size() >= 3, "single segment should produce multiple samples");
    Require(Near(samples.front().distanceMeters, 0.0), "first sample distance");
    Require(Near(samples.front().height, 0.0), "first sample height");
    Require(samples.front().valid, "first sample should be valid");
    Require(Near(samples.back().height, 10.0), "last sample height");
    Require(samples.back().valid, "last sample should be valid");
    Require(Near(samples.back().lineAngleDegrees, 0.0, 0.1), "eastward line angle");
    Require(samples.back().distanceMeters > samples.front().distanceMeters, "distance should increase");
}

void TestOutOfBoundsProfileSampling()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);

    const std::vector<GeoFPS::TerrainProfileVertex> vertices = {{0.0, 0.0}, {0.0, 2.0}};
    const auto samples = GeoFPS::SampleTerrainProfile(vertices, grid, converter, 40000.0);
    Require(samples.size() >= 3, "out-of-bounds segment should still produce samples");
    Require(samples.front().valid, "first out-of-bounds test sample should be valid");
    Require(!samples.back().valid, "last out-of-bounds test sample should be invalid");
}

void TestLocalCoordinateProfileSampling()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);

    GeoFPS::TerrainProfileVertex start;
    start.localPosition = converter.ToLocal(0.0, 0.0, 0.0);
    GeoFPS::TerrainProfileVertex end;
    end.localPosition = converter.ToLocal(0.0, 1.0, 0.0);

    const auto samples = GeoFPS::SampleTerrainProfile({start, end}, grid, converter, 40000.0, true);
    Require(samples.size() >= 3, "local coordinate segment should produce samples");
    Require(samples.front().valid, "first local coordinate sample should be valid");
    Require(samples.back().valid, "last local coordinate sample should be valid");
    Require(Near(samples.front().height, 0.0), "first local coordinate sample height");
    Require(Near(samples.back().height, 10.0), "last local coordinate sample height");
}

void TestLocalMetersProfileSampling()
{
    const std::vector<GeoFPS::TerrainPoint> points = {
        {0.0, 0.0, 10.0},
        {10.0, 0.0, 20.0},
        {0.0, 10.0, 30.0},
        {10.0, 10.0, 40.0},
    };
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(points), "local meters grid should build");
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);

    const std::vector<GeoFPS::TerrainProfileVertex> vertices = {{0.0, 0.0}, {10.0, 0.0}};
    const auto samples = GeoFPS::SampleTerrainProfile(vertices,
                                                      grid,
                                                      converter,
                                                      5.0,
                                                      false,
                                                      GeoFPS::TerrainCoordinateMode::LocalMeters);
    Require(samples.size() == 3, "local meters profile should sample by meter spacing");
    Require(Near(samples.front().distanceMeters, 0.0), "local meters first distance");
    Require(Near(samples.back().distanceMeters, 10.0), "local meters last distance");
    Require(Near(samples.front().height, 10.0), "local meters first height");
    Require(Near(samples.back().height, 20.0), "local meters last height");
    Require(Near(samples.back().localPosition.x, 10.0), "local meters local x");
    Require(Near(samples.back().localPosition.y, 20.0), "local meters local y height");
    Require(Near(samples.back().localPosition.z, 0.0), "local meters local z");

    GeoFPS::TerrainProfileVertex start;
    start.localPosition = {0.0, 0.0, 0.0};
    GeoFPS::TerrainProfileVertex end;
    end.localPosition = {0.0, 0.0, 10.0};
    const auto localSamples = GeoFPS::SampleTerrainProfile({start, end},
                                                           grid,
                                                           converter,
                                                           5.0,
                                                           true,
                                                           GeoFPS::TerrainCoordinateMode::LocalMeters);
    Require(localSamples.size() == 3, "local meters local-XYZ profile should sample by meter spacing");
    Require(Near(localSamples.back().height, 30.0), "local meters local-XYZ end height");
    Require(Near(localSamples.back().distanceMeters, 10.0), "local meters local-XYZ distance");
}

void TestLineAngles()
{
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);
    Require(Near(GeoFPS::TerrainProfileLineAngleDegrees({0.0, 0.0}, {0.0, 1.0}, converter), 0.0, 0.1),
            "east line angle");
    Require(Near(GeoFPS::TerrainProfileLineAngleDegrees({0.0, 0.0}, {1.0, 0.0}, converter), 90.0, 0.1),
            "north line angle");
    Require(Near(GeoFPS::TerrainProfileLineAngleDegrees({0.0, 0.0}, {-1.0, 0.0}, converter), 270.0, 0.1),
            "south line angle");
}

void TestLocalMetersTerrainMeshGeneration()
{
    const std::vector<GeoFPS::TerrainPoint> points = {
        {0.0, 0.0, 10.0},
        {10.0, 0.0, 20.0},
        {0.0, 10.0, 30.0},
        {10.0, 10.0, 40.0},
    };
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);
    GeoFPS::TerrainBuildSettings settings;
    settings.gridResolutionX = 2;
    settings.gridResolutionZ = 2;
    settings.heightScale = 1.0f;
    settings.coordinateMode = GeoFPS::TerrainCoordinateMode::LocalMeters;

    GeoFPS::TerrainMeshBuilder builder;
    const GeoFPS::MeshData meshData = builder.BuildFromGeographicPoints(points, converter, settings);
    Require(meshData.vertices.size() == 4, "local meters mesh vertex count");
    Require(meshData.indices.size() == 6, "local meters mesh index count");

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const GeoFPS::Vertex& vertex : meshData.vertices)
    {
        minX = std::min(minX, vertex.position.x);
        maxX = std::max(maxX, vertex.position.x);
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
        minZ = std::min(minZ, vertex.position.z);
        maxZ = std::max(maxZ, vertex.position.z);
    }

    Require(Near(minX, 0.0), "local meters mesh min x");
    Require(Near(maxX, 10.0), "local meters mesh max x");
    Require(Near(minZ, 0.0), "local meters mesh min z");
    Require(Near(maxZ, 10.0), "local meters mesh max z");
    Require(Near(minY, 10.0), "local meters mesh min height");
    Require(Near(maxY, 40.0), "local meters mesh max height");
}

void TestGeographicTerrainMeshUsesLocalHeights()
{
    const std::vector<GeoFPS::TerrainPoint> points = {
        {48.0, 11.0, 1000.0},
        {48.0, 11.001, 1010.0},
        {48.001, 11.0, 1020.0},
        {48.001, 11.001, 1030.0},
    };
    GeoFPS::GeoReference reference;
    reference.originLatitude = points.front().latitude;
    reference.originLongitude = points.front().longitude;
    reference.originHeight = points.front().height;
    GeoFPS::GeoConverter converter(reference);
    GeoFPS::TerrainBuildSettings settings;
    settings.gridResolutionX = 2;
    settings.gridResolutionZ = 2;
    settings.heightScale = 1.0f;
    settings.coordinateMode = GeoFPS::TerrainCoordinateMode::Geographic;

    GeoFPS::TerrainMeshBuilder builder;
    const GeoFPS::MeshData meshData = builder.BuildFromGeographicPoints(points, converter, settings);
    Require(meshData.vertices.size() == 4, "geographic mesh vertex count");

    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    for (const GeoFPS::Vertex& vertex : meshData.vertices)
    {
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
        Require(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z),
                "geographic mesh normals should be finite");
        Require(vertex.normal.y > 0.0f, "geographic mesh normals should point upward");
    }

    Require(Near(minY, 0.0), "geographic mesh should subtract origin height");
    Require(Near(maxY, 30.0), "geographic mesh should preserve relative relief");
}

void TestPolylineSampling()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::GeoReference reference;
    GeoFPS::GeoConverter converter(reference);

    const std::vector<GeoFPS::TerrainProfileVertex> vertices = {{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};
    const auto samples = GeoFPS::SampleTerrainProfile(vertices, grid, converter, 60000.0);
    Require(samples.size() >= 5, "polyline should include both segments");
    Require(Near(samples.front().height, 0.0), "polyline first height");
    Require(Near(samples.back().height, 30.0), "polyline last height");
    Require(samples.back().distanceMeters > samples[samples.size() / 2].distanceMeters, "distance accumulates across segments");
}

void TestProfileExportImport()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "geofps_profile_test.geofpsprofile";
    GeoFPS::TerrainProfile profile;
    profile.name = "Test Profile";
    profile.vertices = {{0.0, 0.0, false}, {1.0, 1.0, true}};
    profile.color = {0.25f, 0.5f, 0.75f, 1.0f};
    profile.thickness = 4.0f;
    profile.worldThicknessMeters = 17.0f;
    profile.worldGroundOffsetMeters = 23.0f;
    profile.sampleSpacingMeters = 12.0f;
    profile.includedTerrainNames = {"Terrain A", "Terrain B"};
    profile.showInWorld = true;
    profile.useLocalCoordinates = true;
    profile.vertices.back().localPosition = {12.0, 0.0, 34.0};
    profile.samples.push_back({0.0, 0.0, 0.0, 3.0, 45.0, {1.0, 3.0, 2.0}});

    Require(GeoFPS::ExportTerrainProfiles(path.string(), {profile}), "profile export should succeed");

    std::vector<GeoFPS::TerrainProfile> imported;
    std::string error;
    Require(GeoFPS::ImportTerrainProfiles(path.string(), imported, error), "profile import should succeed: " + error);
    Require(imported.size() == 1, "one profile imported");
    Require(imported.front().name == "Test Profile", "profile name imported");
    Require(imported.front().vertices.size() == 2, "vertices imported");
    Require(Near(imported.front().vertices.back().latitude, 1.0), "vertex latitude imported");
    Require(imported.front().vertices.back().auxiliary, "auxiliary vertex marker imported");
    Require(Near(imported.front().color.b, 0.75), "style imported");
    Require(Near(imported.front().thickness, 4.0), "thickness imported");
    Require(Near(imported.front().worldThicknessMeters, 17.0), "world thickness imported");
    Require(Near(imported.front().worldGroundOffsetMeters, 23.0), "world ground offset imported");
    Require(imported.front().showInWorld, "world visibility imported");
    Require(imported.front().useLocalCoordinates, "coordinate mode imported");
    Require(Near(imported.front().vertices.back().localPosition.x, 12.0), "vertex local x imported");
    Require(Near(imported.front().vertices.back().localPosition.z, 34.0), "vertex local z imported");
    Require(imported.front().includedTerrainNames.size() == 2, "included terrain names imported");
    Require(imported.front().includedTerrainNames.back() == "Terrain B", "included terrain name value imported");

    std::filesystem::remove(path);
}

void TestIsolineGeneration()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::TerrainIsolineSettings settings;
    settings.autoInterval = false;
    settings.contourIntervalMeters = 10.0;
    settings.resolutionX = 8;
    settings.resolutionZ = 8;
    settings.opacity = 0.5f;
    const auto isolines = GeoFPS::GenerateTerrainIsolines(grid, settings);
    Require(!isolines.empty(), "isolines should be generated");
    bool foundTenMeterLine = false;
    for (const GeoFPS::TerrainIsolineSegment& segment : isolines)
    {
        if (Near(segment.levelHeight, 10.0))
        {
            foundTenMeterLine = true;
        }
        Require(Near(segment.color.a, 0.5), "isoline opacity should be applied");
    }
    Require(foundTenMeterLine, "10 meter contour should exist");
}

void TestIsolineSampleGridCache()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::TerrainIsolineSettings settings;
    settings.autoInterval = false;
    settings.contourIntervalMeters = 10.0;
    settings.resolutionX = 8;
    settings.resolutionZ = 8;
    settings.opacity = 0.6f;

    const GeoFPS::TerrainIsolineSampleGrid sampleGrid = GeoFPS::BuildTerrainIsolineSampleGrid(grid, settings);
    Require(sampleGrid.IsValid(), "isoline sample grid should be valid");
    Require(sampleGrid.heights.size() == 64, "sample grid should cache all requested heights");
    Require(Near(sampleGrid.minHeight, 0.0), "sample grid min height");
    Require(Near(sampleGrid.maxHeight, 30.0), "sample grid max height");

    const auto isolines = GeoFPS::GenerateTerrainIsolinesFromSampleGrid(sampleGrid, settings);
    Require(!isolines.empty(), "cached sample grid should generate isolines");
    for (const GeoFPS::TerrainIsolineSegment& segment : isolines)
    {
        Require(Near(segment.color.a, 0.6), "cached-grid isoline opacity should be applied");
    }
}

void TestIsolineResolution()
{
    GeoFPS::TerrainHeightGrid grid;
    Require(grid.Build(TestGrid()), "grid should build");
    GeoFPS::TerrainIsolineSettings lowSettings;
    lowSettings.autoInterval = false;
    lowSettings.contourIntervalMeters = 5.0;
    lowSettings.resolutionX = 8;
    lowSettings.resolutionZ = 8;
    GeoFPS::TerrainIsolineSettings highSettings = lowSettings;
    highSettings.resolutionX = 32;
    highSettings.resolutionZ = 32;

    const auto low = GeoFPS::GenerateTerrainIsolines(grid, lowSettings);
    const auto high = GeoFPS::GenerateTerrainIsolines(grid, highSettings);
    Require(high.size() >= low.size(), "higher isoline resolution should not produce fewer segments on simple slope");
}

void TestIsolineColorRamp()
{
    const glm::vec4 low = GeoFPS::IsolineColorForHeight(0.0, 0.0, 100.0, 0.75f);
    const glm::vec4 mid = GeoFPS::IsolineColorForHeight(50.0, 0.0, 100.0, 0.75f);
    const glm::vec4 high = GeoFPS::IsolineColorForHeight(100.0, 0.0, 100.0, 0.75f);
    Require(low.b > low.r, "low isoline should be cooler");
    Require(mid.g > mid.b, "mid isoline should be yellow/green dominant");
    Require(high.r > high.g, "high isoline should be warmer");
    Require(Near(high.a, 0.75), "isoline alpha should match opacity");
}

void TestSunPositionCalculations()
{
    GeoFPS::GeoReference reference;
    reference.originLatitude = 55.6761;
    reference.originLongitude = 12.5683;

    GeoFPS::SunSettings summer;
    summer.useGeographicSun = true;
    summer.year = 2026;
    summer.month = 6;
    summer.day = 21;
    summer.localTimeHours = 12.0f;
    summer.utcOffsetHours = 2.0f;
    summer.illuminance = 1.0f;

    GeoFPS::SunSettings winter = summer;
    winter.month = 12;
    winter.day = 21;
    winter.utcOffsetHours = 1.0f;

    const GeoFPS::ApplicationInternal::SunParameters summerSun =
        GeoFPS::ApplicationInternal::ComputeSunParameters(reference, summer);
    const GeoFPS::ApplicationInternal::SunParameters winterSun =
        GeoFPS::ApplicationInternal::ComputeSunParameters(reference, winter);

    Require(summerSun.elevationDegrees > winterSun.elevationDegrees, "summer sun should be higher than winter sun in Copenhagen");
    Require(summerSun.intensity > winterSun.intensity, "summer sun should be brighter than winter sun in Copenhagen");

    GeoFPS::SunSettings manual;
    manual.useGeographicSun = false;
    manual.manualAzimuthDegrees = 135.0f;
    manual.manualElevationDegrees = 25.0f;
    manual.ambientStrength = 0.25f;
    const GeoFPS::ApplicationInternal::SunParameters manualSun =
        GeoFPS::ApplicationInternal::ComputeSunParameters(reference, manual);
    Require(Near(manualSun.azimuthDegrees, 135.0), "manual sun azimuth should be preserved");
    Require(Near(manualSun.elevationDegrees, 25.0), "manual sun elevation should be preserved");
}

void TestTerrainMeshGenerationLargeRaster()
{
    std::vector<GeoFPS::TerrainPoint> points;
    constexpr int inputResolution = 50;
    points.reserve(static_cast<size_t>(inputResolution * inputResolution));
    for (int latIndex = 0; latIndex < inputResolution; ++latIndex)
    {
        for (int lonIndex = 0; lonIndex < inputResolution; ++lonIndex)
        {
            const double latitude = static_cast<double>(latIndex) * 0.0001;
            const double longitude = static_cast<double>(lonIndex) * 0.0001;
            const double height = 100.0 + (static_cast<double>(latIndex) * 1.5) + (static_cast<double>(lonIndex) * 0.75);
            points.push_back({latitude, longitude, height});
        }
    }

    GeoFPS::GeoReference reference;
    reference.originLatitude = points.front().latitude;
    reference.originLongitude = points.front().longitude;
    reference.originHeight = points.front().height;
    GeoFPS::GeoConverter converter(reference);

    GeoFPS::TerrainBuildSettings settings;
    settings.gridResolutionX = 64;
    settings.gridResolutionZ = 64;
    settings.heightScale = 1.25f;

    GeoFPS::TerrainMeshBuilder builder;
    const GeoFPS::MeshData meshData = builder.BuildFromGeographicPoints(points, converter, settings);
    Require(meshData.vertices.size() == static_cast<size_t>(settings.gridResolutionX * settings.gridResolutionZ),
            "large raster mesh vertex count");
    Require(meshData.indices.size() == static_cast<size_t>((settings.gridResolutionX - 1) * (settings.gridResolutionZ - 1) * 6),
            "large raster mesh index count");

    float minHeight = std::numeric_limits<float>::max();
    float maxHeight = std::numeric_limits<float>::lowest();
    for (const GeoFPS::Vertex& vertex : meshData.vertices)
    {
        Require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z),
                "large raster mesh positions should be finite");
        Require(std::isfinite(vertex.normal.x) && std::isfinite(vertex.normal.y) && std::isfinite(vertex.normal.z),
                "large raster mesh normals should be finite");
        minHeight = std::min(minHeight, vertex.position.y);
        maxHeight = std::max(maxHeight, vertex.position.y);
    }

    Require(maxHeight > minHeight, "large raster mesh should preserve terrain relief");

    settings.chunkResolution = 16;
    const std::vector<GeoFPS::TerrainMeshChunkData> chunks =
        builder.BuildChunksFromGeographicPoints(points, converter, settings);
    Require(!chunks.empty(), "large raster mesh should produce chunks");
    for (const GeoFPS::TerrainMeshChunkData& chunk : chunks)
    {
        Require(!chunk.meshData.vertices.empty(), "chunk should contain vertices");
        Require(!chunk.meshData.indices.empty(), "chunk should contain indices");
        Require(chunk.maxX >= chunk.minX && chunk.maxZ >= chunk.minZ, "chunk bounds should be ordered");
    }
}

void TestWorldFileParsing()
{
    std::istringstream input(R"world(
# GeoFPS world file
world_name=Parser Test World
active_terrain_index=1
active_asset_index=0
sun.use_geographic=0
sun.year=2026
sun.month=12
sun.day=21
sun.local_time_hours=14.5
sun.utc_offset_hours=1
sun.illuminance=1.35
sun.ambient_strength=0.31
sun.sky_brightness=0.82
sun.manual_azimuth_degrees=181
sun.manual_elevation_degrees=22

[terrain]
name=Everest
tile_manifest=assets/datasets/nepal_everest_demo/terrain_tiles/tile_manifest.json
path=assets/datasets/nepal_everest_demo/terrain/everest_terrain.csv
visible=1
grid_x=128
grid_z=96
height_scale=1.75
import_sample_step=4
chunk_resolution=32
color_by_height=1
auto_height_color_range=0
height_color_min=10
height_color_max=100
low_height_color_r=0.1
low_height_color_g=0.2
low_height_color_b=0.3
mid_height_color_r=0.4
mid_height_color_g=0.5
mid_height_color_b=0.6
high_height_color_r=0.7
high_height_color_g=0.8
high_height_color_b=0.9
crs=EPSG:3857
origin_latitude=27.9881
origin_longitude=86.9250
origin_height=5364
active_overlay_index=0
  [overlay]
  name=Satellite
  image_path=assets/datasets/nepal_everest_demo/overlays/satellite.jpg
  enabled=1
  opacity=0.66
  top_left_latitude=28.1
  top_left_longitude=86.8
  top_right_latitude=28.1
  top_right_longitude=87.0
  bottom_left_latitude=27.9
  bottom_left_longitude=86.8
  bottom_right_latitude=27.9
  bottom_right_longitude=87.0
  [/overlay]
[/terrain]

[terrain]
name=Second Terrain
path=assets/data/sample_terrain.csv
visible=0
coordinate_mode=local_meters
[/terrain]

[asset]
name=Tower A
path=assets/blender/tower.glb
position_mode=geographic
latitude=55.6761
longitude=12.5683
height=14
position_x=1
position_y=2
position_z=3
rotation_z=45
scale_x=2
scale_y=3
scale_z=4
tint_r=0.8
tint_g=0.7
tint_b=0.6
[/asset]

[terrain_profile]
name=Local Profile
visible=1
show_in_world=1
use_local_coordinates=1
color_r=0.1
color_g=0.2
color_b=0.3
color_a=0.4
thickness=5
world_thickness_m=6
sample_spacing_m=7
terrain=Everest
  [vertex]
  latitude=27.99
  longitude=86.93
  auxiliary=1
  local_x=10
  local_y=20
  local_z=30
  [/vertex]
[/terrain_profile]
)world");

    GeoFPS::ParsedWorldFile world;
    std::string error;
    Require(GeoFPS::ParseWorldFile(input, world, error), "world file parser should succeed: " + error);
    Require(world.worldName == "Parser Test World", "world name parsed");
    Require(world.activeTerrainIndex == 1, "active terrain index parsed");
    Require(!world.sunSettings.useGeographicSun, "manual sun mode parsed");
    Require(world.sunSettings.month == 12, "sun month parsed");
    Require(Near(world.sunSettings.localTimeHours, 14.5), "sun local time parsed");
    Require(Near(world.sunSettings.illuminance, 1.35), "sun illuminance parsed");
    Require(world.terrains.size() == 2, "terrain count parsed");
    Require(world.terrains.front().name == "Everest", "terrain name parsed");
    Require(world.terrains.front().hasTileManifest, "terrain tile manifest flag parsed");
    Require(world.terrains.front().tileManifestPath ==
                "assets/datasets/nepal_everest_demo/terrain_tiles/tile_manifest.json",
            "terrain tile manifest path parsed");
    Require(world.terrains.front().settings.gridResolutionX == 128, "terrain grid x parsed");
    Require(Near(world.terrains.front().settings.heightScale, 1.75), "terrain height scale parsed");
    Require(world.terrains.front().settings.importSampleStep == 4, "terrain import sample step parsed");
    Require(world.terrains.front().settings.chunkResolution == 32, "terrain chunk resolution parsed");
    Require(world.terrains.front().settings.colorByHeight, "terrain height coloring parsed");
    Require(!world.terrains.front().settings.autoHeightColorRange, "terrain manual height color range parsed");
    Require(Near(world.terrains.front().settings.heightColorMin, 10.0), "terrain height color min parsed");
    Require(Near(world.terrains.front().settings.heightColorMax, 100.0), "terrain height color max parsed");
    Require(Near(world.terrains.front().settings.lowHeightColor.g, 0.2), "terrain low height color parsed");
    Require(Near(world.terrains.front().settings.midHeightColor.b, 0.6), "terrain mid height color parsed");
    Require(Near(world.terrains.front().settings.highHeightColor.r, 0.7), "terrain high height color parsed");
    Require(world.terrains.front().settings.crs.kind == GeoFPS::CrsKind::WebMercator, "terrain CRS parsed");
    Require(world.terrains.front().settings.coordinateMode == GeoFPS::TerrainCoordinateMode::Geographic,
            "default terrain coordinate mode parsed");
    Require(Near(world.terrains.front().geoReference.originLongitude, 86.9250), "terrain origin longitude parsed");
    Require(world.terrains.front().overlays.size() == 1, "overlay count parsed");
    Require(world.terrains.front().overlays.front().image.enabled, "overlay enabled parsed");
    Require(Near(world.terrains.front().overlays.front().image.opacity, 0.66), "overlay opacity parsed");
    Require(!world.terrains.back().visible, "terrain visibility parsed");
    Require(world.terrains.back().settings.coordinateMode == GeoFPS::TerrainCoordinateMode::LocalMeters,
            "local meters terrain coordinate mode parsed");
    Require(world.assets.size() == 1, "world asset count parsed");
    Require(world.assets.front().useGeographicPlacement, "asset geographic placement parsed");
    Require(Near(world.assets.front().rotationDegrees.z, 45.0), "asset z rotation parsed");
    Require(Near(world.assets.front().scale.z, 4.0), "asset z scale parsed");
    Require(Near(world.assets.front().tint.g, 0.7), "asset tint parsed");
    Require(world.profiles.size() == 1, "profile count parsed");
    Require(world.profiles.front().useLocalCoordinates, "profile local coordinate mode parsed");
    Require(world.profiles.front().vertices.size() == 1, "profile vertex parsed");
    Require(world.profiles.front().vertices.front().auxiliary, "profile auxiliary vertex parsed");
    Require(Near(world.profiles.front().vertices.front().localPosition.x, 10.0), "profile local x parsed");
    Require(Near(world.profiles.front().vertices.front().localPosition.z, 30.0), "profile local z parsed");
}

void TestExternalBlenderAssetTextParsing()
{
    std::istringstream input(R"assets(
# External GeoFPS blender assets file
[asset]
name=Local Tower
path=assets/blender/local_tower.glb
position_mode=local
position_x=10
position_y=20
position_z=30
rotation_z=15
scale_x=1.1
scale_y=1.2
scale_z=1.3
tint_r=0.9
tint_g=0.8
tint_b=0.7
[/asset]

[asset]
name=Copenhagen Tower
path=assets/blender/copenhagen_tower.glb
position_mode=geographic
latitude=55.6761
longitude=12.5683
height=42
scale_x=2
scale_y=2
scale_z=2
[/asset]
)assets");

    std::vector<GeoFPS::ParsedAssetDefinition> assets;
    std::string error;
    Require(GeoFPS::ParseBlenderAssetList(input, assets, error), "external blender asset parser should succeed: " + error);
    Require(assets.size() == 2, "external asset count parsed");
    Require(assets.front().name == "Local Tower", "local asset name parsed");
    Require(!assets.front().useGeographicPlacement, "local asset placement mode parsed");
    Require(Near(assets.front().position.x, 10.0), "local asset x parsed");
    Require(Near(assets.front().rotationDegrees.z, 15.0), "local asset z rotation parsed");
    Require(Near(assets.front().scale.y, 1.2), "local asset y scale parsed");
    Require(Near(assets.front().tint.b, 0.7), "local asset tint parsed");
    Require(assets.back().useGeographicPlacement, "geographic asset placement mode parsed");
    Require(Near(assets.back().latitude, 55.6761), "geographic asset latitude parsed");
    Require(Near(assets.back().longitude, 12.5683), "geographic asset longitude parsed");
    Require(Near(assets.back().height, 42.0), "geographic asset height parsed");

    std::istringstream emptyInput("# no assets\n");
    assets.clear();
    error.clear();
    Require(!GeoFPS::ParseBlenderAssetList(emptyInput, assets, error), "empty external asset file should fail");
    Require(!error.empty(), "empty external asset file should provide an error");
}

void TestWorldFileSchemaDiagnostics()
{
    std::istringstream missingTerrainPath(R"world(
[terrain]
name=Broken Terrain
[/terrain]
)world");

    GeoFPS::ParsedWorldFile world;
    const GeoFPS::WorldFileParseResult worldResult = GeoFPS::ParseWorldFile(missingTerrainPath, world);
    Require(!worldResult.success, "world parser should fail when terrain path is missing");
    Require(worldResult.ErrorMessage().find("Line 2") != std::string::npos, "world parser error should include line number");
    Require(worldResult.ErrorMessage().find("path") != std::string::npos, "world parser error should mention missing path");

    std::istringstream localAssetWithGeoFields(R"assets(
[asset]
name=Local With Geo Fields
path=assets/tower.glb
position_mode=local
latitude=55.6761
longitude=12.5683
[/asset]
)assets");

    std::vector<GeoFPS::ParsedAssetDefinition> assets;
    const GeoFPS::WorldFileParseResult warningResult = GeoFPS::ParseBlenderAssetList(localAssetWithGeoFields, assets);
    Require(warningResult.success, "local asset with geographic fields should parse with warning");
    bool foundWarning = false;
    for (const GeoFPS::WorldFileParseDiagnostic& diagnostic : warningResult.diagnostics)
    {
        foundWarning = foundWarning || (diagnostic.warning && diagnostic.message.find("geographic fields") != std::string::npos);
    }
    Require(foundWarning, "local asset with geographic fields should emit warning");

    std::istringstream missingGeoFields(R"assets(
[asset]
name=Geographic Without Coordinates
path=assets/tower.glb
position_mode=geographic
[/asset]
)assets");

    assets.clear();
    const GeoFPS::WorldFileParseResult errorResult = GeoFPS::ParseBlenderAssetList(missingGeoFields, assets);
    Require(!errorResult.success, "geographic asset without coordinates should fail");
    Require(errorResult.ErrorMessage().find("Line 2") != std::string::npos, "asset parser error should include block line number");
    Require(errorResult.ErrorMessage().find("latitude") != std::string::npos, "asset parser error should mention latitude");
}

} // namespace

int main()
{
    TestTerrainCsvLoading();
    TestTerrainCsvDecimation();
    TestSparseTerrainHeightGridDoesNotReturnSeaLevel();
    TestTerrainTileManifestParsing();
    TestGeoConverterRoundTrip();
    TestWebMercatorCrsConversion();
    TestGridInterpolation();
    TestSingleLineSampling();
    TestOutOfBoundsProfileSampling();
    TestLocalCoordinateProfileSampling();
    TestLocalMetersProfileSampling();
    TestLineAngles();
    TestLocalMetersTerrainMeshGeneration();
    TestGeographicTerrainMeshUsesLocalHeights();
    TestPolylineSampling();
    TestProfileExportImport();
    TestIsolineGeneration();
    TestIsolineSampleGridCache();
    TestIsolineResolution();
    TestIsolineColorRamp();
    TestSunPositionCalculations();
    TestTerrainMeshGenerationLargeRaster();
    TestWorldFileParsing();
    TestExternalBlenderAssetTextParsing();
    TestWorldFileSchemaDiagnostics();
    std::cout << "TerrainProfileTests passed\n";
    return 0;
}
