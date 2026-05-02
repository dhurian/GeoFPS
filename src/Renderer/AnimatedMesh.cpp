#include "Renderer/AnimatedMesh.h"

#include <glad/glad.h>
#include <cstddef>  // offsetof

namespace GeoFPS
{

AnimatedMesh::AnimatedMesh(const SkinMeshData& data)
{
    if (data.vertices.empty() || data.indices.empty())
        return;

    m_IndexCount = static_cast<int>(data.indices.size());

    glGenVertexArrays(1, &m_Vao);
    glGenBuffers(1, &m_Vbo);
    glGenBuffers(1, &m_Ebo);

    glBindVertexArray(m_Vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.vertices.size() * sizeof(SkinVertex)),
                 data.vertices.data(),
                 GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.indices.size() * sizeof(unsigned int)),
                 data.indices.data(),
                 GL_STATIC_DRAW);

    // loc 0 — position (vec3, float)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(SkinVertex),
                          reinterpret_cast<const void*>(offsetof(SkinVertex, position)));

    // loc 1 — normal (vec3, float)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(SkinVertex),
                          reinterpret_cast<const void*>(offsetof(SkinVertex, normal)));

    // loc 2 — uv (vec2, float)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(SkinVertex),
                          reinterpret_cast<const void*>(offsetof(SkinVertex, uv)));

    // loc 3 — jointIndices (uvec4, unsigned int)
    // IMPORTANT: must use glVertexAttribIPointer for integer attributes.
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_INT,
                           sizeof(SkinVertex),
                           reinterpret_cast<const void*>(offsetof(SkinVertex, jointIndices)));

    // loc 4 — jointWeights (vec4, float)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE,
                          sizeof(SkinVertex),
                          reinterpret_cast<const void*>(offsetof(SkinVertex, jointWeights)));

    glBindVertexArray(0);

    m_Loaded = true;
}

AnimatedMesh::~AnimatedMesh()
{
    if (m_Vao) glDeleteVertexArrays(1, &m_Vao);
    if (m_Vbo) glDeleteBuffers(1, &m_Vbo);
    if (m_Ebo) glDeleteBuffers(1, &m_Ebo);
}

void AnimatedMesh::Draw() const
{
    if (!m_Loaded)
        return;
    glBindVertexArray(m_Vao);
    glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace GeoFPS
