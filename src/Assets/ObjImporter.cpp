#include "Assets/ObjImporter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace GeoFPS
{
namespace
{
struct ObjIndex
{
    int position {-1};
    int texCoord {-1};
    int normal {-1};

    bool operator==(const ObjIndex& other) const
    {
        return position == other.position && texCoord == other.texCoord && normal == other.normal;
    }
};

struct ObjIndexHasher
{
    size_t operator()(const ObjIndex& value) const
    {
        size_t hash = std::hash<int> {}(value.position);
        hash ^= std::hash<int> {}(value.texCoord + 131) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= std::hash<int> {}(value.normal + 719) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }
};

std::string Trim(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();

    if (first >= last)
    {
        return {};
    }

    return std::string(first, last);
}

int ResolveObjIndex(int rawIndex, int count)
{
    if (rawIndex > 0)
    {
        return rawIndex - 1;
    }
    if (rawIndex < 0)
    {
        return count + rawIndex;
    }
    return -1;
}

bool ParseFaceVertex(const std::string& token, ObjIndex& index)
{
    std::stringstream stream(token);
    std::string positionToken;
    std::string texCoordToken;
    std::string normalToken;

    if (!std::getline(stream, positionToken, '/'))
    {
        return false;
    }

    if (positionToken.empty())
    {
        return false;
    }

    index.position = std::stoi(positionToken);

    if (std::getline(stream, texCoordToken, '/'))
    {
        if (!texCoordToken.empty())
        {
            index.texCoord = std::stoi(texCoordToken);
        }

        if (std::getline(stream, normalToken, '/') && !normalToken.empty())
        {
            index.normal = std::stoi(normalToken);
        }
    }

    return true;
}

void GenerateNormalsIfMissing(MeshData& meshData)
{
    bool hasNormal = false;
    for (const Vertex& vertex : meshData.vertices)
    {
        if (glm::length(vertex.normal) > 0.0001f)
        {
            hasNormal = true;
            break;
        }
    }

    if (hasNormal)
    {
        return;
    }

    for (Vertex& vertex : meshData.vertices)
    {
        vertex.normal = glm::vec3(0.0f);
    }

    for (size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
    {
        Vertex& a = meshData.vertices[meshData.indices[index]];
        Vertex& b = meshData.vertices[meshData.indices[index + 1]];
        Vertex& c = meshData.vertices[meshData.indices[index + 2]];

        const glm::vec3 edgeAB = b.position - a.position;
        const glm::vec3 edgeAC = c.position - a.position;
        const glm::vec3 faceNormal = glm::cross(edgeAB, edgeAC);
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
            continue;
        }

        vertex.normal = glm::normalize(vertex.normal);
    }
}
} // namespace

bool ObjImporter::Load(const std::string& path, MeshData& meshData, std::string& errorMessage)
{
    meshData.vertices.clear();
    meshData.indices.clear();
    errorMessage.clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        errorMessage = "Could not open OBJ file.";
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;
    std::unordered_map<ObjIndex, unsigned int, ObjIndexHasher> vertexLookup;

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

        std::stringstream stream(trimmed);
        std::string prefix;
        stream >> prefix;

        if (prefix == "v")
        {
            glm::vec3 position {};
            if (!(stream >> position.x >> position.y >> position.z))
            {
                errorMessage = "Invalid vertex position on line " + std::to_string(lineNumber) + ".";
                return false;
            }
            positions.push_back(position);
        }
        else if (prefix == "vt")
        {
            glm::vec2 texCoord {};
            if (!(stream >> texCoord.x >> texCoord.y))
            {
                errorMessage = "Invalid texture coordinate on line " + std::to_string(lineNumber) + ".";
                return false;
            }
            texCoords.push_back(texCoord);
        }
        else if (prefix == "vn")
        {
            glm::vec3 normal {};
            if (!(stream >> normal.x >> normal.y >> normal.z))
            {
                errorMessage = "Invalid normal on line " + std::to_string(lineNumber) + ".";
                return false;
            }
            normals.push_back(normal);
        }
        else if (prefix == "f")
        {
            std::vector<unsigned int> faceIndices;
            std::string token;
            while (stream >> token)
            {
                ObjIndex rawIndex {};
                if (!ParseFaceVertex(token, rawIndex))
                {
                    errorMessage = "Invalid face element on line " + std::to_string(lineNumber) + ".";
                    return false;
                }

                ObjIndex resolvedIndex {};
                resolvedIndex.position = ResolveObjIndex(rawIndex.position, static_cast<int>(positions.size()));
                resolvedIndex.texCoord = ResolveObjIndex(rawIndex.texCoord, static_cast<int>(texCoords.size()));
                resolvedIndex.normal = ResolveObjIndex(rawIndex.normal, static_cast<int>(normals.size()));

                if (resolvedIndex.position < 0 || resolvedIndex.position >= static_cast<int>(positions.size()))
                {
                    errorMessage = "Face references missing vertex data on line " + std::to_string(lineNumber) + ".";
                    return false;
                }

                auto existing = vertexLookup.find(resolvedIndex);
                if (existing == vertexLookup.end())
                {
                    Vertex vertex {};
                    vertex.position = positions[static_cast<size_t>(resolvedIndex.position)];

                    if (resolvedIndex.texCoord >= 0 && resolvedIndex.texCoord < static_cast<int>(texCoords.size()))
                    {
                        vertex.uv = texCoords[static_cast<size_t>(resolvedIndex.texCoord)];
                    }

                    if (resolvedIndex.normal >= 0 && resolvedIndex.normal < static_cast<int>(normals.size()))
                    {
                        vertex.normal = glm::normalize(normals[static_cast<size_t>(resolvedIndex.normal)]);
                    }
                    else
                    {
                        vertex.normal = glm::vec3(0.0f);
                    }

                    const unsigned int newIndex = static_cast<unsigned int>(meshData.vertices.size());
                    meshData.vertices.push_back(vertex);
                    vertexLookup.emplace(resolvedIndex, newIndex);
                    faceIndices.push_back(newIndex);
                }
                else
                {
                    faceIndices.push_back(existing->second);
                }
            }

            if (faceIndices.size() < 3)
            {
                errorMessage = "Face with fewer than 3 vertices on line " + std::to_string(lineNumber) + ".";
                return false;
            }

            for (size_t index = 1; index + 1 < faceIndices.size(); ++index)
            {
                meshData.indices.push_back(faceIndices[0]);
                meshData.indices.push_back(faceIndices[index]);
                meshData.indices.push_back(faceIndices[index + 1]);
            }
        }
    }

    if (meshData.vertices.empty() || meshData.indices.empty())
    {
        errorMessage = "OBJ file did not contain any triangle data.";
        return false;
    }

    GenerateNormalsIfMissing(meshData);
    return true;
}
} // namespace GeoFPS
