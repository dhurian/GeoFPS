#pragma once

#include <string>

namespace GeoFPS
{
class Texture
{
  public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool LoadFromFile(const std::string& path);
    void Bind(unsigned int slot = 0) const;
    void Reset();

    [[nodiscard]] bool IsLoaded() const { return m_TextureId != 0; }
    [[nodiscard]] int GetWidth() const { return m_Width; }
    [[nodiscard]] int GetHeight() const { return m_Height; }

  private:
    unsigned int m_TextureId {0};
    int m_Width {0};
    int m_Height {0};
};
} // namespace GeoFPS
