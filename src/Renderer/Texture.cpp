#include "Renderer/Texture.h"

#include <glad/glad.h>
#include <stb_image.h>
#include <utility>
#include <vector>

namespace GeoFPS
{
namespace
{
unsigned int GetFallbackTextureId()
{
    static unsigned int fallbackTextureId = 0;
    if (fallbackTextureId == 0)
    {
        constexpr unsigned char whitePixel[4] = {255, 255, 255, 255};
        glGenTextures(1, &fallbackTextureId);
        glBindTexture(GL_TEXTURE_2D, fallbackTextureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    return fallbackTextureId;
}

bool UploadTexture(unsigned int& textureId, int& storedWidth, int& storedHeight, const unsigned char* pixels, int width, int height)
{
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    storedWidth = width;
    storedHeight = height;
    return true;
}
} // namespace

Texture::~Texture()
{
    Reset();
}

Texture::Texture(Texture&& other) noexcept
{
    *this = std::move(other);
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_TextureId = other.m_TextureId;
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        other.m_TextureId = 0;
        other.m_Width = 0;
        other.m_Height = 0;
    }
    return *this;
}

bool Texture::LoadFromFile(const std::string& path)
{
    Reset();

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr)
    {
        return false;
    }

    const bool uploaded = UploadTexture(m_TextureId, m_Width, m_Height, pixels, width, height);

    stbi_image_free(pixels);
    return uploaded;
}

bool Texture::LoadFromMemory(const unsigned char* pixels, int width, int height, int channels)
{
    Reset();
    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    if (channels == 4)
    {
        return UploadTexture(m_TextureId, m_Width, m_Height, pixels, width, height);
    }

    std::vector<unsigned char> converted(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 255u);
    for (int pixelIndex = 0; pixelIndex < width * height; ++pixelIndex)
    {
        const int sourceOffset = pixelIndex * channels;
        const int targetOffset = pixelIndex * 4;
        converted[static_cast<size_t>(targetOffset)] = pixels[sourceOffset];
        converted[static_cast<size_t>(targetOffset + 1)] = channels > 1 ? pixels[sourceOffset + 1] : pixels[sourceOffset];
        converted[static_cast<size_t>(targetOffset + 2)] = channels > 2 ? pixels[sourceOffset + 2] : pixels[sourceOffset];
        converted[static_cast<size_t>(targetOffset + 3)] = channels > 3 ? pixels[sourceOffset + 3] : 255u;
    }

    return UploadTexture(m_TextureId, m_Width, m_Height, converted.data(), width, height);
}

void Texture::Bind(unsigned int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_TextureId != 0 ? m_TextureId : GetFallbackTextureId());
}

void Texture::Reset()
{
    if (m_TextureId != 0)
    {
        glDeleteTextures(1, &m_TextureId);
        m_TextureId = 0;
    }
    m_Width = 0;
    m_Height = 0;
}
} // namespace GeoFPS
