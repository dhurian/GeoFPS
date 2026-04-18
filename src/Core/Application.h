#pragma once

#include "Assets/GltfImporter.h"
#include "Assets/ObjImporter.h"
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

struct ImportedAsset
{
    std::string name {"Asset 1"};
    std::string path;
    ImportedAssetData assetData;
    bool selected {false};
    bool useGeographicPlacement {false};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDegrees {0.0f, 0.0f, 0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 tint {1.0f, 1.0f, 1.0f};
    bool loaded {false};
};

struct AssetClipboardEntry
{
    std::string name;
    std::string path;
    bool useGeographicPlacement {false};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    float rotationZDegrees {0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 tint {1.0f, 1.0f, 1.0f};
};

struct SunSettings
{
    bool useGeographicSun {true};
    int year {2026};
    int month {4};
    int day {12};
    float localTimeHours {16.0f};
    float utcOffsetHours {2.0f};
    float illuminance {1.0f};
    float ambientStrength {0.22f};
    float skyBrightness {1.0f};
    float manualAzimuthDegrees {220.0f};
    float manualElevationDegrees {32.0f};
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
    void RenderMiniMap();
    void RenderCameraHud();
    void RenderEditor();
    void RenderTerrainDatasetWindow();
    void RenderSunWindow();
    void RenderAerialOverlayWindow();
    void RenderBlenderAssetsWindow();
    bool LoadImportedAsset(ImportedAsset& asset);
    bool DeleteImportedAsset(int index);
    ImportedAsset* GetActiveImportedAsset();
    const ImportedAsset* GetActiveImportedAsset() const;
    size_t GetSelectedImportedAssetCount() const;
    void CopySelectedImportedAssets();
    void PasteCopiedImportedAssets();
    void RenderSunControls();
    float SampleTerrainHeightAt(double latitude, double longitude) const;
    bool SaveWorldToFile(const std::string& path);
    bool LoadWorldFromFile(const std::string& path);
    bool LoadBlenderAssetsFromFile(const std::string& path);
    bool WriteCurrentWorldReadout(const std::string& path) const;
    void UpdateImportedAssetPositionFromGeographic(ImportedAsset& asset) const;

    Window m_Window;
    Camera m_Camera;
    FPSController m_FPSController;
    std::unique_ptr<Shader> m_TerrainShader;
    std::unique_ptr<Shader> m_AssetShader;
    std::unique_ptr<Mesh> m_TerrainMesh;
    Texture m_OverlayTexture;
    std::vector<TerrainDataset> m_TerrainDatasets;
    std::vector<ImportedAsset> m_ImportedAssets;
    std::vector<TerrainPoint> m_TerrainPoints;
    GeoReference m_GeoReference {};
    TerrainBuildSettings m_TerrainSettings {};
    bool m_MouseCaptured {true};
    bool m_ShowTerrainDatasetWindow {false};
    bool m_ShowSunWindow {false};
    bool m_ShowAerialOverlayWindow {false};
    bool m_ShowBlenderAssetsWindow {false};
    int m_ActiveTerrainIndex {0};
    int m_ActiveImportedAssetIndex {0};
    int m_SelectedCityPresetIndex {0};
    SunSettings m_SunSettings {};
    std::string m_WorldName {"World 1"};
    std::string m_WorldFilePath {"assets/worlds/world_template.geofpsworld"};
    std::string m_BlenderAssetsFilePath {"assets/worlds/blender_assets_template.txt"};
    std::string m_WorldReadoutFilePath {"assets/worlds/current_world_readout.txt"};
    std::vector<AssetClipboardEntry> m_AssetClipboard;
    std::string m_StatusMessage;
};
} // namespace GeoFPS
