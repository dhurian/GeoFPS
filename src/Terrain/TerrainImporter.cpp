#include "Terrain/TerrainImporter.h"

#include <fstream>
#include <sstream>

namespace GeoFPS
{
bool TerrainImporter::LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    outPoints.clear();
    std::string line;
    bool firstLine = true;
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
    }

    return !outPoints.empty();
}
} // namespace GeoFPS
