#pragma once

#include "Renderer/IRenderer.h"

namespace GeoFPS
{
class OpenGLRenderer final : public IRenderer
{
  public:
    [[nodiscard]] RendererBackend Backend() const override;
    [[nodiscard]] std::string_view BackendName() const override;

    void BeginFrame(const FrameRenderPacket& frame) override;
    void DrawTerrain(const TerrainRenderPacket& terrain) override;
    void DrawAssets(const std::vector<AssetRenderPacket>& assets) override;
    void DrawLines(const std::vector<LineRenderPacket>& lines) override;
    void EndFrame() override;
};
} // namespace GeoFPS
