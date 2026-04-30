#pragma once

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

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
    void SetFloat(const std::string& name, float value) const;
    void SetInt(const std::string& name, int value) const;
    void SetVec2(const std::string& name, const glm::vec2& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetMat4(const std::string& name, const glm::mat4& value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;

  private:
    [[nodiscard]] int GetUniformLocation(const std::string& name) const;

    unsigned int m_ProgramId {0};
    mutable std::unordered_map<std::string, int> m_UniformLocations;
};
} // namespace GeoFPS
