#include "Core/WorldFileParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace GeoFPS
{
namespace
{
std::string Trim(const std::string& value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (begin >= end)
    {
        return {};
    }
    return std::string(begin, end);
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

TerrainCoordinateMode ParseCoordinateMode(const std::string& value)
{
    const std::string normalized = ToLower(Trim(value));
    if (normalized == "local_meters" || normalized == "local")
    {
        return TerrainCoordinateMode::LocalMeters;
    }
    if (normalized == "projected")
    {
        return TerrainCoordinateMode::Projected;
    }
    return TerrainCoordinateMode::Geographic;
}

bool ParseBool(const std::string& value, bool fallback = false)
{
    const std::string normalized = ToLower(Trim(value));
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
    {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
    {
        return false;
    }
    return fallback;
}

double ParseDouble(const std::string& value, double fallback = 0.0)
{
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    return end == value.c_str() ? fallback : parsed;
}

int ParseInt(const std::string& value, int fallback = 0)
{
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? fallback : static_cast<int>(parsed);
}

void AddDiagnostic(WorldFileParseResult& result, int lineNumber, std::string message, bool warning = false)
{
    result.diagnostics.push_back({lineNumber, std::move(message), warning});
}

std::string FormatDiagnostic(const WorldFileParseDiagnostic& diagnostic)
{
    std::ostringstream message;
    if (diagnostic.lineNumber > 0)
    {
        message << "Line " << diagnostic.lineNumber << ": ";
    }
    if (diagnostic.warning)
    {
        message << "Warning: ";
    }
    message << diagnostic.message;
    return message.str();
}

bool HasError(const WorldFileParseResult& result)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const WorldFileParseDiagnostic& diagnostic) {
        return !diagnostic.warning;
    });
}

void ApplyAssetValue(ParsedAssetDefinition& asset, const std::string& key, const std::string& value)
{
    if (key == "name") asset.name = value;
    else if (key == "path") asset.path = value;
    else if (key == "position_mode")
    {
        asset.hasPositionMode = true;
        asset.useGeographicPlacement = ToLower(value) == "geographic";
    }
    else if (key == "latitude")
    {
        asset.hasLatitude = true;
        asset.latitude = ParseDouble(value);
    }
    else if (key == "longitude")
    {
        asset.hasLongitude = true;
        asset.longitude = ParseDouble(value);
    }
    else if (key == "height")
    {
        asset.hasHeight = true;
        asset.height = ParseDouble(value);
    }
    else if (key == "position_x")
    {
        asset.hasLocalPosition = true;
        asset.position.x = static_cast<float>(ParseDouble(value));
    }
    else if (key == "position_y")
    {
        asset.hasLocalPosition = true;
        asset.position.y = static_cast<float>(ParseDouble(value));
    }
    else if (key == "position_z")
    {
        asset.hasLocalPosition = true;
        asset.position.z = static_cast<float>(ParseDouble(value));
    }
    else if (key == "rotation_z") asset.rotationDegrees.z = static_cast<float>(ParseDouble(value));
    else if (key == "scale_x") asset.scale.x = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "scale_y") asset.scale.y = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "scale_z") asset.scale.z = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "tint_r") asset.tint.r = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "tint_g") asset.tint.g = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "tint_b") asset.tint.b = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "show_label")   asset.showLabel    = ParseBool(value, asset.showLabel);
    else if (key == "anim_clip")         asset.animClipName    = value;
    else if (key == "anim_speed")        asset.animSpeed       = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "anim_loop")         asset.animLoop        = ParseBool(value, true);
    else if (key == "anim_playing")      asset.animPlaying     = ParseBool(value, false);
    else if (key == "node_anim_speed")   asset.nodeAnimSpeed   = static_cast<float>(ParseDouble(value, 1.0));
    else if (key == "node_anim_loop")    asset.nodeAnimLoop    = ParseBool(value, true);
    else if (key == "node_anim_playing") asset.nodeAnimPlaying = ParseBool(value, false);
}

void ValidateAsset(const ParsedAssetDefinition& asset,
                   WorldFileParseResult& result,
                   int lineNumber,
                   const std::string& context)
{
    if (asset.path.empty())
    {
        AddDiagnostic(result, lineNumber, context + " is missing required field 'path'.");
    }

    if (asset.useGeographicPlacement)
    {
        if (!asset.hasLatitude || !asset.hasLongitude)
        {
            AddDiagnostic(result, lineNumber, context + " uses geographic placement but is missing latitude or longitude.");
        }
    }
    else if (asset.hasLatitude || asset.hasLongitude || asset.hasHeight)
    {
        AddDiagnostic(result,
                      lineNumber,
                      context + " has geographic fields but position_mode is local; latitude/longitude/height will be ignored.",
                      true);
    }

    if (!asset.useGeographicPlacement && !asset.hasLocalPosition)
    {
        AddDiagnostic(result,
                      lineNumber,
                      context + " has no local position fields; it will be placed at local origin.",
                      true);
    }
}
} // namespace

std::string WorldFileParseResult::ErrorMessage() const
{
    for (const WorldFileParseDiagnostic& diagnostic : diagnostics)
    {
        if (!diagnostic.warning)
        {
            return FormatDiagnostic(diagnostic);
        }
    }
    return diagnostics.empty() ? std::string() : FormatDiagnostic(diagnostics.front());
}

WorldFileParseResult ParseWorldFile(std::istream& input, ParsedWorldFile& world)
{
    world = ParsedWorldFile {};
    WorldFileParseResult result;

    ParsedTerrainDefinition* currentTerrain = nullptr;
    ParsedOverlayDefinition* currentOverlay = nullptr;
    ParsedAssetDefinition* currentAsset = nullptr;
    TerrainProfile* currentProfile = nullptr;
    TerrainProfileVertex currentProfileVertex {};
    int currentAssetStartLine = 0;
    int currentTerrainStartLine = 0;
    int currentOverlayStartLine = 0;
    int currentProfileStartLine = 0;
    int currentVertexStartLine = 0;
    bool insideProfileVertex = false;

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed == "[terrain]")
        {
            if (currentTerrain != nullptr || currentAsset != nullptr || currentProfile != nullptr)
            {
                AddDiagnostic(result, lineNumber, "Nested terrain blocks are not allowed.");
                continue;
            }
            world.terrains.emplace_back();
            currentTerrain = &world.terrains.back();
            currentTerrainStartLine = lineNumber;
            currentOverlay = nullptr;
            currentAsset = nullptr;
            currentProfile = nullptr;
            insideProfileVertex = false;
            continue;
        }
        if (trimmed == "[/terrain]")
        {
            if (currentTerrain == nullptr)
            {
                AddDiagnostic(result, lineNumber, "Unexpected [/terrain] block end.");
                continue;
            }
            if ((!currentTerrain->hasPath || currentTerrain->path.empty()) &&
                (!currentTerrain->hasTileManifest || currentTerrain->tileManifestPath.empty()))
            {
                AddDiagnostic(result,
                              currentTerrainStartLine,
                              "Terrain block is missing required field 'path' or 'tile_manifest'.");
            }
            currentTerrain = nullptr;
            currentOverlay = nullptr;
            continue;
        }
        if (trimmed == "[overlay]" && currentTerrain != nullptr)
        {
            currentTerrain->overlays.emplace_back();
            currentOverlay = &currentTerrain->overlays.back();
            currentOverlayStartLine = lineNumber;
            currentAsset = nullptr;
            currentProfile = nullptr;
            insideProfileVertex = false;
            continue;
        }
        if (trimmed == "[/overlay]")
        {
            if (currentOverlay == nullptr)
            {
                AddDiagnostic(result, lineNumber, "Unexpected [/overlay] block end.");
                continue;
            }
            if (currentOverlay->image.enabled && (!currentOverlay->hasImagePath || currentOverlay->image.imagePath.empty()))
            {
                AddDiagnostic(result, currentOverlayStartLine, "Enabled overlay is missing required field 'image_path'.");
            }
            currentOverlay = nullptr;
            continue;
        }
        if (trimmed == "[asset]")
        {
            if (currentTerrain != nullptr || currentAsset != nullptr || currentProfile != nullptr)
            {
                AddDiagnostic(result, lineNumber, "Nested asset blocks are not allowed.");
                continue;
            }
            world.assets.emplace_back();
            currentAsset = &world.assets.back();
            currentAssetStartLine = lineNumber;
            currentTerrain = nullptr;
            currentOverlay = nullptr;
            currentProfile = nullptr;
            insideProfileVertex = false;
            continue;
        }
        if (trimmed == "[/asset]")
        {
            if (currentAsset == nullptr)
            {
                AddDiagnostic(result, lineNumber, "Unexpected [/asset] block end.");
                continue;
            }
            ValidateAsset(*currentAsset, result, currentAssetStartLine, "Asset block");
            currentAsset = nullptr;
            continue;
        }
        if (trimmed == "[terrain_profile]")
        {
            if (currentTerrain != nullptr || currentAsset != nullptr || currentProfile != nullptr)
            {
                AddDiagnostic(result, lineNumber, "Nested terrain_profile blocks are not allowed.");
                continue;
            }
            world.profiles.emplace_back();
            currentProfile = &world.profiles.back();
            currentProfileStartLine = lineNumber;
            currentTerrain = nullptr;
            currentOverlay = nullptr;
            currentAsset = nullptr;
            insideProfileVertex = false;
            continue;
        }
        if (trimmed == "[/terrain_profile]")
        {
            if (currentProfile == nullptr)
            {
                AddDiagnostic(result, lineNumber, "Unexpected [/terrain_profile] block end.");
                continue;
            }
            if (insideProfileVertex)
            {
                AddDiagnostic(result, currentVertexStartLine, "Profile vertex block is missing [/vertex].");
                insideProfileVertex = false;
            }
            if (currentProfile->vertices.empty())
            {
                AddDiagnostic(result, currentProfileStartLine, "Terrain profile has no vertices.", true);
            }
            currentProfile = nullptr;
            insideProfileVertex = false;
            continue;
        }
        if (trimmed == "[vertex]" && currentProfile != nullptr)
        {
            if (insideProfileVertex)
            {
                AddDiagnostic(result, lineNumber, "Nested profile vertex blocks are not allowed.");
                continue;
            }
            currentProfileVertex = TerrainProfileVertex {};
            currentVertexStartLine = lineNumber;
            insideProfileVertex = true;
            continue;
        }
        if (trimmed == "[/vertex]" && currentProfile != nullptr && insideProfileVertex)
        {
            currentProfile->vertices.push_back(currentProfileVertex);
            insideProfileVertex = false;
            continue;
        }

        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            AddDiagnostic(result, lineNumber, "Expected key=value entry.");
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));

        if (currentOverlay != nullptr)
        {
            if (key == "name") currentOverlay->name = value;
            else if (key == "image_path")
            {
                currentOverlay->hasImagePath = true;
                currentOverlay->image.imagePath = value;
            }
            else if (key == "enabled") currentOverlay->image.enabled = ParseBool(value, currentOverlay->image.enabled);
            else if (key == "opacity") currentOverlay->image.opacity = static_cast<float>(ParseDouble(value, currentOverlay->image.opacity));
            else if (key == "top_left_latitude") currentOverlay->image.topLeft.latitude = ParseDouble(value);
            else if (key == "top_left_longitude") currentOverlay->image.topLeft.longitude = ParseDouble(value);
            else if (key == "top_right_latitude") currentOverlay->image.topRight.latitude = ParseDouble(value);
            else if (key == "top_right_longitude") currentOverlay->image.topRight.longitude = ParseDouble(value);
            else if (key == "bottom_left_latitude") currentOverlay->image.bottomLeft.latitude = ParseDouble(value);
            else if (key == "bottom_left_longitude") currentOverlay->image.bottomLeft.longitude = ParseDouble(value);
            else if (key == "bottom_right_latitude") currentOverlay->image.bottomRight.latitude = ParseDouble(value);
            else if (key == "bottom_right_longitude") currentOverlay->image.bottomRight.longitude = ParseDouble(value);
            continue;
        }

        if (currentTerrain != nullptr)
        {
            if (key == "name") currentTerrain->name = value;
            else if (key == "path")
            {
                currentTerrain->hasPath = true;
                currentTerrain->path = value;
            }
            else if (key == "tile_manifest")
            {
                currentTerrain->hasTileManifest = true;
                currentTerrain->tileManifestPath = value;
            }
            else if (key == "visible") currentTerrain->visible = ParseBool(value, currentTerrain->visible);
            else if (key == "grid_x") currentTerrain->settings.gridResolutionX = ParseInt(value, currentTerrain->settings.gridResolutionX);
            else if (key == "grid_z") currentTerrain->settings.gridResolutionZ = ParseInt(value, currentTerrain->settings.gridResolutionZ);
            else if (key == "height_scale") currentTerrain->settings.heightScale = static_cast<float>(ParseDouble(value, currentTerrain->settings.heightScale));
            else if (key == "smoothing_passes") currentTerrain->settings.smoothingPasses = ParseInt(value, currentTerrain->settings.smoothingPasses);
            else if (key == "import_sample_step") currentTerrain->settings.importSampleStep = ParseInt(value, currentTerrain->settings.importSampleStep);
            else if (key == "chunk_resolution") currentTerrain->settings.chunkResolution = ParseInt(value, currentTerrain->settings.chunkResolution);
            else if (key == "color_by_height") currentTerrain->settings.colorByHeight = ParseBool(value, currentTerrain->settings.colorByHeight);
            else if (key == "auto_height_color_range") currentTerrain->settings.autoHeightColorRange = ParseBool(value, currentTerrain->settings.autoHeightColorRange);
            else if (key == "height_color_min") currentTerrain->settings.heightColorMin = static_cast<float>(ParseDouble(value, currentTerrain->settings.heightColorMin));
            else if (key == "height_color_max") currentTerrain->settings.heightColorMax = static_cast<float>(ParseDouble(value, currentTerrain->settings.heightColorMax));
            else if (key == "low_height_color_r") currentTerrain->settings.lowHeightColor.r = static_cast<float>(ParseDouble(value, currentTerrain->settings.lowHeightColor.r));
            else if (key == "low_height_color_g") currentTerrain->settings.lowHeightColor.g = static_cast<float>(ParseDouble(value, currentTerrain->settings.lowHeightColor.g));
            else if (key == "low_height_color_b") currentTerrain->settings.lowHeightColor.b = static_cast<float>(ParseDouble(value, currentTerrain->settings.lowHeightColor.b));
            else if (key == "mid_height_color_r") currentTerrain->settings.midHeightColor.r = static_cast<float>(ParseDouble(value, currentTerrain->settings.midHeightColor.r));
            else if (key == "mid_height_color_g") currentTerrain->settings.midHeightColor.g = static_cast<float>(ParseDouble(value, currentTerrain->settings.midHeightColor.g));
            else if (key == "mid_height_color_b") currentTerrain->settings.midHeightColor.b = static_cast<float>(ParseDouble(value, currentTerrain->settings.midHeightColor.b));
            else if (key == "high_height_color_r") currentTerrain->settings.highHeightColor.r = static_cast<float>(ParseDouble(value, currentTerrain->settings.highHeightColor.r));
            else if (key == "high_height_color_g") currentTerrain->settings.highHeightColor.g = static_cast<float>(ParseDouble(value, currentTerrain->settings.highHeightColor.g));
            else if (key == "high_height_color_b") currentTerrain->settings.highHeightColor.b = static_cast<float>(ParseDouble(value, currentTerrain->settings.highHeightColor.b));
            else if (key == "coordinate_mode") currentTerrain->settings.coordinateMode = ParseCoordinateMode(value);
            else if (key == "crs") currentTerrain->settings.crs = GeoConverter::ParseCrs(value);
            else if (key == "crs_false_easting") currentTerrain->settings.crs.falseEasting = ParseDouble(value);
            else if (key == "crs_false_northing") currentTerrain->settings.crs.falseNorthing = ParseDouble(value);
            else if (key == "origin_latitude") currentTerrain->geoReference.originLatitude = ParseDouble(value);
            else if (key == "origin_longitude") currentTerrain->geoReference.originLongitude = ParseDouble(value);
            else if (key == "origin_height") currentTerrain->geoReference.originHeight = ParseDouble(value);
            else if (key == "active_overlay_index") currentTerrain->activeOverlayIndex = ParseInt(value, 0);
            continue;
        }

        if (currentAsset != nullptr)
        {
            ApplyAssetValue(*currentAsset, key, value);
            continue;
        }

        if (currentProfile != nullptr)
        {
            if (insideProfileVertex)
            {
                if (key == "latitude") currentProfileVertex.latitude = ParseDouble(value);
                else if (key == "longitude") currentProfileVertex.longitude = ParseDouble(value);
                else if (key == "auxiliary") currentProfileVertex.auxiliary = ParseBool(value, currentProfileVertex.auxiliary);
                else if (key == "local_x") currentProfileVertex.localPosition.x = ParseDouble(value);
                else if (key == "local_y") currentProfileVertex.localPosition.y = ParseDouble(value);
                else if (key == "local_z") currentProfileVertex.localPosition.z = ParseDouble(value);
                continue;
            }

            if (key == "name") currentProfile->name = value;
            else if (key == "visible") currentProfile->visible = ParseBool(value, currentProfile->visible);
            else if (key == "show_in_world") currentProfile->showInWorld = ParseBool(value, currentProfile->showInWorld);
            else if (key == "use_local_coordinates") currentProfile->useLocalCoordinates = ParseBool(value, currentProfile->useLocalCoordinates);
            else if (key == "terrain") currentProfile->includedTerrainNames.push_back(value);
            else if (key == "color_r") currentProfile->color.r = static_cast<float>(ParseDouble(value, currentProfile->color.r));
            else if (key == "color_g") currentProfile->color.g = static_cast<float>(ParseDouble(value, currentProfile->color.g));
            else if (key == "color_b") currentProfile->color.b = static_cast<float>(ParseDouble(value, currentProfile->color.b));
            else if (key == "color_a") currentProfile->color.a = static_cast<float>(ParseDouble(value, currentProfile->color.a));
            else if (key == "thickness") currentProfile->thickness = static_cast<float>(ParseDouble(value, currentProfile->thickness));
            else if (key == "world_thickness_m") currentProfile->worldThicknessMeters = static_cast<float>(ParseDouble(value, currentProfile->worldThicknessMeters));
            else if (key == "world_ground_offset_m") currentProfile->worldGroundOffsetMeters = static_cast<float>(ParseDouble(value, currentProfile->worldGroundOffsetMeters));
            else if (key == "sample_spacing_m") currentProfile->sampleSpacingMeters = static_cast<float>(ParseDouble(value, currentProfile->sampleSpacingMeters));
            continue;
        }

        if (key == "world_name") world.worldName = value;
        else if (key == "active_terrain_index") world.activeTerrainIndex = ParseInt(value, 0);
        else if (key == "active_asset_index") world.activeAssetIndex = ParseInt(value, 0);
        else if (key == "sun.use_geographic") world.sunSettings.useGeographicSun = ParseBool(value, world.sunSettings.useGeographicSun);
        else if (key == "sun.year") world.sunSettings.year = ParseInt(value, world.sunSettings.year);
        else if (key == "sun.month") world.sunSettings.month = ParseInt(value, world.sunSettings.month);
        else if (key == "sun.day") world.sunSettings.day = ParseInt(value, world.sunSettings.day);
        else if (key == "sun.local_time_hours") world.sunSettings.localTimeHours = static_cast<float>(ParseDouble(value, world.sunSettings.localTimeHours));
        else if (key == "sun.utc_offset_hours") world.sunSettings.utcOffsetHours = static_cast<float>(ParseDouble(value, world.sunSettings.utcOffsetHours));
        else if (key == "sun.illuminance") world.sunSettings.illuminance = static_cast<float>(ParseDouble(value, world.sunSettings.illuminance));
        else if (key == "sun.ambient_strength") world.sunSettings.ambientStrength = static_cast<float>(ParseDouble(value, world.sunSettings.ambientStrength));
        else if (key == "sun.sky_brightness") world.sunSettings.skyBrightness = static_cast<float>(ParseDouble(value, world.sunSettings.skyBrightness));
        else if (key == "sun.manual_azimuth_degrees") world.sunSettings.manualAzimuthDegrees = static_cast<float>(ParseDouble(value, world.sunSettings.manualAzimuthDegrees));
        else if (key == "sun.manual_elevation_degrees") world.sunSettings.manualElevationDegrees = static_cast<float>(ParseDouble(value, world.sunSettings.manualElevationDegrees));
        // Sky settings
        else if (key == "sky.enabled")            world.skySettings.enabled           = ParseBool(value, world.skySettings.enabled);
        else if (key == "sky.use_sun_color")      world.skySettings.useSunDrivenColor = ParseBool(value, world.skySettings.useSunDrivenColor);
        else if (key == "sky.zenith_r")           world.skySettings.zenithR           = static_cast<float>(ParseDouble(value, world.skySettings.zenithR));
        else if (key == "sky.zenith_g")           world.skySettings.zenithG           = static_cast<float>(ParseDouble(value, world.skySettings.zenithG));
        else if (key == "sky.zenith_b")           world.skySettings.zenithB           = static_cast<float>(ParseDouble(value, world.skySettings.zenithB));
        else if (key == "sky.horizon_r")          world.skySettings.horizonR          = static_cast<float>(ParseDouble(value, world.skySettings.horizonR));
        else if (key == "sky.horizon_g")          world.skySettings.horizonG          = static_cast<float>(ParseDouble(value, world.skySettings.horizonG));
        else if (key == "sky.horizon_b")          world.skySettings.horizonB          = static_cast<float>(ParseDouble(value, world.skySettings.horizonB));
        else if (key == "sky.horizon_sharpness")  world.skySettings.horizonSharpness  = static_cast<float>(ParseDouble(value, world.skySettings.horizonSharpness));
        else if (key == "sky.show_sun_disk")      world.skySettings.showSunDisk       = ParseBool(value, world.skySettings.showSunDisk);
        else if (key == "sky.sun_disk_size")      world.skySettings.sunDiskSize       = static_cast<float>(ParseDouble(value, world.skySettings.sunDiskSize));
        else if (key == "sky.sun_disk_intensity") world.skySettings.sunDiskIntensity  = static_cast<float>(ParseDouble(value, world.skySettings.sunDiskIntensity));
        // Cloud settings
        else if (key == "sky.clouds_enabled")   world.skySettings.cloudsEnabled  = ParseBool(value, world.skySettings.cloudsEnabled);
        else if (key == "sky.cloud_coverage")   world.skySettings.cloudCoverage  = static_cast<float>(ParseDouble(value, world.skySettings.cloudCoverage));
        else if (key == "sky.cloud_density")    world.skySettings.cloudDensity   = static_cast<float>(ParseDouble(value, world.skySettings.cloudDensity));
        else if (key == "sky.cloud_scale")      world.skySettings.cloudScale     = static_cast<float>(ParseDouble(value, world.skySettings.cloudScale));
        else if (key == "sky.cloud_speed_x")    world.skySettings.cloudSpeedX    = static_cast<float>(ParseDouble(value, world.skySettings.cloudSpeedX));
        else if (key == "sky.cloud_speed_y")    world.skySettings.cloudSpeedY    = static_cast<float>(ParseDouble(value, world.skySettings.cloudSpeedY));
        else if (key == "sky.cloud_altitude")   world.skySettings.cloudAltitude  = static_cast<float>(ParseDouble(value, world.skySettings.cloudAltitude));
        else if (key == "sky.cloud_auto_color") world.skySettings.cloudAutoColor = ParseBool(value, world.skySettings.cloudAutoColor);
        else if (key == "sky.cloud_color_r")    world.skySettings.cloudColorR    = static_cast<float>(ParseDouble(value, world.skySettings.cloudColorR));
        else if (key == "sky.cloud_color_g")    world.skySettings.cloudColorG    = static_cast<float>(ParseDouble(value, world.skySettings.cloudColorG));
        else if (key == "sky.cloud_color_b")    world.skySettings.cloudColorB    = static_cast<float>(ParseDouble(value, world.skySettings.cloudColorB));
    }

    if (world.terrains.empty())
    {
        AddDiagnostic(result, 0, "World file has no terrains.");
    }

    if (currentOverlay != nullptr)
    {
        AddDiagnostic(result, currentOverlayStartLine, "Overlay block is missing [/overlay].");
    }
    if (currentTerrain != nullptr)
    {
        AddDiagnostic(result, currentTerrainStartLine, "Terrain block is missing [/terrain].");
    }
    if (currentAsset != nullptr)
    {
        ValidateAsset(*currentAsset, result, currentAssetStartLine, "Asset block");
        AddDiagnostic(result, currentAssetStartLine, "Asset block is missing [/asset].");
    }
    if (currentProfile != nullptr)
    {
        AddDiagnostic(result, currentProfileStartLine, "Terrain profile block is missing [/terrain_profile].");
    }

    result.success = !HasError(result);
    return result;
}

WorldFileParseResult ParseBlenderAssetList(std::istream& input, std::vector<ParsedAssetDefinition>& assets)
{
    assets.clear();
    WorldFileParseResult result;

    ParsedAssetDefinition currentAsset;
    bool insideAssetBlock = false;
    int currentAssetStartLine = 0;
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line))
    {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed == "[asset]")
        {
            if (insideAssetBlock)
            {
                AddDiagnostic(result, lineNumber, "Nested asset blocks are not allowed.");
                continue;
            }
            currentAsset = ParsedAssetDefinition {};
            insideAssetBlock = true;
            currentAssetStartLine = lineNumber;
            continue;
        }

        if (trimmed == "[/asset]")
        {
            if (!insideAssetBlock)
            {
                AddDiagnostic(result, lineNumber, "Unexpected [/asset] block end.");
                continue;
            }
            ValidateAsset(currentAsset, result, currentAssetStartLine, "Asset block");
            if (!currentAsset.path.empty())
            {
                assets.push_back(currentAsset);
            }
            insideAssetBlock = false;
            continue;
        }

        if (!insideAssetBlock)
        {
            AddDiagnostic(result, lineNumber, "Content outside [asset] blocks is ignored.", true);
            continue;
        }

        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            AddDiagnostic(result, lineNumber, "Expected key=value entry inside asset block.");
            continue;
        }

        ApplyAssetValue(currentAsset, Trim(trimmed.substr(0, separator)), Trim(trimmed.substr(separator + 1)));
    }

    if (insideAssetBlock)
    {
        ValidateAsset(currentAsset, result, currentAssetStartLine, "Asset block");
        AddDiagnostic(result, currentAssetStartLine, "Asset block is missing [/asset].");
    }

    if (assets.empty())
    {
        AddDiagnostic(result, 0, "No blender assets found in file.");
    }

    result.success = !HasError(result);
    return result;
}

bool ParseWorldFile(std::istream& input, ParsedWorldFile& world, std::string& errorMessage)
{
    const WorldFileParseResult result = ParseWorldFile(input, world);
    errorMessage = result.ErrorMessage();
    return result.success;
}

bool ParseBlenderAssetList(std::istream& input, std::vector<ParsedAssetDefinition>& assets, std::string& errorMessage)
{
    const WorldFileParseResult result = ParseBlenderAssetList(input, assets);
    errorMessage = result.ErrorMessage();
    return result.success;
}

} // namespace GeoFPS
