#include "Renderer/Mesh.h"

#include <glad/glad.h>
#include <utility>

namespace GeoFPS
{
Mesh::Mesh(const MeshData& meshData) : m_IndexCount(static_cast<unsigned int>(meshData.indices.size()))
{
    glGenVertexArrays(1, &m_Vao);
    glGenBuffers(1, &m_Vbo);
    glGenBuffers(1, &m_Ebo);

    glBindVertexArray(m_Vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(meshData.vertices.size() * sizeof(Vertex)),
                 meshData.vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(meshData.indices.size() * sizeof(unsigned int)),
                 meshData.indices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));

    glBindVertexArray(0);
}

Mesh::~Mesh()
{
    Release();
}

Mesh::Mesh(Mesh&& other) noexcept
{
    *this = std::move(other);
}

Mesh& Mesh::operator=(Mesh&& other) noexcept
{
    if (this != &other)
    {
        Release();
        m_Vao = other.m_Vao;
        m_Vbo = other.m_Vbo;
        m_Ebo = other.m_Ebo;
        m_IndexCount = other.m_IndexCount;
        other.m_Vao = 0;
        other.m_Vbo = 0;
        other.m_Ebo = 0;
        other.m_IndexCount = 0;
    }
    return *this;
}

void Mesh::Draw() const
{
    glBindVertexArray(m_Vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_IndexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::Release()
{
    if (m_Ebo != 0)
    {
        glDeleteBuffers(1, &m_Ebo);
        m_Ebo = 0;
    }
    if (m_Vbo != 0)
    {
        glDeleteBuffers(1, &m_Vbo);
        m_Vbo = 0;
    }
    if (m_Vao != 0)
    {
        glDeleteVertexArrays(1, &m_Vao);
        m_Vao = 0;
    }
    m_IndexCount = 0;
}
} // namespace GeoFPS
