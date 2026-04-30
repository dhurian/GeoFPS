#pragma once

#include "Mapping/GeoImage.h"
#include "Math/GeoConverter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include "Terrain/TerrainProfile.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace GeoFPS
{
struct ParsedSunSettings
{
    bool useGeographicSun {true};
    int year {2026};
    int month {4};
    int day {12};
    float localTimeHours {16.0f};
    float utcOffsetHours {2.0f};
    float illuminance {1.0f};
    float ambientStrength {0.22f};
    float skyBrightness {1.0f};
    float manualAzimuthDegrees {220.0f};
    float manualElevationDegrees {32.0f};
};

struct ParsedSkySettings
{
    bool  enabled           {true};
    bool  useSunDrivenColor {true};
    float zenithR  {0.10f};
    float zenithG  {0.20f};
    float zenithB  {0.50f};
    float horizonR {0.50f};
    float horizonG {0.70f};
    float horizonB {0.90f};
    float horizonSharpness  {4.0f};
    bool  showSunDisk       {true};
    float sunDiskSize       {0.008f};
    float sunDiskIntensity  {3.0f};
    // Clouds
    bool  cloudsEnabled     {false};
    float cloudCoverage     {0.45f};
    float cloudDensity      {0.85f};
    float cloudScale        {1.0f};
    float cloudSpeedX       {12.0f};   // m/s
    float cloudSpeedY       {5.0f};    // m/s
    float cloudAltitude     {1500.0f}; // metres above ground
    bool  cloudAutoColor    {true};
    float cloudColorR       {1.0f};
    float cloudColorG       {1.0f};
    float cloudColorB       {1.0f};
};

struct ParsedOverlayDefinition
{
    std::string name {"Overlay 1"};
    GeoImageDefinition image {};
    bool hasImagePath {false};
};

struct ParsedTerrainDefinition
{
    std::string name {"Terrain 1"};
    std::string path {"assets/data/sample_terrain.csv"};
    std::string tileManifestPath;
    bool hasPath {false};
    bool hasTileManifest {false};
    GeoReference geoReference {};
    TerrainBuildSettings settings {};
    std::vector<ParsedOverlayDefinition> overlays;
    bool visible {true};
    int activeOverlayIndex {0};
};

struct ParsedAssetDefinition
{
    std::string name {"Asset 1"};
    std::string path;
    bool useGeographicPlacement {false};
    bool hasPositionMode {false};
    bool hasLatitude {false};
    bool hasLongitude {false};
    bool hasHeight {false};
    bool hasLocalPosition {false};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDegrees {0.0f, 0.0f, 0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 tint {1.0f, 1.0f, 1.0f};
    bool showLabel {true};
};

struct ParsedWorldFile
{
    std::string worldName;
    int activeTerrainIndex {0};
    int activeAssetIndex {0};
    ParsedSunSettings sunSettings {};
    ParsedSkySettings skySettings {};
    std::vector<ParsedTerrainDefinition> terrains;
    std::vector<ParsedAssetDefinition> assets;
    std::vector<TerrainProfile> profiles;
};

struct WorldFileParseDiagnostic
{
    int lineNumber {0};
    std::string message;
    bool warning {false};
};

struct WorldFileParseResult
{
    bool success {false};
    std::vector<WorldFileParseDiagnostic> diagnostics;

    [[nodiscard]] std::string ErrorMessage() const;
};

[[nodiscard]] WorldFileParseResult ParseWorldFile(std::istream& input, ParsedWorldFile& world);
[[nodiscard]] WorldFileParseResult ParseBlenderAssetList(std::istream& input,
                                                         std::vector<ParsedAssetDefinition>& assets);
[[nodiscard]] bool ParseWorldFile(std::istream& input, ParsedWorldFile& world, std::string& errorMessage);
[[nodiscard]] bool ParseBlenderAssetList(std::istream& input,
                                         std::vector<ParsedAssetDefinition>& assets,
                                         std::string& errorMessage);

} // namespace GeoFPS
