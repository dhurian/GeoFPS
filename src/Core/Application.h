#pragma once

#include "Core/Window.h"
#include "Game/FPSController.h"
#include "Math/GeoConverter.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Shader.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace GeoFPS
{
class Application
{
  public:
    bool Initialize();
    void Run();
    void Shutdown();

  private:
    void ProcessInput(float deltaTime);
    void Update(float deltaTime);
    void Render();
    bool LoadStartupTerrain();
    bool RebuildTerrain();
    void FrameCameraToTerrain();
    void SetupImGui();
    void ShutdownImGui();
    void BeginImGuiFrame();
    void RenderEditor();

    Window m_Window;
    Camera m_Camera;
    FPSController m_FPSController;
    std::unique_ptr<Shader> m_TerrainShader;
    std::unique_ptr<Mesh> m_TerrainMesh;
    std::vector<TerrainPoint> m_TerrainPoints;
    GeoReference m_GeoReference {};
    TerrainBuildSettings m_TerrainSettings {};
    bool m_MouseCaptured {true};
    std::string m_TerrainPath {"assets/data/sample_terrain.csv"};
};
} // namespace GeoFPS
