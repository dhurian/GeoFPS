#include "Renderer/OpenGLRenderer.h"

namespace GeoFPS
{
RendererBackend OpenGLRenderer::Backend() const
{
    return RendererBackend::OpenGL;
}

std::string_view OpenGLRenderer::BackendName() const
{
    return "OpenGL";
}

void OpenGLRenderer::BeginFrame(const FrameRenderPacket&)
{
}

void OpenGLRenderer::DrawTerrain(const TerrainRenderPacket&)
{
}

void OpenGLRenderer::DrawAssets(const std::vector<AssetRenderPacket>&)
{
}

void OpenGLRenderer::DrawLines(const std::vector<LineRenderPacket>&)
{
}

void OpenGLRenderer::EndFrame()
{
}
} // namespace GeoFPS
