#include "TerrainImporter.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace
{
    std::string Trim(const std::string& value)
    {
        const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
            return std::isspace(ch);
        });

        const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
            return std::isspace(ch);
        }).base();

        if (begin >= end)
        {
            return {};
        }

        return std::string(begin, end);
    }

    bool ParseCSVLine(const std::string& line, TerrainPoint& outPoint)
    {
        std::stringstream ss(line);
        std::string latStr, lonStr, heightStr;

        if (!std::getline(ss, latStr, ',')) return false;
        if (!std::getline(ss, lonStr, ',')) return false;
        if (!std::getline(ss, heightStr, ',')) return false;

        latStr = Trim(latStr);
        lonStr = Trim(lonStr);
        heightStr = Trim(heightStr);

        try
        {
            outPoint.latitude  = std::stod(latStr);
            outPoint.longitude = std::stod(lonStr);
            outPoint.height    = std::stod(heightStr);
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    bool LooksLikeHeader(const std::string& line)
    {
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return lower.find("latitude")  != std::string::npos ||
               lower.find("longitude") != std::string::npos ||
               lower.find("height")    != std::string::npos;
    }
}

bool TerrainImporter::LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints)
{
    outPoints.clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open terrain file: " << path << std::endl;
        return false;
    }

    std::string line;
    bool firstLine = true;
    size_t lineNumber = 0;

    while (std::getline(file, line))
    {
        ++lineNumber;

        line = Trim(line);
        if (line.empty())
            continue;

        if (firstLine)
        {
            firstLine = false;
            if (LooksLikeHeader(line))
                continue;
        }

        TerrainPoint point;
        if (!ParseCSVLine(line, point))
        {
            std::cerr << "Skipping invalid line " << lineNumber << std::endl;
            continue;
        }

        outPoints.push_back(point);
    }

    std::cout << "Loaded " << outPoints.size() << " terrain points\n";

    return !outPoints.empty();
}
