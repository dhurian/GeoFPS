#include "Assets/GltfImporter.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include <string>

namespace GeoFPS
{
namespace
{
glm::mat4 GetNodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16)
    {
        glm::mat4 matrix(1.0f);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                matrix[column][row] = static_cast<float>(node.matrix[static_cast<size_t>((column * 4) + row)]);
            }
        }
        return matrix;
    }

    glm::mat4 matrix(1.0f);
    if (node.translation.size() == 3)
    {
        matrix = glm::translate(matrix,
                                glm::vec3(static_cast<float>(node.translation[0]),
                                          static_cast<float>(node.translation[1]),
                                          static_cast<float>(node.translation[2])));
    }

    if (node.rotation.size() == 4)
    {
        const glm::quat rotation(static_cast<float>(node.rotation[3]),
                                 static_cast<float>(node.rotation[0]),
                                 static_cast<float>(node.rotation[1]),
                                 static_cast<float>(node.rotation[2]));
        matrix *= glm::mat4_cast(rotation);
    }

    if (node.scale.size() == 3)
    {
        matrix = glm::scale(matrix,
                            glm::vec3(static_cast<float>(node.scale[0]),
                                      static_cast<float>(node.scale[1]),
                                      static_cast<float>(node.scale[2])));
    }

    return matrix;
}

template <typename T>
const T* ReadAccessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor)
{
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return nullptr;
    }

    const tinygltf::BufferView& bufferView = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int>(model.buffers.size()))
    {
        return nullptr;
    }

    const tinygltf::Buffer& buffer = model.buffers[static_cast<size_t>(bufferView.buffer)];
    const size_t byteOffset = static_cast<size_t>(bufferView.byteOffset + accessor.byteOffset);
    if (byteOffset >= buffer.data.size())
    {
        return nullptr;
    }

    return reinterpret_cast<const T*>(buffer.data.data() + byteOffset);
}

size_t GetAccessorStride(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t elementSize)
{
    if (accessor.bufferView < 0 || accessor.bufferView >= static_cast<int>(model.bufferViews.size()))
    {
        return 0;
    }

    const tinygltf::BufferView& bufferView = model.bufferViews[static_cast<size_t>(accessor.bufferView)];
    if (bufferView.byteStride != 0)
    {
        return static_cast<size_t>(bufferView.byteStride);
    }

    return elementSize;
}

bool ReadVec3Accessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec3>& output)
{
    if (accessor.type != TINYGLTF_TYPE_VEC3 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        return false;
    }

    const auto* data = ReadAccessorData<unsigned char>(model, accessor);
    if (data == nullptr)
    {
        return false;
    }

    output.resize(accessor.count);
    const size_t stride = GetAccessorStride(model, accessor, sizeof(float) * 3u);
    for (size_t index = 0; index < accessor.count; ++index)
    {
        const float* values = reinterpret_cast<const float*>(data + (index * stride));
        output[index] = glm::vec3(values[0], values[1], values[2]);
    }
    return true;
}

bool ReadVec2Accessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<glm::vec2>& output)
{
    if (accessor.type != TINYGLTF_TYPE_VEC2 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
    {
        return false;
    }

    const auto* data = ReadAccessorData<unsigned char>(model, accessor);
    if (data == nullptr)
    {
        return false;
    }

    output.resize(accessor.count);
    const size_t stride = GetAccessorStride(model, accessor, sizeof(float) * 2u);
    for (size_t index = 0; index < accessor.count; ++index)
    {
        const float* values = reinterpret_cast<const float*>(data + (index * stride));
        output[index] = glm::vec2(values[0], values[1]);
    }
    return true;
}

bool ReadIndexAccessor(const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::vector<unsigned int>& output)
{
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
    {
        return false;
    }

    const unsigned char* data = ReadAccessorData<unsigned char>(model, accessor);
    if (data == nullptr)
    {
        return false;
    }

    output.resize(accessor.count);
    const size_t stride = GetAccessorStride(model, accessor, tinygltf::GetComponentSizeInBytes(accessor.componentType));
    for (size_t index = 0; index < accessor.count; ++index)
    {
        const unsigned char* element = data + (index * stride);
        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            output[index] = *reinterpret_cast<const uint8_t*>(element);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            output[index] = *reinterpret_cast<const uint16_t*>(element);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            output[index] = *reinterpret_cast<const uint32_t*>(element);
            break;
        default:
            return false;
        }
    }

    return true;
}

void GenerateNormals(MeshData& meshData)
{
    for (Vertex& vertex : meshData.vertices)
    {
        vertex.normal = glm::vec3(0.0f);
    }

    for (size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
    {
        Vertex& a = meshData.vertices[meshData.indices[index]];
        Vertex& b = meshData.vertices[meshData.indices[index + 1]];
        Vertex& c = meshData.vertices[meshData.indices[index + 2]];

        const glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::length(faceNormal) <= 0.0001f)
        {
            continue;
        }

        a.normal += faceNormal;
        b.normal += faceNormal;
        c.normal += faceNormal;
    }

    for (Vertex& vertex : meshData.vertices)
    {
        if (glm::length(vertex.normal) <= 0.0001f)
        {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        else
        {
            vertex.normal = glm::normalize(vertex.normal);
        }
    }
}

bool LoadPrimitiveMaterial(const tinygltf::Model& model,
                           const tinygltf::Primitive& primitive,
                           ImportedPrimitiveData& primitiveData)
{
    if (primitive.material < 0 || primitive.material >= static_cast<int>(model.materials.size()))
    {
        return true;
    }

    const tinygltf::Material& material = model.materials[static_cast<size_t>(primitive.material)];
    primitiveData.materialName = material.name;
    primitiveData.metallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
    primitiveData.roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);
    primitiveData.alphaMode = material.alphaMode.empty() ? "OPAQUE" : material.alphaMode;
    primitiveData.alphaCutoff = static_cast<float>(material.alphaCutoff);
    if (material.emissiveFactor.size() == 3)
    {
        primitiveData.emissiveFactor = glm::vec3(static_cast<float>(material.emissiveFactor[0]),
                                                 static_cast<float>(material.emissiveFactor[1]),
                                                 static_cast<float>(material.emissiveFactor[2]));
    }

    const auto& factor = material.pbrMetallicRoughness.baseColorFactor;
    if (factor.size() == 4)
    {
        primitiveData.baseColorFactor = glm::vec4(static_cast<float>(factor[0]),
                                                  static_cast<float>(factor[1]),
                                                  static_cast<float>(factor[2]),
                                                  static_cast<float>(factor[3]));
    }

    const auto loadTexturePixels = [&](int textureIndex,
                                       std::vector<unsigned char>& pixels,
                                       int& width,
                                       int& height,
                                       int& channels) {
        if (textureIndex < 0 || textureIndex >= static_cast<int>(model.textures.size()))
        {
            return;
        }

        const tinygltf::Texture& texture = model.textures[static_cast<size_t>(textureIndex)];
        if (texture.source < 0 || texture.source >= static_cast<int>(model.images.size()))
        {
            return;
        }

        const tinygltf::Image& image = model.images[static_cast<size_t>(texture.source)];
        if (image.image.empty() || image.width <= 0 || image.height <= 0 || image.component <= 0)
        {
            return;
        }

        pixels = image.image;
        width = image.width;
        height = image.height;
        channels = image.component;
    };

    loadTexturePixels(material.pbrMetallicRoughness.baseColorTexture.index,
                      primitiveData.baseColorPixels,
                      primitiveData.baseColorWidth,
                      primitiveData.baseColorHeight,
                      primitiveData.baseColorChannels);
    loadTexturePixels(material.pbrMetallicRoughness.metallicRoughnessTexture.index,
                      primitiveData.metallicRoughnessPixels,
                      primitiveData.metallicRoughnessWidth,
                      primitiveData.metallicRoughnessHeight,
                      primitiveData.metallicRoughnessChannels);
    loadTexturePixels(material.normalTexture.index,
                      primitiveData.normalPixels,
                      primitiveData.normalWidth,
                      primitiveData.normalHeight,
                      primitiveData.normalChannels);
    loadTexturePixels(material.emissiveTexture.index,
                      primitiveData.emissivePixels,
                      primitiveData.emissiveWidth,
                      primitiveData.emissiveHeight,
                      primitiveData.emissiveChannels);

    return true;
}

bool LoadMeshPrimitive(const tinygltf::Model& model,
                       const tinygltf::Primitive& primitive,
                       ImportedPrimitiveData& primitiveData,
                       std::string& errorMessage)
{
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
    {
        errorMessage = "Only triangle glTF primitives are currently supported.";
        return false;
    }

    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end())
    {
        errorMessage = "glTF primitive is missing POSITION data.";
        return false;
    }

    if (positionIt->second < 0 || positionIt->second >= static_cast<int>(model.accessors.size()))
    {
        errorMessage = "glTF POSITION accessor is invalid.";
        return false;
    }

    std::vector<glm::vec3> positions;
    if (!ReadVec3Accessor(model, model.accessors[static_cast<size_t>(positionIt->second)], positions) || positions.empty())
    {
        errorMessage = "Failed to read glTF positions.";
        return false;
    }

    std::vector<glm::vec3> normals;
    const auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end() && normalIt->second >= 0 &&
        normalIt->second < static_cast<int>(model.accessors.size()))
    {
        ReadVec3Accessor(model, model.accessors[static_cast<size_t>(normalIt->second)], normals);
    }

    std::vector<glm::vec2> texCoords;
    const auto texCoordIt = primitive.attributes.find("TEXCOORD_0");
    if (texCoordIt != primitive.attributes.end() && texCoordIt->second >= 0 &&
        texCoordIt->second < static_cast<int>(model.accessors.size()))
    {
        ReadVec2Accessor(model, model.accessors[static_cast<size_t>(texCoordIt->second)], texCoords);
    }

    primitiveData.meshData.vertices.resize(positions.size());
    for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex)
    {
        Vertex& vertex = primitiveData.meshData.vertices[vertexIndex];
        vertex.position = positions[vertexIndex];
        if (vertexIndex < normals.size())
        {
            vertex.normal = normals[vertexIndex];
        }
        if (vertexIndex < texCoords.size())
        {
            vertex.uv = texCoords[vertexIndex];
        }
    }

    if (primitive.indices >= 0)
    {
        if (primitive.indices >= static_cast<int>(model.accessors.size()))
        {
            errorMessage = "glTF index accessor is invalid.";
            return false;
        }

        if (!ReadIndexAccessor(model, model.accessors[static_cast<size_t>(primitive.indices)], primitiveData.meshData.indices))
        {
            errorMessage = "Failed to read glTF indices.";
            return false;
        }
    }
    else
    {
        primitiveData.meshData.indices.resize(positions.size());
        for (size_t index = 0; index < positions.size(); ++index)
        {
            primitiveData.meshData.indices[index] = static_cast<unsigned int>(index);
        }
    }

    if (primitiveData.meshData.indices.empty())
    {
        errorMessage = "glTF primitive contained no index data.";
        return false;
    }

    if (normals.empty())
    {
        GenerateNormals(primitiveData.meshData);
    }

    return LoadPrimitiveMaterial(model, primitive, primitiveData);
}

void ApplyTransform(MeshData& meshData, const glm::mat4& transform)
{
    const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (Vertex& vertex : meshData.vertices)
    {
        const glm::vec4 worldPosition = transform * glm::vec4(vertex.position, 1.0f);
        vertex.position = glm::vec3(worldPosition);
        vertex.normal = glm::normalize(normalTransform * vertex.normal);
    }
}

bool LoadNodeRecursive(const tinygltf::Model& model,
                       int nodeIndex,
                       const glm::mat4& parentTransform,
                       ImportedAssetData& assetData,
                       std::string& errorMessage)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
    {
        errorMessage = "glTF scene references an invalid node.";
        return false;
    }

    const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];
    const glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

    if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
    {
        const tinygltf::Mesh& mesh = model.meshes[static_cast<size_t>(node.mesh)];
        for (const tinygltf::Primitive& primitive : mesh.primitives)
        {
            ImportedPrimitiveData primitiveData;
            if (!LoadMeshPrimitive(model, primitive, primitiveData, errorMessage))
            {
                return false;
            }

            ApplyTransform(primitiveData.meshData, worldTransform);
            if (!mesh.name.empty() && primitiveData.materialName.empty())
            {
                primitiveData.materialName = mesh.name;
            }
            assetData.primitives.push_back(std::move(primitiveData));
        }
    }

    for (int childIndex : node.children)
    {
        if (!LoadNodeRecursive(model, childIndex, worldTransform, assetData, errorMessage))
        {
            return false;
        }
    }

    return true;
}
} // namespace

bool GltfImporter::Load(const std::string& path, ImportedAssetData& assetData, std::string& errorMessage)
{
    assetData.primitives.clear();
    errorMessage.clear();

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warnings;
    bool success = false;

    const size_t extensionOffset = path.find_last_of('.');
    const std::string extension = extensionOffset == std::string::npos ? std::string() : path.substr(extensionOffset);
    if (extension == ".glb" || extension == ".GLB")
    {
        success = loader.LoadBinaryFromFile(&model, &errorMessage, &warnings, path);
    }
    else
    {
        success = loader.LoadASCIIFromFile(&model, &errorMessage, &warnings, path);
    }

    if (!warnings.empty() && errorMessage.empty())
    {
        errorMessage = warnings;
    }

    if (!success)
    {
        if (errorMessage.empty())
        {
            errorMessage = "Failed to load glTF asset.";
        }
        return false;
    }

    if (!model.scenes.empty())
    {
        int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
        if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model.scenes.size()))
        {
            sceneIndex = 0;
        }

        const tinygltf::Scene& scene = model.scenes[static_cast<size_t>(sceneIndex)];
        for (int nodeIndex : scene.nodes)
        {
            if (!LoadNodeRecursive(model, nodeIndex, glm::mat4(1.0f), assetData, errorMessage))
            {
                return false;
            }
        }
    }
    else
    {
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
        {
            if (!LoadNodeRecursive(model, nodeIndex, glm::mat4(1.0f), assetData, errorMessage))
            {
                return false;
            }
        }
    }

    if (assetData.primitives.empty())
    {
        errorMessage = "glTF asset contained no mesh primitives.";
        return false;
    }

    return true;
}
} // namespace GeoFPS
