#pragma once

#include <glm/glm.hpp>
#include <string>

namespace GeoFPS
{
class Shader
{
  public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool LoadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
    void Bind() const;
    void SetMat4(const std::string& name, const glm::mat4& value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;

  private:
    unsigned int m_ProgramId {0};
};
} // namespace GeoFPS
