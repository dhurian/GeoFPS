#pragma once

#include <string>
#include <vector>

#include "TerrainPoint.h"

class TerrainImporter
{
public:
    static bool LoadCSV(const std::string& path, std::vector<TerrainPoint>& outPoints);
};
