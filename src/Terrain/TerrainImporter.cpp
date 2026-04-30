#include "Terrain/TerrainImporter.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace GeoFPS
{
namespace
{
std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (first >= last)
    {
        return {};
    }
    return std::string(first, last);
}

std::optional<std::string> ExtractJsonString(const std::string& text, const std::string& key)
{
    const std::string marker = "\"" + key + "\"";
    const size_t keyOffset = text.find(marker);
    if (keyOffset == std::string::npos)
    {
        return std::nullopt;
    }
    const size_t separator = text.find(':', keyOffset + marker.size());
    if (separator == std::string::npos)
    {
        return std::nullopt;
    }
    const size_t openQuote = text.find('"', separator + 1);
    if (openQuote == std::string::npos)
    {
        return std::nullopt;
    }
    size_t closeQuote = openQuote + 1;
    while (closeQuote < text.size())
    {
        if (text[closeQuote] == '"' && text[closeQuote - 1] != '\\')
        {
            return text.substr(openQuote + 1, closeQuote - openQuote - 1);
        }
        ++closeQuote;
    }
    return std::nullopt;
}

std::optional<double> ExtractJsonDouble(const std::string& text, const std::string& key)
{
    const std::string marker = "\"" + key + "\"";
    const size_t keyOffset = text.find(marker);
    if (keyOffset == std::string::npos)
    {
        return std::nullopt;
    }
    const size_t separator = text.find(':', keyOffset + marker.size());
    if (separator == std::string::npos)
    {
        return std::nullopt;
    }
    size_t begin = separator + 1;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    size_t end = begin;
    while (end < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '-' || text[end] == '+' ||
            text[end] == '.' || text[end] == 'e' || text[end] == 'E'))
    {
        ++end;
    }
    if (begin == end)
    {
        return std::nullopt;
    }
    return std::stod(text.substr(begin, end - begin));
}

std::optional<size_t> ExtractJsonSize(const std::string& text, const std::string& key)
{
    const std::optional<double> value = ExtractJsonDouble(text, key);
    if (!value.has_value() || *value < 0.0)
    {
        return std::nullopt;
    }
    return static_cast<size_t>(*value);
}

std::vector<std::string> ExtractJsonObjectsFromArray(const std::string& text, const std::string& key)
{
    std::vector<std::string> objects;
    const std::string marker = "\"" + key + "\"";
    const size_t keyOffset = text.find(marker);
    if (keyOffset == std::string::npos)
    {
        return objects;
    }
    const size_t arrayBegin = text.find('[', keyOffset + marker.size());
    if (arrayBegin == std::string::npos)
    {
        return objects;
    }

    int arrayDepth = 0;
    int objectDepth = 0;
    size_t objectBegin = std::string::npos;
    bool inString = false;
    for (size_t index = arrayBegin; index < text.size(); ++index)
    {
        const char c = text[index];
        if (c == '"' && (index == 0 || text[index - 1] != '\\'))
        {
            inString = !inString;
        }
        if (inString)
        {
            continue;
        }
        if (c == '[')
        {
            ++arrayDepth;
        }
        else if (c == ']')
        {
            --arrayDepth;
            if (arrayDepth == 0)
            {
                break;
            }
        }
        else if (c == '{')
        {
            if (objectDepth == 0)
            {
                objectBegin = index;
            }
            ++objectDepth;
        }
        else if (c == '}')
        {
            --objectDepth;
            if (objectDepth == 0 && objectBegin != std::string::npos)
            {
                objects.push_back(text.substr(objectBegin, index - objectBegin + 1));
                objectBegin = std::string::npos;
            }
        }
    }
    return objects;
}

std::string ResolveManifestRelativePath(const std::string& manifestPath, const std::string& tilePath)
{
    const std::filesystem::path rawTilePath(tilePath);
    if (rawTilePath.is_absolute())
    {
        return rawTilePath.string();
    }
    return (std::filesystem::path(manifestPath).parent_path() / rawTilePath).lexically_normal().string();
}
} // namespace

bool TerrainImporter::LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints)
{
    return LoadCSV(path, TerrainImportOptions {}, outPoints);
}

bool TerrainImporter::LoadCSV(const std::string& path,
                              const TerrainImportOptions& options,
                              std::vector<TerrainPoint>& outPoints)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    outPoints.clear();
    std::string line;
    bool firstLine = true;
    size_t dataRowIndex = 0;
    const size_t sampleStep = static_cast<size_t>(std::max(options.sampleStep, 1));
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }

        if (firstLine)
        {
            firstLine = false;
            if (line.find("latitude") != std::string::npos)
            {
                continue;
            }
        }

        if ((dataRowIndex % sampleStep) != 0u)
        {
            ++dataRowIndex;
            continue;
        }

        std::stringstream ss(line);
        std::string latText;
        std::string lonText;
        std::string heightText;

        if (!std::getline(ss, latText, ','))
        {
            continue;
        }
        if (!std::getline(ss, lonText, ','))
        {
            continue;
        }
        if (!std::getline(ss, heightText, ','))
        {
            continue;
        }

        TerrainPoint point;
        point.latitude = std::stod(latText);
        point.longitude = std::stod(lonText);
        point.height = std::stod(heightText);
        outPoints.push_back(point);
        ++dataRowIndex;

        if (options.maxPoints > 0u && outPoints.size() >= options.maxPoints)
        {
            break;
        }
    }

    return !outPoints.empty();
}

bool TerrainImporter::LoadTileManifest(const std::string& path,
                                       TerrainTileManifest& outManifest,
                                       std::string& errorMessage)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        errorMessage = "Terrain tile manifest does not exist: " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    TerrainTileManifest manifest;
    manifest.name = ExtractJsonString(text, "name").value_or(std::string {});
    manifest.coordinateMode = ExtractJsonString(text, "coordinate_mode").value_or("geographic");
    manifest.crs = ExtractJsonString(text, "crs").value_or("EPSG:4326");
    manifest.originLatitude = ExtractJsonDouble(text, "origin_latitude").value_or(0.0);
    manifest.originLongitude = ExtractJsonDouble(text, "origin_longitude").value_or(0.0);
    manifest.originHeight = ExtractJsonDouble(text, "origin_height").value_or(0.0);
    manifest.tileSizeDegrees = ExtractJsonDouble(text, "tile_size_degrees").value_or(0.01);
    manifest.tileOverlapSamples = static_cast<int>(ExtractJsonDouble(text, "tile_overlap_samples").value_or(1.0));
    manifest.minLatitude = ExtractJsonDouble(text, "min_latitude").value_or(0.0);
    manifest.maxLatitude = ExtractJsonDouble(text, "max_latitude").value_or(0.0);
    manifest.minLongitude = ExtractJsonDouble(text, "min_longitude").value_or(0.0);
    manifest.maxLongitude = ExtractJsonDouble(text, "max_longitude").value_or(0.0);
    manifest.minHeight = ExtractJsonDouble(text, "min_height").value_or(0.0);
    manifest.maxHeight = ExtractJsonDouble(text, "max_height").value_or(0.0);
    manifest.pointCount = ExtractJsonSize(text, "point_count").value_or(0u);

    const std::vector<std::string> tileObjects = ExtractJsonObjectsFromArray(text, "tiles");
    for (const std::string& tileObject : tileObjects)
    {
        TerrainTileManifestEntry entry;
        const std::optional<std::string> relativePath = ExtractJsonString(tileObject, "path");
        if (!relativePath.has_value() || Trim(*relativePath).empty())
        {
            errorMessage = "Terrain tile manifest contains a tile without a path.";
            return false;
        }
        entry.path = ResolveManifestRelativePath(path, *relativePath);
        entry.row = static_cast<int>(ExtractJsonDouble(tileObject, "row").value_or(0.0));
        entry.col = static_cast<int>(ExtractJsonDouble(tileObject, "col").value_or(0.0));
        entry.minLatitude = ExtractJsonDouble(tileObject, "min_latitude").value_or(0.0);
        entry.maxLatitude = ExtractJsonDouble(tileObject, "max_latitude").value_or(0.0);
        entry.minLongitude = ExtractJsonDouble(tileObject, "min_longitude").value_or(0.0);
        entry.maxLongitude = ExtractJsonDouble(tileObject, "max_longitude").value_or(0.0);
        entry.minHeight = ExtractJsonDouble(tileObject, "min_height").value_or(0.0);
        entry.maxHeight = ExtractJsonDouble(tileObject, "max_height").value_or(0.0);
        entry.pointCount = ExtractJsonSize(tileObject, "point_count").value_or(0u);

        if (entry.minLatitude > entry.maxLatitude || entry.minLongitude > entry.maxLongitude)
        {
            errorMessage = "Terrain tile manifest contains invalid tile bounds.";
            return false;
        }
        manifest.tiles.push_back(std::move(entry));
    }

    if (manifest.tiles.empty())
    {
        errorMessage = "Terrain tile manifest contains no tiles.";
        return false;
    }

    outManifest = std::move(manifest);
    errorMessage.clear();
    return true;
}
} // namespace GeoFPS
