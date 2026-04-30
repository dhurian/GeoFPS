#include "Core/Application.h"
#include "Core/ApplicationInternal.h"
#include "Core/WorldFileParser.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace GeoFPS
{
using namespace ApplicationInternal;

namespace
{
const char* CoordinateModeName(TerrainCoordinateMode mode)
{
    if (mode == TerrainCoordinateMode::LocalMeters)
    {
        return "local_meters";
    }
    if (mode == TerrainCoordinateMode::Projected)
    {
        return "projected";
    }
    return "geographic";
}

SunSettings ToApplicationSunSettings(const ParsedSunSettings& parsed)
{
    SunSettings settings;
    settings.useGeographicSun = parsed.useGeographicSun;
    settings.year = parsed.year;
    settings.month = parsed.month;
    settings.day = parsed.day;
    settings.localTimeHours = parsed.localTimeHours;
    settings.utcOffsetHours = parsed.utcOffsetHours;
    settings.illuminance = parsed.illuminance;
    settings.ambientStrength = parsed.ambientStrength;
    settings.skyBrightness = parsed.skyBrightness;
    settings.manualAzimuthDegrees = parsed.manualAzimuthDegrees;
    settings.manualElevationDegrees = parsed.manualElevationDegrees;
    return settings;
}

SkySettings ToApplicationSkySettings(const ParsedSkySettings& parsed)
{
    SkySettings settings;
    settings.enabled           = parsed.enabled;
    settings.useSunDrivenColor = parsed.useSunDrivenColor;
    settings.zenithColor       = {parsed.zenithR,  parsed.zenithG,  parsed.zenithB};
    settings.horizonColor      = {parsed.horizonR, parsed.horizonG, parsed.horizonB};
    settings.horizonSharpness  = parsed.horizonSharpness;
    settings.showSunDisk       = parsed.showSunDisk;
    settings.sunDiskSize       = parsed.sunDiskSize;
    settings.sunDiskIntensity  = parsed.sunDiskIntensity;
    settings.cloudsEnabled     = parsed.cloudsEnabled;
    settings.cloudCoverage     = parsed.cloudCoverage;
    settings.cloudDensity      = parsed.cloudDensity;
    settings.cloudScale        = parsed.cloudScale;
    settings.cloudSpeedX       = parsed.cloudSpeedX;
    settings.cloudSpeedY       = parsed.cloudSpeedY;
    settings.cloudAltitude     = parsed.cloudAltitude;
    settings.cloudAutoColor    = parsed.cloudAutoColor;
    settings.cloudColor        = {parsed.cloudColorR, parsed.cloudColorG, parsed.cloudColorB};
    return settings;
}

OverlayEntry ToOverlayEntry(const ParsedOverlayDefinition& parsed)
{
    OverlayEntry overlay;
    overlay.name = parsed.name;
    overlay.image = parsed.image;
    return overlay;
}

TerrainDataset ToTerrainDataset(const ParsedTerrainDefinition& parsed)
{
    TerrainDataset terrain;
    terrain.name = parsed.name;
    terrain.path = parsed.path;
    terrain.tileManifestPath = parsed.tileManifestPath;
    terrain.hasTileManifest = parsed.hasTileManifest && !parsed.tileManifestPath.empty();
    terrain.visible = parsed.visible;
    terrain.settings = parsed.settings;
    terrain.geoReference = parsed.geoReference;
    terrain.activeOverlayIndex = parsed.activeOverlayIndex;
    terrain.overlays.reserve(parsed.overlays.size());
    for (const ParsedOverlayDefinition& overlay : parsed.overlays)
    {
        terrain.overlays.push_back(ToOverlayEntry(overlay));
    }
    return terrain;
}

ImportedAsset ToImportedAsset(const ParsedAssetDefinition& parsed)
{
    ImportedAsset asset;
    asset.name = parsed.name;
    asset.path = parsed.path;
    asset.useGeographicPlacement = parsed.useGeographicPlacement;
    asset.latitude = parsed.latitude;
    asset.longitude = parsed.longitude;
    asset.height = parsed.height;
    asset.position = parsed.position;
    asset.rotationDegrees = parsed.rotationDegrees;
    asset.scale = parsed.scale;
    asset.tint = parsed.tint;
    asset.showLabel = parsed.showLabel;
    return asset;
}
} // namespace

bool Application::SaveWorldToFile(const std::string& path)
{
    const std::string targetPathString = Trim(path);
    if (targetPathString.empty())
    {
        m_StatusMessage = "World file path is empty.";
        return false;
    }

    std::error_code errorCode;
    const std::filesystem::path targetPath(targetPathString);
    if (targetPath.has_parent_path())
    {
        std::filesystem::create_directories(targetPath.parent_path(), errorCode);
        if (errorCode)
        {
            m_StatusMessage = "Failed to create world file folder: " + targetPath.parent_path().string();
            return false;
        }
    }

    std::ofstream file(targetPath);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to save world file: " + targetPathString;
        return false;
    }

    file << "# GeoFPS world file\n";
    file << std::fixed << std::setprecision(8);
    file << "world_name=" << m_WorldName << '\n';
    file << "active_terrain_index=" << m_ActiveTerrainIndex << '\n';
    file << "active_asset_index=" << m_ActiveImportedAssetIndex << '\n';
    file << "sun.use_geographic=" << (m_SunSettings.useGeographicSun ? 1 : 0) << '\n';
    file << "sun.year=" << m_SunSettings.year << '\n';
    file << "sun.month=" << m_SunSettings.month << '\n';
    file << "sun.day=" << m_SunSettings.day << '\n';
    file << "sun.local_time_hours=" << m_SunSettings.localTimeHours << '\n';
    file << "sun.utc_offset_hours=" << m_SunSettings.utcOffsetHours << '\n';
    file << "sun.illuminance=" << m_SunSettings.illuminance << '\n';
    file << "sun.ambient_strength=" << m_SunSettings.ambientStrength << '\n';
    file << "sun.sky_brightness=" << m_SunSettings.skyBrightness << '\n';
    file << "sun.manual_azimuth_degrees=" << m_SunSettings.manualAzimuthDegrees << '\n';
    file << "sun.manual_elevation_degrees=" << m_SunSettings.manualElevationDegrees << '\n';
    file << "sky.enabled="            << (m_SkySettings.enabled           ? 1 : 0) << '\n';
    file << "sky.use_sun_color="      << (m_SkySettings.useSunDrivenColor ? 1 : 0) << '\n';
    file << "sky.zenith_r="           << m_SkySettings.zenithColor.r      << '\n';
    file << "sky.zenith_g="           << m_SkySettings.zenithColor.g      << '\n';
    file << "sky.zenith_b="           << m_SkySettings.zenithColor.b      << '\n';
    file << "sky.horizon_r="          << m_SkySettings.horizonColor.r     << '\n';
    file << "sky.horizon_g="          << m_SkySettings.horizonColor.g     << '\n';
    file << "sky.horizon_b="          << m_SkySettings.horizonColor.b     << '\n';
    file << "sky.horizon_sharpness="  << m_SkySettings.horizonSharpness   << '\n';
    file << "sky.show_sun_disk="      << (m_SkySettings.showSunDisk       ? 1 : 0) << '\n';
    file << "sky.sun_disk_size="      << m_SkySettings.sunDiskSize        << '\n';
    file << "sky.sun_disk_intensity=" << m_SkySettings.sunDiskIntensity   << '\n';
    file << "sky.clouds_enabled="    << (m_SkySettings.cloudsEnabled     ? 1 : 0) << '\n';
    file << "sky.cloud_coverage="    << m_SkySettings.cloudCoverage      << '\n';
    file << "sky.cloud_density="     << m_SkySettings.cloudDensity       << '\n';
    file << "sky.cloud_scale="       << m_SkySettings.cloudScale         << '\n';
    file << "sky.cloud_speed_x="     << m_SkySettings.cloudSpeedX        << '\n';
    file << "sky.cloud_speed_y="     << m_SkySettings.cloudSpeedY        << '\n';
    file << "sky.cloud_altitude="    << m_SkySettings.cloudAltitude      << '\n';
    file << "sky.cloud_auto_color="  << (m_SkySettings.cloudAutoColor    ? 1 : 0) << '\n';
    file << "sky.cloud_color_r="     << m_SkySettings.cloudColor.r       << '\n';
    file << "sky.cloud_color_g="     << m_SkySettings.cloudColor.g       << '\n';
    file << "sky.cloud_color_b="     << m_SkySettings.cloudColor.b       << "\n\n";

    for (const TerrainDataset& terrain : m_TerrainDatasets)
    {
        file << "[terrain]\n";
        file << "name=" << terrain.name << '\n';
        if (terrain.hasTileManifest && !terrain.tileManifestPath.empty())
        {
            file << "tile_manifest=" << terrain.tileManifestPath << '\n';
        }
        file << "path=" << terrain.path << '\n';
        file << "visible=" << (terrain.visible ? 1 : 0) << '\n';
        file << "grid_x=" << terrain.settings.gridResolutionX << '\n';
        file << "grid_z=" << terrain.settings.gridResolutionZ << '\n';
        file << "height_scale=" << terrain.settings.heightScale << '\n';
        file << "smoothing_passes=" << terrain.settings.smoothingPasses << '\n';
        file << "import_sample_step=" << terrain.settings.importSampleStep << '\n';
        file << "chunk_resolution=" << terrain.settings.chunkResolution << '\n';
        file << "color_by_height=" << (terrain.settings.colorByHeight ? 1 : 0) << '\n';
        file << "auto_height_color_range=" << (terrain.settings.autoHeightColorRange ? 1 : 0) << '\n';
        file << "height_color_min=" << terrain.settings.heightColorMin << '\n';
        file << "height_color_max=" << terrain.settings.heightColorMax << '\n';
        file << "low_height_color_r=" << terrain.settings.lowHeightColor.r << '\n';
        file << "low_height_color_g=" << terrain.settings.lowHeightColor.g << '\n';
        file << "low_height_color_b=" << terrain.settings.lowHeightColor.b << '\n';
        file << "mid_height_color_r=" << terrain.settings.midHeightColor.r << '\n';
        file << "mid_height_color_g=" << terrain.settings.midHeightColor.g << '\n';
        file << "mid_height_color_b=" << terrain.settings.midHeightColor.b << '\n';
        file << "high_height_color_r=" << terrain.settings.highHeightColor.r << '\n';
        file << "high_height_color_g=" << terrain.settings.highHeightColor.g << '\n';
        file << "high_height_color_b=" << terrain.settings.highHeightColor.b << '\n';
        file << "coordinate_mode=" << CoordinateModeName(terrain.settings.coordinateMode) << '\n';
        file << "crs=" << terrain.settings.crs.id << '\n';
        file << "crs_false_easting=" << terrain.settings.crs.falseEasting << '\n';
        file << "crs_false_northing=" << terrain.settings.crs.falseNorthing << '\n';
        file << "origin_latitude=" << terrain.geoReference.originLatitude << '\n';
        file << "origin_longitude=" << terrain.geoReference.originLongitude << '\n';
        file << "origin_height=" << terrain.geoReference.originHeight << '\n';
        file << "active_overlay_index=" << terrain.activeOverlayIndex << '\n';

        for (const OverlayEntry& overlay : terrain.overlays)
        {
            file << "  [overlay]\n";
            file << "  name=" << overlay.name << '\n';
            file << "  image_path=" << overlay.image.imagePath << '\n';
            file << "  enabled=" << (overlay.image.enabled ? 1 : 0) << '\n';
            file << "  opacity=" << overlay.image.opacity << '\n';
            file << "  top_left_latitude=" << overlay.image.topLeft.latitude << '\n';
            file << "  top_left_longitude=" << overlay.image.topLeft.longitude << '\n';
            file << "  top_right_latitude=" << overlay.image.topRight.latitude << '\n';
            file << "  top_right_longitude=" << overlay.image.topRight.longitude << '\n';
            file << "  bottom_left_latitude=" << overlay.image.bottomLeft.latitude << '\n';
            file << "  bottom_left_longitude=" << overlay.image.bottomLeft.longitude << '\n';
            file << "  bottom_right_latitude=" << overlay.image.bottomRight.latitude << '\n';
            file << "  bottom_right_longitude=" << overlay.image.bottomRight.longitude << '\n';
            file << "  [/overlay]\n";
        }

        file << "[/terrain]\n\n";
    }

    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        file << "[asset]\n";
        file << "name=" << asset.name << '\n';
        file << "path=" << asset.path << '\n';
        file << "position_mode=" << (asset.useGeographicPlacement ? "geographic" : "local") << '\n';
        file << "latitude=" << asset.latitude << '\n';
        file << "longitude=" << asset.longitude << '\n';
        file << "height=" << asset.height << '\n';
        file << "position_x=" << asset.position.x << '\n';
        file << "position_y=" << asset.position.y << '\n';
        file << "position_z=" << asset.position.z << '\n';
        file << "rotation_z=" << asset.rotationDegrees.z << '\n';
        file << "scale_x=" << asset.scale.x << '\n';
        file << "scale_y=" << asset.scale.y << '\n';
        file << "scale_z=" << asset.scale.z << '\n';
        file << "tint_r=" << asset.tint.r << '\n';
        file << "tint_g=" << asset.tint.g << '\n';
        file << "tint_b=" << asset.tint.b << '\n';
        file << "show_label=" << (asset.showLabel ? 1 : 0) << '\n';
        file << "[/asset]\n\n";
    }

    for (const TerrainProfile& profile : m_TerrainProfiles)
    {
        file << "[terrain_profile]\n";
        file << "name=" << profile.name << '\n';
        file << "visible=" << (profile.visible ? 1 : 0) << '\n';
        file << "show_in_world=" << (profile.showInWorld ? 1 : 0) << '\n';
        file << "color_r=" << profile.color.r << '\n';
        file << "color_g=" << profile.color.g << '\n';
        file << "color_b=" << profile.color.b << '\n';
        file << "color_a=" << profile.color.a << '\n';
        file << "thickness=" << profile.thickness << '\n';
        file << "world_thickness_m=" << profile.worldThicknessMeters << '\n';
        file << "world_ground_offset_m=" << profile.worldGroundOffsetMeters << '\n';
        file << "sample_spacing_m=" << profile.sampleSpacingMeters << '\n';
        file << "use_local_coordinates=" << (profile.useLocalCoordinates ? 1 : 0) << '\n';
        for (const std::string& terrainName : profile.includedTerrainNames)
        {
            file << "terrain=" << terrainName << '\n';
        }
        for (const TerrainProfileVertex& vertex : profile.vertices)
        {
            file << "  [vertex]\n";
            file << "  latitude=" << vertex.latitude << '\n';
            file << "  longitude=" << vertex.longitude << '\n';
            file << "  auxiliary=" << (vertex.auxiliary ? 1 : 0) << '\n';
            file << "  local_x=" << vertex.localPosition.x << '\n';
            file << "  local_y=" << vertex.localPosition.y << '\n';
            file << "  local_z=" << vertex.localPosition.z << '\n';
            file << "  [/vertex]\n";
        }
        file << "[/terrain_profile]\n\n";
    }

    file.flush();
    if (!file.good())
    {
        m_StatusMessage = "Failed while writing world file: " + targetPathString;
        return false;
    }

    m_WorldFilePath = targetPathString;
    m_StatusMessage = "Saved world file: " + targetPathString;
    return true;
}

bool Application::LoadWorldFromFile(const std::string& path)
{
    const std::string sourcePathString = Trim(path);
    if (sourcePathString.empty())
    {
        m_StatusMessage = "World file path is empty.";
        return false;
    }

    std::error_code existsError;
    if (!std::filesystem::exists(sourcePathString, existsError))
    {
        m_StatusMessage = "World file does not exist: " + sourcePathString;
        return false;
    }

    std::ifstream file(sourcePathString);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to load world file: " + sourcePathString;
        return false;
    }

    std::cout << "[GeoFPS] Loading world file: " << sourcePathString << '\n';

    ParsedWorldFile parsedWorld;
    const WorldFileParseResult parseResult = ParseWorldFile(file, parsedWorld);
    for (const WorldFileParseDiagnostic& diagnostic : parseResult.diagnostics)
    {
        if (diagnostic.warning)
        {
            std::cout << "[GeoFPS] World parse warning";
            if (diagnostic.lineNumber > 0)
            {
                std::cout << " line " << diagnostic.lineNumber;
            }
            std::cout << ": " << diagnostic.message << '\n';
        }
    }
    if (!parseResult.success)
    {
        const std::string parseError = parseResult.ErrorMessage();
        m_StatusMessage = parseError.empty() ? "Failed to parse world file: " + sourcePathString
                                             : parseError + " " + sourcePathString;
        return false;
    }

    std::vector<TerrainDataset> terrains;
    terrains.reserve(parsedWorld.terrains.size());
    for (const ParsedTerrainDefinition& parsedTerrain : parsedWorld.terrains)
    {
        std::cout << "[GeoFPS] Parsed terrain '" << parsedTerrain.name << "' path='" << parsedTerrain.path
                  << "' tile_manifest='" << parsedTerrain.tileManifestPath << "' with "
                  << parsedTerrain.overlays.size() << " overlay definition(s)\n";
        terrains.push_back(ToTerrainDataset(parsedTerrain));
    }

    std::vector<ImportedAsset> assets;
    assets.reserve(parsedWorld.assets.size());
    for (const ParsedAssetDefinition& parsedAsset : parsedWorld.assets)
    {
        std::cout << "[GeoFPS] Parsed asset '" << parsedAsset.name << "' path='" << parsedAsset.path << "'\n";
        assets.push_back(ToImportedAsset(parsedAsset));
    }

    std::vector<TerrainProfile> profiles = std::move(parsedWorld.profiles);
    const int activeTerrainIndex = parsedWorld.activeTerrainIndex;
    const int activeAssetIndex = parsedWorld.activeAssetIndex;
    const SunSettings loadedSun = ToApplicationSunSettings(parsedWorld.sunSettings);
    const SkySettings loadedSky = ToApplicationSkySettings(parsedWorld.skySettings);
    const std::string loadedWorldName = parsedWorld.worldName.empty() ? m_WorldName : parsedWorld.worldName;

    std::cout << "[GeoFPS] Parsed " << terrains.size() << " terrain dataset(s), " << assets.size()
              << " asset definition(s), and " << profiles.size() << " terrain profile(s)\n";

    for (TerrainDataset& terrain : terrains)
    {
        if (terrain.overlays.empty())
        {
            terrain.overlays.push_back(OverlayEntry {});
        }
        terrain.activeOverlayIndex = std::clamp(terrain.activeOverlayIndex, 0, static_cast<int>(terrain.overlays.size()) - 1);
        terrain.loaded = false;
        terrain.points.clear();
        for (TerrainTile& tile : terrain.tiles)
        {
            tile.loaded = false;
            tile.meshLoaded = false;
            tile.points.clear();
            tile.chunks.clear();
        }
    }

    m_WorldName = loadedWorldName;
    m_WorldFilePath = sourcePathString;
    m_SunSettings = loadedSun;
    m_SkySettings = loadedSky;
    m_TerrainDatasets = std::move(terrains);
    m_ImportedAssets = std::move(assets);
    if (!profiles.empty())
    {
        m_TerrainProfiles = std::move(profiles);
    }

    m_ActiveTerrainIndex = std::clamp(activeTerrainIndex, 0, static_cast<int>(m_TerrainDatasets.size()) - 1);
    std::cout << "[GeoFPS] Activating terrain index " << m_ActiveTerrainIndex << '\n';
    if (!ActivateTerrainDataset(m_ActiveTerrainIndex))
    {
        std::cout << "[GeoFPS] Failed to activate terrain index " << m_ActiveTerrainIndex << '\n';
        return false;
    }
    for (int terrainIndex = 0; terrainIndex < static_cast<int>(m_TerrainDatasets.size()); ++terrainIndex)
    {
        if (terrainIndex == m_ActiveTerrainIndex)
        {
            continue;
        }
        TerrainDataset& terrain = m_TerrainDatasets[static_cast<size_t>(terrainIndex)];
        if (!terrain.visible)
        {
            continue;
        }
        if ((terrain.loaded || LoadTerrainDataset(terrain)) && !terrain.mesh)
        {
            RebuildTerrainMesh(terrain);
        }
    }
    for (TerrainDataset& terrain : m_TerrainDatasets)
    {
        for (OverlayEntry& overlay : terrain.overlays)
        {
            if (terrain.visible && overlay.image.enabled)
            {
                LoadOverlayImage(overlay);
            }
        }
    }

    for (ImportedAsset& asset : m_ImportedAssets)
    {
        std::cout << "[GeoFPS] Loading asset '" << asset.name << "' from " << asset.path << '\n';
        if (asset.useGeographicPlacement)
        {
            UpdateImportedAssetPositionFromGeographic(asset);
            std::cout << "[GeoFPS]  geographic placement lat=" << asset.latitude << " lon=" << asset.longitude
                      << " height=" << asset.height << '\n';
        }
        if (!asset.path.empty())
        {
            LoadImportedAsset(asset);
        }
    }

    if (m_ImportedAssets.empty())
    {
        ImportedAsset asset;
        asset.name = "Asset 1";
        m_ImportedAssets.push_back(std::move(asset));
    }

    m_ActiveImportedAssetIndex = std::clamp(activeAssetIndex, 0, static_cast<int>(m_ImportedAssets.size()) - 1);
    RebuildAllTerrainProfileSamples();
    m_MouseCaptured = true;
    m_Window.SetCursorCaptured(true);
    m_FPSController.ResetMouseState();
    m_FPSController.SetEnabled(true);
    std::cout << "[GeoFPS] World load complete. Active asset index " << m_ActiveImportedAssetIndex << '\n';
    m_StatusMessage = "Loaded world file: " + sourcePathString;
    return true;
}

bool Application::LoadBlenderAssetsFromFile(const std::string& path)
{
    if (path.empty())
    {
        m_StatusMessage = "Blender assets file path is empty.";
        return false;
    }

    std::error_code existsError;
    if (!std::filesystem::exists(path, existsError))
    {
        m_StatusMessage = "Blender assets file does not exist: " + path;
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to load blender assets file: " + path;
        return false;
    }

    std::cout << "[GeoFPS] Importing blender assets from file: " << path << '\n';

    std::vector<ParsedAssetDefinition> parsedAssets;
    const WorldFileParseResult parseResult = ParseBlenderAssetList(file, parsedAssets);
    for (const WorldFileParseDiagnostic& diagnostic : parseResult.diagnostics)
    {
        if (diagnostic.warning)
        {
            std::cout << "[GeoFPS] Blender asset import warning";
            if (diagnostic.lineNumber > 0)
            {
                std::cout << " line " << diagnostic.lineNumber;
            }
            std::cout << ": " << diagnostic.message << '\n';
        }
    }
    if (!parseResult.success)
    {
        const std::string parseError = parseResult.ErrorMessage();
        m_StatusMessage = parseError.empty() ? "No blender assets found in file: " + path : parseError + " " + path;
        return false;
    }

    std::vector<ImportedAsset> loadedAssets;
    loadedAssets.reserve(parsedAssets.size());
    for (const ParsedAssetDefinition& parsedAsset : parsedAssets)
    {
        ImportedAsset asset = ToImportedAsset(parsedAsset);
        std::cout << "[GeoFPS] Finalizing asset '" << asset.name << "' path='" << asset.path << "'\n";
        if (asset.useGeographicPlacement)
        {
            UpdateImportedAssetPositionFromGeographic(asset);
            std::cout << "[GeoFPS]  geographic placement lat=" << asset.latitude
                      << " lon=" << asset.longitude << " height=" << asset.height << '\n';
        }
        LoadImportedAsset(asset);
        loadedAssets.push_back(std::move(asset));
    }

    if (loadedAssets.empty())
    {
        m_StatusMessage = "No blender assets found in file: " + path;
        return false;
    }

    std::cout << "[GeoFPS] Parsed " << loadedAssets.size() << " blender asset definition(s)\n";

    for (ImportedAsset& asset : m_ImportedAssets)
    {
        asset.selected = false;
    }

    for (ImportedAsset& asset : loadedAssets)
    {
        asset.selected = true;
        m_ImportedAssets.push_back(std::move(asset));
    }

    m_ActiveImportedAssetIndex = static_cast<int>(m_ImportedAssets.size()) - 1;
    m_BlenderAssetsFilePath = path;
    std::cout << "[GeoFPS] Blender asset import complete. Active asset index " << m_ActiveImportedAssetIndex << '\n';
    m_StatusMessage = "Loaded blender assets file: " + path;
    return true;
}

bool Application::WriteCurrentWorldReadout(const std::string& path) const
{
    if (path.empty())
    {
        return false;
    }

    std::error_code errorCode;
    const std::filesystem::path targetPath(path);
    if (targetPath.has_parent_path())
    {
        std::filesystem::create_directories(targetPath.parent_path(), errorCode);
    }

    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    file << "GeoFPS Current World Readout\n";
    file << "World Name: " << m_WorldName << "\n";
    file << "World File: " << m_WorldFilePath << "\n";
    file << "Terrain Dataset Count: " << m_TerrainDatasets.size() << "\n";
    file << "Imported Asset Count: " << m_ImportedAssets.size() << "\n";
    file << "Terrain Profile Count: " << m_TerrainProfiles.size() << "\n";
    file << "Active Terrain Index: " << m_ActiveTerrainIndex << "\n";
    file << "Active Asset Index: " << m_ActiveImportedAssetIndex << "\n\n";

    file << "Sun Settings\n";
    file << "  Geographic Sun: " << (m_SunSettings.useGeographicSun ? "true" : "false") << "\n";
    file << "  Date: " << m_SunSettings.year << "-" << m_SunSettings.month << "-" << m_SunSettings.day << "\n";
    file << "  Local Time Hours: " << m_SunSettings.localTimeHours << "\n";
    file << "  UTC Offset Hours: " << m_SunSettings.utcOffsetHours << "\n";
    file << "  Illuminance: " << m_SunSettings.illuminance << "\n";
    file << "  Ambient Strength: " << m_SunSettings.ambientStrength << "\n";
    file << "  Sky Brightness: " << m_SunSettings.skyBrightness << "\n\n";

    file << "Terrain Datasets\n";
    for (size_t terrainIndex = 0; terrainIndex < m_TerrainDatasets.size(); ++terrainIndex)
    {
        const TerrainDataset& terrain = m_TerrainDatasets[terrainIndex];
        file << "- Terrain " << terrainIndex << ": " << terrain.name << "\n";
        file << "  Path: " << terrain.path << "\n";
        if (terrain.hasTileManifest && !terrain.tileManifestPath.empty())
        {
            file << "  Tile Manifest: " << terrain.tileManifestPath << "\n";
            file << "  Tile Count: " << terrain.tiles.size() << "\n";
        }
        file << "  Origin: lat " << terrain.geoReference.originLatitude << ", lon " << terrain.geoReference.originLongitude
             << ", height " << terrain.geoReference.originHeight << "\n";
        file << "  Grid: " << terrain.settings.gridResolutionX << " x " << terrain.settings.gridResolutionZ << "\n";
        file << "  Height Scale: " << terrain.settings.heightScale << "\n";
        file << "  Smoothing Passes: " << terrain.settings.smoothingPasses << "\n";
        file << "  Import Sample Step: " << terrain.settings.importSampleStep << "\n";
        file << "  Chunk Resolution: " << terrain.settings.chunkResolution << "\n";
        file << "  Color By Height: " << (terrain.settings.colorByHeight ? "true" : "false") << "\n";
        file << "  Coordinate Mode: " << CoordinateModeName(terrain.settings.coordinateMode) << "\n";
        file << "  CRS: " << terrain.settings.crs.id << "\n";
        file << "  Overlay Count: " << terrain.overlays.size() << "\n";
    }

    file << "\nImported Assets\n";
    for (size_t assetIndex = 0; assetIndex < m_ImportedAssets.size(); ++assetIndex)
    {
        const ImportedAsset& asset = m_ImportedAssets[assetIndex];
        file << "- Asset " << assetIndex << ": " << asset.name << "\n";
        file << "  Path: " << asset.path << "\n";
        file << "  Placement Mode: " << (asset.useGeographicPlacement ? "geographic" : "local") << "\n";
        if (asset.useGeographicPlacement)
        {
            file << "  Geographic: lat " << asset.latitude << ", lon " << asset.longitude << ", height " << asset.height << "\n";
        }
        file << "  Local Position: " << asset.position.x << ", " << asset.position.y << ", " << asset.position.z << "\n";
        file << "  Rotation Z: " << asset.rotationDegrees.z << "\n";
        file << "  Scale: " << asset.scale.x << ", " << asset.scale.y << ", " << asset.scale.z << "\n";
        file << "  Tint: " << asset.tint.r << ", " << asset.tint.g << ", " << asset.tint.b << "\n";
        file << "  Loaded: " << (asset.loaded ? "true" : "false") << "\n";
    }

    file << "\nTerrain Profiles\n";
    for (size_t profileIndex = 0; profileIndex < m_TerrainProfiles.size(); ++profileIndex)
    {
        const TerrainProfile& profile = m_TerrainProfiles[profileIndex];
        file << "- Profile " << profileIndex << ": " << profile.name << "\n";
        file << "  Vertices: " << profile.vertices.size() << "\n";
        file << "  Samples: " << profile.samples.size() << "\n";
        file << "  Sample Spacing Meters: " << profile.sampleSpacingMeters << "\n";
        file << "  Visible: " << (profile.visible ? "true" : "false") << "\n";
    }

    return true;
}

bool Application::ExportTerrainProfileFile(const std::string& path)
{
    const std::string targetPath = Trim(path);
    if (targetPath.empty())
    {
        m_StatusMessage = "Terrain profile export path is empty.";
        return false;
    }

    std::cout << "[GeoFPS] Exporting " << m_TerrainProfiles.size() << " terrain profile(s) to " << targetPath << '\n';
    if (!ExportTerrainProfiles(targetPath, m_TerrainProfiles))
    {
        m_StatusMessage = "Failed to export terrain profiles: " + targetPath;
        std::cout << "[GeoFPS] Terrain profile export failed: " << targetPath << '\n';
        return false;
    }

    m_TerrainProfileFilePath = targetPath;
    m_StatusMessage = "Exported terrain profiles: " + targetPath;
    std::cout << "[GeoFPS] Terrain profile export complete: " << targetPath << '\n';
    return true;
}

bool Application::ImportTerrainProfileFile(const std::string& path)
{
    const std::string sourcePath = Trim(path);
    if (sourcePath.empty())
    {
        m_StatusMessage = "Terrain profile import path is empty.";
        return false;
    }

    std::cout << "[GeoFPS] Importing terrain profiles from " << sourcePath << '\n';
    std::vector<TerrainProfile> importedProfiles;
    std::string errorMessage;
    if (!ImportTerrainProfiles(sourcePath, importedProfiles, errorMessage))
    {
        m_StatusMessage = errorMessage.empty() ? "Failed to import terrain profiles: " + sourcePath : errorMessage;
        std::cout << "[GeoFPS] Terrain profile import failed: " << m_StatusMessage << '\n';
        return false;
    }

    m_TerrainProfiles = std::move(importedProfiles);
    RebuildAllTerrainProfileSamples();
    m_ActiveTerrainProfileIndex = std::clamp(m_ActiveTerrainProfileIndex, 0, static_cast<int>(m_TerrainProfiles.size()) - 1);
    m_SelectedProfileVertexIndex = -1;
    m_SelectedProfileSampleIndex = -1;
    m_TerrainProfileFilePath = sourcePath;
    m_StatusMessage = "Imported terrain profiles: " + sourcePath;
    std::cout << "[GeoFPS] Terrain profile import complete: " << m_TerrainProfiles.size() << " profile(s)\n";
    return true;
}


} // namespace GeoFPS
