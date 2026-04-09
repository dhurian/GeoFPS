#pragma once

#include "Core/Window.h"
#include "Game/FPSController.h"
#include "Mapping/GeoImage.h"
#include "Math/GeoConverter.h"
#include "Renderer/Camera.h"
#include "Renderer/Mesh.h"
#include "Renderer/Shader.h"
#include "Renderer/Texture.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace GeoFPS
{
struct OverlayEntry
{
    std::string name {"Overlay 1"};
    GeoImageDefinition image {};
};

struct TerrainDataset
{
    std::string name {"Terrain 1"};
    std::string path {"assets/data/sample_terrain.csv"};
    std::vector<TerrainPoint> points;
    GeoReference geoReference {};
    TerrainBuildSettings settings {};
    std::vector<OverlayEntry> overlays;
    bool loaded {false};
    int activeOverlayIndex {0};
};

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
    void InitializeProject();
    TerrainDataset* GetActiveTerrainDataset();
    const TerrainDataset* GetActiveTerrainDataset() const;
    OverlayEntry* GetActiveOverlayEntry();
    const OverlayEntry* GetActiveOverlayEntry() const;
    bool LoadTerrainDataset(TerrainDataset& dataset);
    bool ActivateTerrainDataset(int index);
    bool LoadOverlayImage(OverlayEntry& overlay);
    bool LoadActiveOverlayImage();
    bool DeleteTerrainDataset(int index);
    bool DeleteActiveOverlay();
    bool RebuildTerrain();
    void ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition, const std::vector<TerrainPoint>& points) const;
    void ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition) const;
    void LoadActiveTerrainIntoScene();
    void RenderMainMenuBar();
    void SetupImGui();
    void ShutdownImGui();
    void BeginImGuiFrame();
    void RenderEditor();

    Window m_Window;
    Camera m_Camera;
    FPSController m_FPSController;
    std::unique_ptr<Shader> m_TerrainShader;
    std::unique_ptr<Mesh> m_TerrainMesh;
    Texture m_OverlayTexture;
    std::vector<TerrainDataset> m_TerrainDatasets;
    std::vector<TerrainPoint> m_TerrainPoints;
    GeoReference m_GeoReference {};
    TerrainBuildSettings m_TerrainSettings {};
    bool m_MouseCaptured {true};
    int m_ActiveTerrainIndex {0};
    std::string m_StatusMessage;
};
} // namespace GeoFPS
