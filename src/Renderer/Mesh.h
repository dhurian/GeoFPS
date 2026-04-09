#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace GeoFPS
{
struct Vertex
{
    glm::vec3 position {};
    glm::vec3 normal {0.0f, 1.0f, 0.0f};
    glm::vec2 uv {};
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
};

class Mesh
{
  public:
    explicit Mesh(const MeshData& meshData);
    ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    void Draw() const;

  private:
    void Release();

    unsigned int m_Vao {0};
    unsigned int m_Vbo {0};
    unsigned int m_Ebo {0};
    unsigned int m_IndexCount {0};
};
} // namespace GeoFPS
