#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace GeoFPS
{
enum class RendererBackend
{
    OpenGL,
    Metal
};

struct FrameRenderPacket
{
    glm::mat4 view {1.0f};
    glm::mat4 projection {1.0f};
    glm::vec3 cameraPosition {0.0f};
    glm::vec3 clearColor {0.0f};
    glm::ivec2 viewportSize {0};
};

struct TerrainRenderChunkPacket
{
    glm::mat4 model {1.0f};
    size_t vertexCount {0};
    size_t indexCount {0};
};

struct TerrainRenderPacket
{
    std::vector<TerrainRenderChunkPacket> visibleChunks;
    bool colorByHeight {false};
    bool hasOverlay {false};
};

struct AssetRenderPacket
{
    glm::mat4 transform {1.0f};
    glm::vec4 tint {1.0f};
};

struct LineRenderPacket
{
    std::vector<glm::vec3> vertices;
    glm::vec4 color {1.0f};
    float thickness {1.0f};
};

class IRenderer
{
  public:
    virtual ~IRenderer() = default;

    [[nodiscard]] virtual RendererBackend Backend() const = 0;
    [[nodiscard]] virtual std::string_view BackendName() const = 0;

    virtual void BeginFrame(const FrameRenderPacket& frame) = 0;
    virtual void DrawTerrain(const TerrainRenderPacket& terrain) = 0;
    virtual void DrawAssets(const std::vector<AssetRenderPacket>& assets) = 0;
    virtual void DrawLines(const std::vector<LineRenderPacket>& lines) = 0;
    virtual void EndFrame() = 0;
};
} // namespace GeoFPS
