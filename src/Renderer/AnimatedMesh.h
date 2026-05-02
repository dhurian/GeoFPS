#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace GeoFPS
{

// ---------------------------------------------------------------------------
//  Vertex format for GPU-skinned meshes
//  Layout (stride = 64 bytes):
//    loc 0  position      vec3   offset  0
//    loc 1  normal        vec3   offset 12
//    loc 2  uv            vec2   offset 24
//    loc 3  jointIndices  uvec4  offset 32  (integer attrib — glVertexAttribIPointer)
//    loc 4  jointWeights  vec4   offset 48
// ---------------------------------------------------------------------------

struct SkinVertex
{
    glm::vec3  position     {};
    glm::vec3  normal       {0.0f, 1.0f, 0.0f};
    glm::vec2  uv           {};
    glm::uvec4 jointIndices {0u, 0u, 0u, 0u};
    glm::vec4  jointWeights {1.0f, 0.0f, 0.0f, 0.0f};  // must sum to 1.0
};

struct SkinMeshData
{
    std::vector<SkinVertex>      vertices;
    std::vector<unsigned int>    indices;
};

// ---------------------------------------------------------------------------
//  GPU mesh that owns the VAO/VBO/EBO for a skinned primitive.
//  Must be created on the main (OpenGL) thread.
// ---------------------------------------------------------------------------

class AnimatedMesh
{
public:
    explicit AnimatedMesh(const SkinMeshData& data);
    ~AnimatedMesh();

    // Non-copyable, movable
    AnimatedMesh(const AnimatedMesh&)            = delete;
    AnimatedMesh& operator=(const AnimatedMesh&) = delete;
    AnimatedMesh(AnimatedMesh&&)                 = default;
    AnimatedMesh& operator=(AnimatedMesh&&)      = default;

    void Draw()     const;
    bool IsLoaded() const { return m_Loaded; }

private:
    unsigned int m_Vao        {0};
    unsigned int m_Vbo        {0};
    unsigned int m_Ebo        {0};
    int          m_IndexCount {0};
    bool         m_Loaded     {false};
};

} // namespace GeoFPS
