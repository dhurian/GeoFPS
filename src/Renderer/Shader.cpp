#include "Renderer/Shader.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

namespace GeoFPS
{
namespace
{
std::string ReadFile(const std::string& path)
{
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool CheckCompileStatus(unsigned int shaderId, const char* label)
{
    int success = 0;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE)
    {
        return true;
    }

    char infoLog[1024];
    glGetShaderInfoLog(shaderId, sizeof(infoLog), nullptr, infoLog);
    std::cerr << label << " compile error: " << infoLog << '\n';
    return false;
}

bool CheckLinkStatus(unsigned int programId)
{
    int success = 0;
    glGetProgramiv(programId, GL_LINK_STATUS, &success);
    if (success == GL_TRUE)
    {
        return true;
    }

    char infoLog[1024];
    glGetProgramInfoLog(programId, sizeof(infoLog), nullptr, infoLog);
    std::cerr << "Program link error: " << infoLog << '\n';
    return false;
}
} // namespace

Shader::~Shader()
{
    if (m_ProgramId != 0)
    {
        glDeleteProgram(m_ProgramId);
    }
}

bool Shader::LoadFromFiles(const std::string& vertexPath, const std::string& fragmentPath)
{
    if (m_ProgramId != 0)
    {
        glDeleteProgram(m_ProgramId);
        m_ProgramId = 0;
    }
    m_UniformLocations.clear();

    const std::string vertexSource = ReadFile(vertexPath);
    const std::string fragmentSource = ReadFile(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty())
    {
        return false;
    }

    const char* vertexSourcePtr = vertexSource.c_str();
    const char* fragmentSourcePtr = fragmentSource.c_str();

    const unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSourcePtr, nullptr);
    glCompileShader(vertexShader);
    if (!CheckCompileStatus(vertexShader, "Vertex shader"))
    {
        glDeleteShader(vertexShader);
        return false;
    }

    const unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSourcePtr, nullptr);
    glCompileShader(fragmentShader);
    if (!CheckCompileStatus(fragmentShader, "Fragment shader"))
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    const unsigned int programId = glCreateProgram();
    glAttachShader(programId, vertexShader);
    glAttachShader(programId, fragmentShader);
    glLinkProgram(programId);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!CheckLinkStatus(programId))
    {
        glDeleteProgram(programId);
        return false;
    }

    m_ProgramId = programId;
    return true;
}

void Shader::Bind() const
{
    glUseProgram(m_ProgramId);
}

void Shader::SetFloat(const std::string& name, float value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniform1f(location, value);
}

void Shader::SetInt(const std::string& name, int value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniform1i(location, value);
}

void Shader::SetVec2(const std::string& name, const glm::vec2& value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniform2fv(location, 1, &value[0]);
}

void Shader::SetVec4(const std::string& name, const glm::vec4& value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniform4fv(location, 1, &value[0]);
}

void Shader::SetMat4(const std::string& name, const glm::mat4& value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
}

void Shader::SetVec3(const std::string& name, const glm::vec3& value) const
{
    const int location = GetUniformLocation(name);
    if (location < 0)
    {
        return;
    }
    glUniform3fv(location, 1, &value[0]);
}

void Shader::SetMat4Array(const std::string& name, const std::vector<glm::mat4>& matrices) const
{
    if (matrices.empty())
        return;
    const int location = GetUniformLocation(name + "[0]");
    if (location < 0)
        return;
    glUniformMatrix4fv(location,
                       static_cast<GLsizei>(matrices.size()),
                       GL_FALSE,
                       glm::value_ptr(matrices[0]));
}

int Shader::GetUniformLocation(const std::string& name) const
{
    const auto existing = m_UniformLocations.find(name);
    if (existing != m_UniformLocations.end())
    {
        return existing->second;
    }

    const int location = glGetUniformLocation(m_ProgramId, name.c_str());
    m_UniformLocations.emplace(name, location);
    return location;
}
} // namespace GeoFPS
