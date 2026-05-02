#include "Assets/GltfImporter.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <unordered_set>

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

// Read JOINTS_0 — supports UNSIGNED_BYTE and UNSIGNED_SHORT component types.
bool ReadJointsAccessor(const tinygltf::Model& model,
                        const tinygltf::Accessor& accessor,
                        std::vector<glm::uvec4>& output)
{
    if (accessor.type != TINYGLTF_TYPE_VEC4)
        return false;

    const unsigned char* data = ReadAccessorData<unsigned char>(model, accessor);
    if (data == nullptr)
        return false;

    output.resize(accessor.count);
    const size_t componentSize = tinygltf::GetComponentSizeInBytes(accessor.componentType);
    const size_t stride = GetAccessorStride(model, accessor, componentSize * 4u);

    for (size_t i = 0; i < accessor.count; ++i)
    {
        const unsigned char* elem = data + i * stride;
        switch (accessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            output[i] = glm::uvec4(elem[0], elem[1], elem[2], elem[3]);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(elem);
            output[i] = glm::uvec4(s[0], s[1], s[2], s[3]);
        }
        break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            const uint32_t* u = reinterpret_cast<const uint32_t*>(elem);
            output[i] = glm::uvec4(u[0], u[1], u[2], u[3]);
        }
        break;
        default:
            return false;
        }
    }
    return true;
}

// Read WEIGHTS_0 — float VEC4.  Normalises weights so they sum to 1.
bool ReadWeightsAccessor(const tinygltf::Model& model,
                         const tinygltf::Accessor& accessor,
                         std::vector<glm::vec4>& output)
{
    if (accessor.type != TINYGLTF_TYPE_VEC4 || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return false;

    const unsigned char* data = ReadAccessorData<unsigned char>(model, accessor);
    if (data == nullptr)
        return false;

    output.resize(accessor.count);
    const size_t stride = GetAccessorStride(model, accessor, sizeof(float) * 4u);
    for (size_t i = 0; i < accessor.count; ++i)
    {
        const float* f = reinterpret_cast<const float*>(data + i * stride);
        glm::vec4 w(f[0], f[1], f[2], f[3]);
        const float sum = w.x + w.y + w.z + w.w;
        if (sum > 1e-6f)
            w /= sum;
        else
            w = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        output[i] = w;
    }
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

    // --- Read joint/weight attributes (GPU skinning) ---
    std::vector<glm::uvec4> jointIndices;
    std::vector<glm::vec4>  jointWeights;

    const auto jointsIt = primitive.attributes.find("JOINTS_0");
    if (jointsIt != primitive.attributes.end() && jointsIt->second >= 0 &&
        jointsIt->second < static_cast<int>(model.accessors.size()))
    {
        ReadJointsAccessor(model, model.accessors[static_cast<size_t>(jointsIt->second)], jointIndices);
    }

    const auto weightsIt = primitive.attributes.find("WEIGHTS_0");
    if (weightsIt != primitive.attributes.end() && weightsIt->second >= 0 &&
        weightsIt->second < static_cast<int>(model.accessors.size()))
    {
        ReadWeightsAccessor(model, model.accessors[static_cast<size_t>(weightsIt->second)], jointWeights);
    }

    const bool hasSkinData = !jointIndices.empty() && !jointWeights.empty() &&
                             jointIndices.size() == positions.size() &&
                             jointWeights.size() == positions.size();

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

    // Populate SkinMeshData in parallel when joint/weight data is available.
    if (hasSkinData)
    {
        primitiveData.skinMeshData.vertices.resize(positions.size());
        for (size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex)
        {
            SkinVertex& sv = primitiveData.skinMeshData.vertices[vertexIndex];
            sv.position     = positions[vertexIndex];
            sv.normal       = vertexIndex < normals.size() ? normals[vertexIndex] : glm::vec3(0.0f, 1.0f, 0.0f);
            sv.uv           = vertexIndex < texCoords.size() ? texCoords[vertexIndex] : glm::vec2(0.0f);
            sv.jointIndices = jointIndices[vertexIndex];
            sv.jointWeights = jointWeights[vertexIndex];
        }
        primitiveData.isSkinned = true;
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

    // Share index buffer with skinMeshData.
    if (hasSkinData)
    {
        primitiveData.skinMeshData.indices = primitiveData.meshData.indices;
    }

    if (normals.empty())
    {
        GenerateNormals(primitiveData.meshData);
        // Also fix normals in skinMeshData if it was populated.
        if (hasSkinData)
        {
            for (size_t i = 0; i < primitiveData.skinMeshData.vertices.size(); ++i)
                primitiveData.skinMeshData.vertices[i].normal =
                    primitiveData.meshData.vertices[i].normal;
        }
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
                       std::string& errorMessage,
                       const std::unordered_set<int>& animatedNodeSet)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
    {
        errorMessage = "glTF scene references an invalid node.";
        return false;
    }

    const tinygltf::Node& node = model.nodes[static_cast<size_t>(nodeIndex)];
    const glm::mat4 worldTransform = parentTransform * GetNodeTransform(node);

    // A node is "node-animated" when animation channels target it directly
    // (and it is not a skin joint — those are already excluded from animatedNodeSet).
    const bool nodeIsAnimated = animatedNodeSet.count(nodeIndex) > 0;

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

            // Baking rules:
            //  • Skinned mesh   → no bake (GPU skinning shader handles all transforms).
            //  • Node-animated  → no bake (animated node world transform applied per-frame via uModel).
            //  • Otherwise      → bake worldTransform into vertex positions now.
            if (!primitiveData.isSkinned && !nodeIsAnimated)
            {
                ApplyTransform(primitiveData.meshData, worldTransform);
            }
            if (nodeIsAnimated)
            {
                // Record which glTF node owns this primitive so the render loop can
                // look up the per-frame world transform.
                primitiveData.nodeIndex = nodeIndex;
            }
            if (!mesh.name.empty() && primitiveData.materialName.empty())
            {
                primitiveData.materialName = mesh.name;
            }
            assetData.primitives.push_back(std::move(primitiveData));
        }
    }

    for (int childIndex : node.children)
    {
        if (!LoadNodeRecursive(model, childIndex, worldTransform, assetData, errorMessage, animatedNodeSet))
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

    // -----------------------------------------------------------------------
    //  Pre-scan: build the set of node indices that have animation channels
    //  targeting them directly (node-transform animation, not skin joints).
    //  Joint nodes are excluded so they remain handled by the skin path.
    // -----------------------------------------------------------------------
    std::unordered_set<int> jointNodeSet;
    for (const tinygltf::Skin& skin : model.skins)
        for (int jni : skin.joints)
            jointNodeSet.insert(jni);

    std::unordered_set<int> animatedNodeSet;
    for (const tinygltf::Animation& anim : model.animations)
        for (const tinygltf::AnimationChannel& chan : anim.channels)
            if (!jointNodeSet.count(chan.target_node))
                animatedNodeSet.insert(chan.target_node);

    // -----------------------------------------------------------------------
    //  Build the node hierarchy (indexed by glTF node index) for runtime use.
    //  We store TRS components separately so animation channels can override
    //  individual components without a costly matrix decomposition.
    // -----------------------------------------------------------------------
    assetData.nodes.resize(model.nodes.size());
    for (size_t ni = 0; ni < model.nodes.size(); ++ni)
    {
        const tinygltf::Node& n = model.nodes[ni];
        NodeData& nd = assetData.nodes[ni];
        nd.name        = n.name;
        nd.parentIndex = -1; // filled below

        if (n.matrix.size() == 16)
        {
            // Node specified a raw matrix — cannot override individual TRS.
            nd.hasMatrix = true;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    nd.matrix[col][row] = static_cast<float>(n.matrix[static_cast<size_t>(col * 4 + row)]);
        }
        else
        {
            nd.hasMatrix = false;
            if (n.translation.size() == 3)
                nd.translation = glm::vec3(static_cast<float>(n.translation[0]),
                                           static_cast<float>(n.translation[1]),
                                           static_cast<float>(n.translation[2]));
            if (n.rotation.size() == 4)
                nd.rotation = glm::quat(static_cast<float>(n.rotation[3]),
                                        static_cast<float>(n.rotation[0]),
                                        static_cast<float>(n.rotation[1]),
                                        static_cast<float>(n.rotation[2]));
            if (n.scale.size() == 3)
                nd.scale = glm::vec3(static_cast<float>(n.scale[0]),
                                     static_cast<float>(n.scale[1]),
                                     static_cast<float>(n.scale[2]));
        }
    }
    // Fill parent indices by scanning children lists.
    for (size_t ni = 0; ni < model.nodes.size(); ++ni)
        for (int childIdx : model.nodes[ni].children)
            if (childIdx >= 0 && childIdx < static_cast<int>(model.nodes.size()))
                assetData.nodes[static_cast<size_t>(childIdx)].parentIndex = static_cast<int>(ni);

    // -----------------------------------------------------------------------
    //  Traverse scene nodes and collect mesh primitives.
    // -----------------------------------------------------------------------
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
            if (!LoadNodeRecursive(model, nodeIndex, glm::mat4(1.0f), assetData, errorMessage, animatedNodeSet))
            {
                return false;
            }
        }
    }
    else
    {
        for (int nodeIndex = 0; nodeIndex < static_cast<int>(model.nodes.size()); ++nodeIndex)
        {
            if (!LoadNodeRecursive(model, nodeIndex, glm::mat4(1.0f), assetData, errorMessage, animatedNodeSet))
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

    // -----------------------------------------------------------------------
    //  Parse skin (first skin only)
    // -----------------------------------------------------------------------
    if (!model.skins.empty())
    {
        const tinygltf::Skin& skin = model.skins[0];
        assetData.hasSkin = true;

        // Build a node-index → joint-index lookup table.
        std::vector<int> nodeToJoint(model.nodes.size(), -1);
        for (int ji = 0; ji < static_cast<int>(skin.joints.size()); ++ji)
        {
            const int nodeIdx = skin.joints[static_cast<size_t>(ji)];
            if (nodeIdx >= 0 && nodeIdx < static_cast<int>(model.nodes.size()))
                nodeToJoint[static_cast<size_t>(nodeIdx)] = ji;
        }

        // Inverse-bind matrices accessor (optional).
        std::vector<glm::mat4> invBindMatrices;
        if (skin.inverseBindMatrices >= 0 &&
            skin.inverseBindMatrices < static_cast<int>(model.accessors.size()))
        {
            const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(skin.inverseBindMatrices)];
            if (acc.type == TINYGLTF_TYPE_MAT4 && acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT)
            {
                const unsigned char* raw = ReadAccessorData<unsigned char>(model, acc);
                if (raw != nullptr)
                {
                    const size_t stride = GetAccessorStride(model, acc, sizeof(float) * 16u);
                    invBindMatrices.resize(acc.count);
                    for (size_t k = 0; k < acc.count; ++k)
                    {
                        const float* f = reinterpret_cast<const float*>(raw + k * stride);
                        glm::mat4 m;
                        // glTF column-major storage matches GLM column-major.
                        std::memcpy(glm::value_ptr(m), f, sizeof(float) * 16u);
                        invBindMatrices[k] = m;
                    }
                }
            }
        }

        assetData.skeleton.joints.resize(skin.joints.size());
        for (int ji = 0; ji < static_cast<int>(skin.joints.size()); ++ji)
        {
            const int nodeIdx = skin.joints[static_cast<size_t>(ji)];
            JointData& joint = assetData.skeleton.joints[static_cast<size_t>(ji)];

            if (nodeIdx >= 0 && nodeIdx < static_cast<int>(model.nodes.size()))
                joint.name = model.nodes[static_cast<size_t>(nodeIdx)].name;

            if (ji < static_cast<int>(invBindMatrices.size()))
                joint.inverseBindMatrix = invBindMatrices[static_cast<size_t>(ji)];

            // Find parent: iterate every joint and check whether ji appears in children.
            joint.parentIndex = -1;
            for (int pj = 0; pj < static_cast<int>(skin.joints.size()); ++pj)
            {
                if (pj == ji)
                    continue;
                const int pNode = skin.joints[static_cast<size_t>(pj)];
                if (pNode < 0 || pNode >= static_cast<int>(model.nodes.size()))
                    continue;
                const auto& children = model.nodes[static_cast<size_t>(pNode)].children;
                if (std::find(children.begin(), children.end(), nodeIdx) != children.end())
                {
                    joint.parentIndex = pj;
                    break;
                }
            }
        }

        // Identify root joint (skin.skeleton node, or whichever joint has no parent).
        assetData.skeleton.rootJointIndex = -1;
        if (skin.skeleton >= 0 && skin.skeleton < static_cast<int>(model.nodes.size()))
            assetData.skeleton.rootJointIndex = nodeToJoint[static_cast<size_t>(skin.skeleton)];
        if (assetData.skeleton.rootJointIndex < 0)
        {
            for (int ji = 0; ji < static_cast<int>(assetData.skeleton.joints.size()); ++ji)
            {
                if (assetData.skeleton.joints[static_cast<size_t>(ji)].parentIndex < 0)
                {
                    assetData.skeleton.rootJointIndex = ji;
                    break;
                }
            }
        }

        // -----------------------------------------------------------------------
        //  Parse animations
        // -----------------------------------------------------------------------
        for (const tinygltf::Animation& anim : model.animations)
        {
            AnimationClip clip;
            clip.name = anim.name.empty() ? "Animation" : anim.name;

            for (const tinygltf::AnimationChannel& chan : anim.channels)
            {
                // Map target node → joint index.
                const int targetNode = chan.target_node;
                if (targetNode < 0 || targetNode >= static_cast<int>(nodeToJoint.size()))
                    continue;
                const int jointIdx = nodeToJoint[static_cast<size_t>(targetNode)];
                if (jointIdx < 0)
                    continue;

                if (chan.sampler < 0 || chan.sampler >= static_cast<int>(anim.samplers.size()))
                    continue;
                const tinygltf::AnimationSampler& sampler = anim.samplers[static_cast<size_t>(chan.sampler)];

                // Input accessor — time values.
                if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
                    continue;
                const tinygltf::Accessor& timeAcc = model.accessors[static_cast<size_t>(sampler.input)];
                if (timeAcc.type != TINYGLTF_TYPE_SCALAR || timeAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                    continue;
                const unsigned char* timeRaw = ReadAccessorData<unsigned char>(model, timeAcc);
                if (timeRaw == nullptr)
                    continue;

                // Output accessor — TRS values.
                if (sampler.output < 0 || sampler.output >= static_cast<int>(model.accessors.size()))
                    continue;
                const tinygltf::Accessor& valAcc = model.accessors[static_cast<size_t>(sampler.output)];
                const unsigned char* valRaw = ReadAccessorData<unsigned char>(model, valAcc);
                if (valRaw == nullptr)
                    continue;

                const std::string& path = chan.target_path;
                if (path != "translation" && path != "rotation" && path != "scale")
                    continue;

                AnimationChannel animChan;
                animChan.jointIndex    = jointIdx;
                animChan.path          = path;
                animChan.interpolation = sampler.interpolation;

                // Read times.
                {
                    const size_t timeStride = GetAccessorStride(model, timeAcc, sizeof(float));
                    animChan.times.resize(timeAcc.count);
                    for (size_t k = 0; k < timeAcc.count; ++k)
                    {
                        const float* f = reinterpret_cast<const float*>(timeRaw + k * timeStride);
                        animChan.times[k] = f[0];
                    }
                }

                // Read values.
                if (path == "translation" || path == "scale")
                {
                    if (valAcc.type != TINYGLTF_TYPE_VEC3 || valAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                        continue;
                    const size_t valStride = GetAccessorStride(model, valAcc, sizeof(float) * 3u);
                    animChan.valuesVec3.resize(valAcc.count);
                    for (size_t k = 0; k < valAcc.count; ++k)
                    {
                        const float* f = reinterpret_cast<const float*>(valRaw + k * valStride);
                        animChan.valuesVec3[k] = glm::vec3(f[0], f[1], f[2]);
                    }
                }
                else // rotation
                {
                    if (valAcc.type != TINYGLTF_TYPE_VEC4 || valAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                        continue;
                    const size_t valStride = GetAccessorStride(model, valAcc, sizeof(float) * 4u);
                    animChan.valuesQuat.resize(valAcc.count);
                    for (size_t k = 0; k < valAcc.count; ++k)
                    {
                        const float* f = reinterpret_cast<const float*>(valRaw + k * valStride);
                        // glTF stores quaternion as (x, y, z, w); GLM quat ctor is (w, x, y, z).
                        animChan.valuesQuat[k] = glm::normalize(glm::quat(f[3], f[0], f[1], f[2]));
                    }
                }

                if (!animChan.times.empty())
                    clip.duration = std::max(clip.duration, animChan.times.back());

                clip.channels.push_back(std::move(animChan));
            }

            if (!clip.channels.empty())
                assetData.animations.push_back(std::move(clip));
        }
    }

    // -----------------------------------------------------------------------
    //  Parse node-transform animations (channels targeting non-joint nodes).
    //  These work without any skin — the GPU uses per-primitive uModel instead.
    // -----------------------------------------------------------------------
    for (const tinygltf::Animation& anim : model.animations)
    {
        NodeAnimationClip clip;
        clip.name = anim.name.empty() ? "NodeAnimation" : anim.name;

        for (const tinygltf::AnimationChannel& chan : anim.channels)
        {
            const int targetNode = chan.target_node;
            if (targetNode < 0 || targetNode >= static_cast<int>(model.nodes.size()))
                continue;
            // Skip joint nodes — those are handled by the skin animation path.
            if (jointNodeSet.count(targetNode))
                continue;

            const std::string& path = chan.target_path;
            if (path != "translation" && path != "rotation" && path != "scale")
                continue;

            // Skip nodes that specified a raw matrix (TRS components unknown).
            if (assetData.nodes[static_cast<size_t>(targetNode)].hasMatrix)
                continue;

            if (chan.sampler < 0 || chan.sampler >= static_cast<int>(anim.samplers.size()))
                continue;
            const tinygltf::AnimationSampler& sampler = anim.samplers[static_cast<size_t>(chan.sampler)];

            if (sampler.input < 0 || sampler.input >= static_cast<int>(model.accessors.size()))
                continue;
            const tinygltf::Accessor& timeAcc = model.accessors[static_cast<size_t>(sampler.input)];
            if (timeAcc.type != TINYGLTF_TYPE_SCALAR || timeAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                continue;
            const unsigned char* timeRaw = ReadAccessorData<unsigned char>(model, timeAcc);
            if (timeRaw == nullptr)
                continue;

            if (sampler.output < 0 || sampler.output >= static_cast<int>(model.accessors.size()))
                continue;
            const tinygltf::Accessor& valAcc = model.accessors[static_cast<size_t>(sampler.output)];
            const unsigned char* valRaw = ReadAccessorData<unsigned char>(model, valAcc);
            if (valRaw == nullptr)
                continue;

            NodeAnimationChannel animChan;
            animChan.nodeIndex     = targetNode;
            animChan.path          = path;
            animChan.interpolation = sampler.interpolation;

            // Read time values.
            {
                const size_t stride = GetAccessorStride(model, timeAcc, sizeof(float));
                animChan.times.resize(timeAcc.count);
                for (size_t k = 0; k < timeAcc.count; ++k)
                {
                    const float* f = reinterpret_cast<const float*>(timeRaw + k * stride);
                    animChan.times[k] = f[0];
                }
            }

            // Read output values.
            if (path == "translation" || path == "scale")
            {
                if (valAcc.type != TINYGLTF_TYPE_VEC3 || valAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                    continue;
                const size_t stride = GetAccessorStride(model, valAcc, sizeof(float) * 3u);
                animChan.valuesVec3.resize(valAcc.count);
                for (size_t k = 0; k < valAcc.count; ++k)
                {
                    const float* f = reinterpret_cast<const float*>(valRaw + k * stride);
                    animChan.valuesVec3[k] = glm::vec3(f[0], f[1], f[2]);
                }
            }
            else // rotation
            {
                if (valAcc.type != TINYGLTF_TYPE_VEC4 || valAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
                    continue;
                const size_t stride = GetAccessorStride(model, valAcc, sizeof(float) * 4u);
                animChan.valuesQuat.resize(valAcc.count);
                for (size_t k = 0; k < valAcc.count; ++k)
                {
                    const float* f = reinterpret_cast<const float*>(valRaw + k * stride);
                    // glTF quaternion (x,y,z,w) → GLM quat ctor (w,x,y,z).
                    animChan.valuesQuat[k] = glm::normalize(glm::quat(f[3], f[0], f[1], f[2]));
                }
            }

            if (!animChan.times.empty())
                clip.duration = std::max(clip.duration, animChan.times.back());

            clip.channels.push_back(std::move(animChan));
        }

        if (!clip.channels.empty())
            assetData.nodeAnimations.push_back(std::move(clip));
    }

    if (!assetData.nodeAnimations.empty())
        assetData.hasNodeAnimation = true;

    return true;
}
} // namespace GeoFPS
