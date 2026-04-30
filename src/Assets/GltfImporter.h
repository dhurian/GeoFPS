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
    float metallicFactor {0.0f};
    float roughnessFactor {1.0f};
    glm::vec3 emissiveFactor {0.0f, 0.0f, 0.0f};
    std::string alphaMode {"OPAQUE"};
    float alphaCutoff {0.5f};
    std::vector<unsigned char> baseColorPixels;
    std::vector<unsigned char> metallicRoughnessPixels;
    std::vector<unsigned char> normalPixels;
    std::vector<unsigned char> emissivePixels;
    int baseColorWidth {0};
    int baseColorHeight {0};
    int baseColorChannels {0};
    int metallicRoughnessWidth {0};
    int metallicRoughnessHeight {0};
    int metallicRoughnessChannels {0};
    int normalWidth {0};
    int normalHeight {0};
    int normalChannels {0};
    int emissiveWidth {0};
    int emissiveHeight {0};
    int emissiveChannels {0};
    std::unique_ptr<Mesh> mesh;
    Texture baseColorTexture;
    Texture metallicRoughnessTexture;
    Texture normalTexture;
    Texture emissiveTexture;
    bool hasBaseColorTexture {false};
    bool hasMetallicRoughnessTexture {false};
    bool hasNormalTexture {false};
    bool hasEmissiveTexture {false};
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
