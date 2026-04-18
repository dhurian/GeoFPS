#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace GeoFPS
{
using namespace ApplicationInternal;
bool Application::SaveWorldToFile(const std::string& path)
{
    if (path.empty())
    {
        m_StatusMessage = "World file path is empty.";
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
        m_StatusMessage = "Failed to save world file: " + path;
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
    file << "sun.manual_elevation_degrees=" << m_SunSettings.manualElevationDegrees << "\n\n";

    for (const TerrainDataset& terrain : m_TerrainDatasets)
    {
        file << "[terrain]\n";
        file << "name=" << terrain.name << '\n';
        file << "path=" << terrain.path << '\n';
        file << "grid_x=" << terrain.settings.gridResolutionX << '\n';
        file << "grid_z=" << terrain.settings.gridResolutionZ << '\n';
        file << "height_scale=" << terrain.settings.heightScale << '\n';
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
        file << "[/asset]\n\n";
    }

    m_StatusMessage = "Saved world file: " + path;
    return true;
}

bool Application::LoadWorldFromFile(const std::string& path)
{
    if (path.empty())
    {
        m_StatusMessage = "World file path is empty.";
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to load world file: " + path;
        return false;
    }

    std::cout << "[GeoFPS] Loading world file: " << path << '\n';

    std::vector<TerrainDataset> terrains;
    std::vector<ImportedAsset> assets;
    TerrainDataset* currentTerrain = nullptr;
    OverlayEntry* currentOverlay = nullptr;
    ImportedAsset* currentAsset = nullptr;
    int activeTerrainIndex = 0;
    int activeAssetIndex = 0;
    SunSettings loadedSun = m_SunSettings;
    std::string loadedWorldName = m_WorldName;

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        const bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed == "[terrain]")
        {
            terrains.emplace_back();
            currentTerrain = &terrains.back();
            currentOverlay = nullptr;
            currentAsset = nullptr;
            std::cout << "[GeoFPS] Line " << lineNumber << ": begin terrain block\n";
            continue;
        }
        if (trimmed == "[/terrain]")
        {
            if (currentTerrain != nullptr)
            {
                std::cout << "[GeoFPS] Line " << lineNumber << ": end terrain block '" << currentTerrain->name << "'\n";
            }
            currentTerrain = nullptr;
            currentOverlay = nullptr;
            continue;
        }
        if (trimmed == "[overlay]" && currentTerrain != nullptr)
        {
            currentTerrain->overlays.emplace_back();
            currentOverlay = &currentTerrain->overlays.back();
            currentAsset = nullptr;
            std::cout << "[GeoFPS] Line " << lineNumber << ": begin overlay block for terrain '" << currentTerrain->name << "'\n";
            continue;
        }
        if (trimmed == "[/overlay]")
        {
            if (currentOverlay != nullptr)
            {
                std::cout << "[GeoFPS] Line " << lineNumber << ": end overlay block '" << currentOverlay->name << "'\n";
            }
            currentOverlay = nullptr;
            continue;
        }
        if (trimmed == "[asset]")
        {
            assets.emplace_back();
            currentAsset = &assets.back();
            currentTerrain = nullptr;
            currentOverlay = nullptr;
            std::cout << "[GeoFPS] Line " << lineNumber << ": begin asset block\n";
            continue;
        }
        if (trimmed == "[/asset]")
        {
            if (currentAsset != nullptr)
            {
                std::cout << "[GeoFPS] Line " << lineNumber << ": end asset block '" << currentAsset->name
                          << "' path='" << currentAsset->path << "'\n";
            }
            currentAsset = nullptr;
            continue;
        }

        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));

        if (currentOverlay != nullptr && indented)
        {
            if (key == "name") currentOverlay->name = value;
            else if (key == "image_path") currentOverlay->image.imagePath = value;
            else if (key == "enabled") currentOverlay->image.enabled = ParseBool(value);
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
            else if (key == "path") currentTerrain->path = value;
            else if (key == "grid_x") currentTerrain->settings.gridResolutionX = ParseInt(value, currentTerrain->settings.gridResolutionX);
            else if (key == "grid_z") currentTerrain->settings.gridResolutionZ = ParseInt(value, currentTerrain->settings.gridResolutionZ);
            else if (key == "height_scale") currentTerrain->settings.heightScale = static_cast<float>(ParseDouble(value, currentTerrain->settings.heightScale));
            else if (key == "origin_latitude") currentTerrain->geoReference.originLatitude = ParseDouble(value);
            else if (key == "origin_longitude") currentTerrain->geoReference.originLongitude = ParseDouble(value);
            else if (key == "origin_height") currentTerrain->geoReference.originHeight = ParseDouble(value);
            else if (key == "active_overlay_index") currentTerrain->activeOverlayIndex = ParseInt(value, 0);
            continue;
        }

        if (currentAsset != nullptr)
        {
            if (key == "name") currentAsset->name = value;
            else if (key == "path") currentAsset->path = value;
            else if (key == "position_mode") currentAsset->useGeographicPlacement = ToLower(value) == "geographic";
            else if (key == "latitude") currentAsset->latitude = ParseDouble(value);
            else if (key == "longitude") currentAsset->longitude = ParseDouble(value);
            else if (key == "height") currentAsset->height = ParseDouble(value);
            else if (key == "position_x") currentAsset->position.x = static_cast<float>(ParseDouble(value));
            else if (key == "position_y") currentAsset->position.y = static_cast<float>(ParseDouble(value));
            else if (key == "position_z") currentAsset->position.z = static_cast<float>(ParseDouble(value));
            else if (key == "rotation_z") currentAsset->rotationDegrees.z = static_cast<float>(ParseDouble(value));
            else if (key == "scale_x") currentAsset->scale.x = static_cast<float>(ParseDouble(value, 1.0));
            else if (key == "scale_y") currentAsset->scale.y = static_cast<float>(ParseDouble(value, 1.0));
            else if (key == "scale_z") currentAsset->scale.z = static_cast<float>(ParseDouble(value, 1.0));
            else if (key == "tint_r") currentAsset->tint.r = static_cast<float>(ParseDouble(value, 1.0));
            else if (key == "tint_g") currentAsset->tint.g = static_cast<float>(ParseDouble(value, 1.0));
            else if (key == "tint_b") currentAsset->tint.b = static_cast<float>(ParseDouble(value, 1.0));
            continue;
        }

        if (key == "world_name") loadedWorldName = value;
        else if (key == "active_terrain_index") activeTerrainIndex = ParseInt(value, 0);
        else if (key == "active_asset_index") activeAssetIndex = ParseInt(value, 0);
        else if (key == "sun.use_geographic") loadedSun.useGeographicSun = ParseBool(value);
        else if (key == "sun.year") loadedSun.year = ParseInt(value, loadedSun.year);
        else if (key == "sun.month") loadedSun.month = ParseInt(value, loadedSun.month);
        else if (key == "sun.day") loadedSun.day = ParseInt(value, loadedSun.day);
        else if (key == "sun.local_time_hours") loadedSun.localTimeHours = static_cast<float>(ParseDouble(value, loadedSun.localTimeHours));
        else if (key == "sun.utc_offset_hours") loadedSun.utcOffsetHours = static_cast<float>(ParseDouble(value, loadedSun.utcOffsetHours));
        else if (key == "sun.illuminance") loadedSun.illuminance = static_cast<float>(ParseDouble(value, loadedSun.illuminance));
        else if (key == "sun.ambient_strength") loadedSun.ambientStrength = static_cast<float>(ParseDouble(value, loadedSun.ambientStrength));
        else if (key == "sun.sky_brightness") loadedSun.skyBrightness = static_cast<float>(ParseDouble(value, loadedSun.skyBrightness));
        else if (key == "sun.manual_azimuth_degrees") loadedSun.manualAzimuthDegrees = static_cast<float>(ParseDouble(value, loadedSun.manualAzimuthDegrees));
        else if (key == "sun.manual_elevation_degrees") loadedSun.manualElevationDegrees = static_cast<float>(ParseDouble(value, loadedSun.manualElevationDegrees));
    }

    if (terrains.empty())
    {
        m_StatusMessage = "World file has no terrains: " + path;
        return false;
    }

    std::cout << "[GeoFPS] Parsed " << terrains.size() << " terrain dataset(s) and " << assets.size() << " asset definition(s)\n";

    for (TerrainDataset& terrain : terrains)
    {
        if (terrain.overlays.empty())
        {
            terrain.overlays.push_back(OverlayEntry {});
        }
        terrain.loaded = false;
        terrain.points.clear();
    }

    m_WorldName = loadedWorldName;
    m_WorldFilePath = path;
    m_SunSettings = loadedSun;
    m_TerrainDatasets = std::move(terrains);
    m_ImportedAssets = std::move(assets);

    m_ActiveTerrainIndex = std::clamp(activeTerrainIndex, 0, static_cast<int>(m_TerrainDatasets.size()) - 1);
    std::cout << "[GeoFPS] Activating terrain index " << m_ActiveTerrainIndex << '\n';
    if (!ActivateTerrainDataset(m_ActiveTerrainIndex))
    {
        std::cout << "[GeoFPS] Failed to activate terrain index " << m_ActiveTerrainIndex << '\n';
        return false;
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
    std::cout << "[GeoFPS] World load complete. Active asset index " << m_ActiveImportedAssetIndex << '\n';
    m_StatusMessage = "Loaded world file: " + path;
    return true;
}

bool Application::LoadBlenderAssetsFromFile(const std::string& path)
{
    if (path.empty())
    {
        m_StatusMessage = "Blender assets file path is empty.";
        return false;
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        m_StatusMessage = "Failed to load blender assets file: " + path;
        return false;
    }

    std::cout << "[GeoFPS] Importing blender assets from file: " << path << '\n';

    ImportedAsset currentAsset;
    bool insideAssetBlock = false;
    std::vector<ImportedAsset> loadedAssets;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed == "[asset]")
        {
            currentAsset = ImportedAsset {};
            insideAssetBlock = true;
            std::cout << "[GeoFPS] Line " << lineNumber << ": begin blender asset block\n";
            continue;
        }

        if (trimmed == "[/asset]")
        {
            if (insideAssetBlock && !currentAsset.path.empty())
            {
                std::cout << "[GeoFPS] Finalizing asset '" << currentAsset.name << "' path='" << currentAsset.path << "'\n";
                if (currentAsset.useGeographicPlacement)
                {
                    UpdateImportedAssetPositionFromGeographic(currentAsset);
                    std::cout << "[GeoFPS]  geographic placement lat=" << currentAsset.latitude
                              << " lon=" << currentAsset.longitude << " height=" << currentAsset.height << '\n';
                }
                LoadImportedAsset(currentAsset);
                loadedAssets.push_back(std::move(currentAsset));
            }
            insideAssetBlock = false;
            continue;
        }

        if (!insideAssetBlock)
        {
            continue;
        }

        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos)
        {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));
        if (key == "name") currentAsset.name = value;
        else if (key == "path") currentAsset.path = value;
        else if (key == "position_mode") currentAsset.useGeographicPlacement = ToLower(value) == "geographic";
        else if (key == "latitude") currentAsset.latitude = ParseDouble(value);
        else if (key == "longitude") currentAsset.longitude = ParseDouble(value);
        else if (key == "height") currentAsset.height = ParseDouble(value);
        else if (key == "position_x") currentAsset.position.x = static_cast<float>(ParseDouble(value));
        else if (key == "position_y") currentAsset.position.y = static_cast<float>(ParseDouble(value));
        else if (key == "position_z") currentAsset.position.z = static_cast<float>(ParseDouble(value));
        else if (key == "rotation_z") currentAsset.rotationDegrees.z = static_cast<float>(ParseDouble(value));
        else if (key == "scale_x") currentAsset.scale.x = static_cast<float>(ParseDouble(value, 1.0));
        else if (key == "scale_y") currentAsset.scale.y = static_cast<float>(ParseDouble(value, 1.0));
        else if (key == "scale_z") currentAsset.scale.z = static_cast<float>(ParseDouble(value, 1.0));
        else if (key == "tint_r") currentAsset.tint.r = static_cast<float>(ParseDouble(value, 1.0));
        else if (key == "tint_g") currentAsset.tint.g = static_cast<float>(ParseDouble(value, 1.0));
        else if (key == "tint_b") currentAsset.tint.b = static_cast<float>(ParseDouble(value, 1.0));
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
        file << "  Origin: lat " << terrain.geoReference.originLatitude << ", lon " << terrain.geoReference.originLongitude
             << ", height " << terrain.geoReference.originHeight << "\n";
        file << "  Grid: " << terrain.settings.gridResolutionX << " x " << terrain.settings.gridResolutionZ << "\n";
        file << "  Height Scale: " << terrain.settings.heightScale << "\n";
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

    return true;
}


} // namespace GeoFPS
