#pragma once

#include "Renderer/Mesh.h"
#include <string>

namespace GeoFPS
{
class ObjImporter
{
  public:
    static bool Load(const std::string& path, MeshData& meshData, std::string& errorMessage);
};
} // namespace GeoFPS
