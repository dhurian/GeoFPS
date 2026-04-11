#pragma once

#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace GeoFPS
{
struct ImportedPrimitiveData
{
    MeshData meshData;
    std::string materialName;
    glm::vec4 baseColorFactor {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<unsigned char> baseColorPixels;
    int baseColorWidth {0};
    int baseColorHeight {0};
    int baseColorChannels {0};
    std::unique_ptr<Mesh> mesh;
    Texture baseColorTexture;
    bool hasBaseColorTexture {false};
};

struct ImportedAssetData
{
    std::vector<ImportedPrimitiveData> primitives;
};

class GltfImporter
{
  public:
    static bool Load(const std::string& path, ImportedAssetData& assetData, std::string& errorMessage);
};
} // namespace GeoFPS
