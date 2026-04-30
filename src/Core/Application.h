#pragma once

#include "Assets/GltfImporter.h"
#include "Assets/ObjImporter.h"
#include "Core/BackgroundJobQueue.h"
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
#include "Terrain/TerrainProfile.h"
#include <future>
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
    Texture texture;
};

struct TerrainDatasetBounds
{
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    bool valid {false};
};

struct TerrainMeshChunk
{
    MeshData meshData;
    std::unique_ptr<Mesh> mesh;
    float minX {0.0f};
    float maxX {0.0f};
    float minY {0.0f};
    float maxY {0.0f};
    float minZ {0.0f};
    float maxZ {0.0f};
};

struct TerrainTile
{
    std::string path;
    int row {0};
    int col {0};
    size_t pointCount {0};
    TerrainDatasetBounds bounds;
    std::vector<TerrainPoint> points;
    TerrainHeightGrid heightGrid;
    MeshData terrainMeshData;
    std::vector<TerrainMeshChunk> chunks;
    bool loaded {false};
    bool meshLoaded {false};
    bool loading {false};
};

struct TerrainDataset
{
    std::string name {"Terrain 1"};
    std::string path {"assets/data/sample_terrain.csv"};
    std::string tileManifestPath;
    bool hasTileManifest {false};
    std::vector<TerrainPoint> points;
    GeoReference geoReference {};
    TerrainBuildSettings settings {};
    TerrainHeightGrid heightGrid;
    MeshData terrainMeshData;
    TerrainDatasetBounds bounds;
    std::vector<TerrainTile> tiles;
    std::vector<TerrainMeshChunk> chunks;
    std::vector<OverlayEntry> overlays;
    std::unique_ptr<Mesh> mesh;
    bool visible {true};
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
    bool showLabel {true};
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
    bool showLabel {true};
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

struct SkySettings
{
    bool      enabled           {true};              // render procedural skybox; false = legacy flat clear color
    bool      useSunDrivenColor {true};              // derive zenith/horizon colors from SunParameters
    glm::vec3 zenithColor       {0.10f, 0.20f, 0.50f}; // used when useSunDrivenColor = false
    glm::vec3 horizonColor      {0.50f, 0.70f, 0.90f};
    float     horizonSharpness  {4.0f};              // gradient exponent (higher = sharper horizon band)
    bool      showSunDisk       {true};
    float     sunDiskSize       {0.008f};            // dot-product threshold — larger = bigger disk
    float     sunDiskIntensity  {3.0f};              // brightness multiplier applied to the disk
    // --- Clouds ---
    bool      cloudsEnabled     {false};
    float     cloudCoverage     {0.45f};             // 0 = clear sky, 1 = fully overcast
    float     cloudDensity      {0.85f};             // maximum cloud opacity (0–1)
    float     cloudScale        {1.0f};              // formation size (lower = bigger clouds)
    float     cloudSpeedX       {12.0f};             // wind X component in m/s
    float     cloudSpeedY       {5.0f};              // wind Y component in m/s (cross-wind)
    float     cloudAltitude     {1500.0f};           // cloud layer height above ground in metres
    bool      cloudAutoColor    {true};              // derive colour from sun (warm sunset tint)
    glm::vec3 cloudColor        {1.0f, 1.0f, 1.0f}; // manual colour when cloudAutoColor = false
};

enum class ProfileElevationScaleMode
{
    Auto,
    OneX,
    TwoX,
    FiveX,
    TenX,
    Fixed
};

enum class ProfileMapSizeMode
{
    Small,
    Medium,
    Large,
    Fill
};

enum class WorkspaceSection
{
    World,
    Terrain,
    Profiles,
    Assets,
    Lighting,
    Diagnostics
};

class Application
{
  public:
    bool Initialize();
    void Run();
    void Shutdown();

    struct TerrainBuildResult
    {
        bool success {false};
        std::string statusMessage;
        std::vector<TerrainPoint> points;
        GeoReference geoReference {};
        TerrainBuildSettings settings {};
        TerrainHeightGrid heightGrid;
        MeshData meshData;
        std::vector<TerrainMeshChunkData> chunks;
        TerrainDatasetBounds bounds;
    };

    struct TerrainBuildJob
    {
        int terrainIndex {-1};
        std::future<TerrainBuildResult> future;
    };

    struct TerrainTileBuildResult
    {
        bool success {false};
        std::string statusMessage;
        int terrainIndex {-1};
        int tileIndex {-1};
        std::string path;
        std::vector<TerrainPoint> points;
        TerrainHeightGrid heightGrid;
        MeshData meshData;
        std::vector<TerrainMeshChunkData> chunks;
    };

    struct TerrainTileBuildJob
    {
        int terrainIndex {-1};
        int tileIndex {-1};
        std::future<TerrainTileBuildResult> future;
    };

    struct AssetLoadResult
    {
        bool success {false};
        std::string statusMessage;
        ImportedAssetData assetData;
    };

    struct AssetLoadJob
    {
        int assetIndex {-1};
        std::future<AssetLoadResult> future;
    };

    struct ProfileSampleBuildResult
    {
        bool success {false};
        std::string statusMessage;
        std::vector<TerrainProfile> profiles;
    };

    struct ProfileSampleBuildJob
    {
        std::future<ProfileSampleBuildResult> future;
    };

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
    bool LoadTerrainTile(TerrainDataset& dataset, TerrainTile& tile);
    bool StartTerrainTileLoadJob(int terrainIndex, int tileIndex);
    bool StartTerrainBuildJob(int terrainIndex);
    void ProcessBackgroundJobs();
    bool ActivateTerrainDataset(int index);
    bool LoadOverlayImage(OverlayEntry& overlay);
    bool LoadActiveOverlayImage();
    bool DeleteTerrainDataset(int index);
    bool DeleteActiveOverlay();
    bool RebuildTerrainMesh(TerrainDataset& dataset);
    bool RebuildTerrain();
    void ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition, const std::vector<TerrainPoint>& points) const;
    void ResetOverlayToTerrainBounds(GeoImageDefinition& imageDefinition) const;
    void LoadActiveTerrainIntoScene();
    void RenderMainMenuBar();
    void SetupImGui();
    void ShutdownImGui();
    void BeginImGuiFrame();
    void RenderMiniMap();
    void RenderMiniMapWindow();
    void RenderCameraHud();
    void RenderEditor();
    void RenderTerrainDatasetWindow();
    void RenderSunWindow();
    void RenderAerialOverlayWindow();
    void RenderBlenderAssetsWindow();
    void RenderTerrainProfilesWindow();
    void RenderWorldTerrainProfiles();
    void RenderTerrainProfileMap(TerrainProfile& activeProfile);
    void RenderTerrainProfileGraph(TerrainProfile& activeProfile);
    void RenderTerrainProfileToolbar(TerrainProfile& activeProfile);
    void RenderTerrainProfileDetails(TerrainProfile& activeProfile);
    bool LoadImportedAsset(ImportedAsset& asset);
    bool StartImportedAssetLoadJob(int assetIndex);
    bool DeleteImportedAsset(int index);
    size_t DeleteSelectedImportedAssets();
    void SelectAllImportedAssets(bool selected);
    size_t SnapSelectedImportedAssetsToTerrain();
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
    [[nodiscard]] bool TerrainProfileIncludesTerrain(const TerrainProfile& profile, const TerrainDataset& dataset) const;
    void SetTerrainProfileIncludesTerrain(TerrainProfile& profile, const TerrainDataset& dataset, bool included);
    void EnsureTerrainProfileHasTerrainSelection(TerrainProfile& profile);
    [[nodiscard]] float SampleTerrainHeightAt(const TerrainDataset& dataset, double latitude, double longitude) const;
    [[nodiscard]] float SampleRenderedTerrainLocalHeightAt(const TerrainDataset& dataset, double latitude, double longitude) const;
    [[nodiscard]] const TerrainDataset* GetPrimaryTerrainForProfile(const TerrainProfile& profile) const;
    [[nodiscard]] std::vector<TerrainProfileSample> BuildTerrainProfileSamplesForDataset(const TerrainProfile& profile,
                                                                                         const TerrainDataset& dataset) const;
    void RebuildTerrainProfileSamples(TerrainProfile& profile);
    void RebuildAllTerrainProfileSamplesNow();
    bool StartTerrainProfileSampleJob();
    void RebuildAllTerrainProfileSamples();
    void MarkTerrainIsolinesDirty();
    void MarkTerrainIsolineSampleGridDirty();
    void RebuildTerrainIsolineSampleGridIfNeeded();
    void RebuildTerrainIsolines();
    void RebuildTerrainIsolinesIfNeeded();
    bool ExportTerrainProfileFile(const std::string& path);
    bool ImportTerrainProfileFile(const std::string& path);
    [[nodiscard]] size_t CountSceneTriangles() const;

    Window m_Window;
    Camera m_Camera;
    FPSController m_FPSController;
    std::unique_ptr<BackgroundJobQueue> m_BackgroundJobs;
    std::vector<TerrainBuildJob> m_TerrainBuildJobs;
    std::vector<TerrainTileBuildJob> m_TerrainTileBuildJobs;
    std::vector<AssetLoadJob> m_AssetLoadJobs;
    std::vector<ProfileSampleBuildJob> m_ProfileSampleBuildJobs;
    std::unique_ptr<Shader> m_TerrainShader;
    std::unique_ptr<Shader> m_AssetShader;
    std::unique_ptr<Shader> m_LineShader;
    std::unique_ptr<Shader> m_SkyShader;
    std::unique_ptr<Mesh>   m_SkyboxMesh;
    TerrainHeightGrid m_TerrainHeightGrid;
    TerrainIsolineSettings m_IsolineSettings {};
    TerrainIsolineSampleGrid m_TerrainIsolineSampleGrid;
    std::vector<TerrainIsolineSegment> m_TerrainIsolines;
    std::vector<TerrainDataset> m_TerrainDatasets;
    std::vector<ImportedAsset> m_ImportedAssets;
    std::vector<TerrainProfile> m_TerrainProfiles;
    std::vector<TerrainPoint> m_TerrainPoints;
    GeoReference m_GeoReference {};
    TerrainBuildSettings m_TerrainSettings {};
    bool m_MouseCaptured {true};
    bool m_ShowTerrainDatasetWindow {false};
    bool m_ShowMiniMapWindow {false};
    bool m_ShowSunWindow {false};
    bool m_ShowAerialOverlayWindow {false};
    bool m_ShowBlenderAssetsWindow {false};
    bool m_ShowTerrainProfilesWindow {false};
    bool m_ShowWorkspaceWindow {false};
    bool m_ProfileDrawMode {false};
    bool m_ProfileEditMode {true};
    bool m_ShowProfileHeightLabels {true};
    bool m_ShowProfileAerialImage {true};
    bool m_ShowProfileSamples {false};
    bool m_ProfileAuxiliaryDrawMode {false};
    bool m_ProfileGraphAuxiliaryInsertMode {false};
    bool m_ProfileGraphHoverActive {false};
    bool m_TerrainIsolinesDirty {true};
    bool m_TerrainIsolineSampleGridDirty {true};
    bool m_ProfileMapViewInitialized {false};
    bool m_ProfileMapIsPanning {false};
    bool m_UseGpuIsolineGeneration {true};
    bool m_TerrainIsolinesUsedGpu {false};
    int m_ActiveTerrainIndex {0};
    int m_ActiveImportedAssetIndex {0};
    int m_ActiveTerrainProfileIndex {0};
    int m_SelectedProfileVertexIndex {-1};
    int m_SelectedProfileSampleIndex {-1};
    int m_HoveredProfileSampleIndex {-1};
    TerrainProfileSample m_ProfileGraphHoverSample {};
    ProfileElevationScaleMode m_ProfileScaleMode {ProfileElevationScaleMode::Auto};
    ProfileMapSizeMode m_ProfileMapSizeMode {ProfileMapSizeMode::Large};
    WorkspaceSection m_ActiveWorkspaceSection {WorkspaceSection::World};
    float m_ProfileDetailsWidth {360.0f};
    float m_ProfileFixedMinHeight {0.0f};
    float m_ProfileFixedMaxHeight {100.0f};
    double m_ProfileMapMinLatitude {0.0};
    double m_ProfileMapMaxLatitude {0.0};
    double m_ProfileMapMinLongitude {0.0};
    double m_ProfileMapMaxLongitude {0.0};
    float m_ProfileMapLastWidth {720.0f};
    glm::vec2 m_ProfileMapLastPanMouse {0.0f};
    int m_SelectedCityPresetIndex {0};
    SunSettings m_SunSettings {};
    SkySettings m_SkySettings {};
    float       m_ElapsedTime {0.0f};  // accumulated frame time — drives cloud animation
    std::string m_WorldName {"World 1"};
    std::string m_WorldFilePath {"assets/worlds/world_template.geofpsworld"};
    std::string m_BlenderAssetsFilePath {"assets/worlds/blender_assets_template.txt"};
    std::string m_WorldReadoutFilePath {"assets/worlds/current_world_readout.txt"};
    std::string m_TerrainProfileFilePath {"assets/worlds/terrain_profiles.geofpsprofile"};
    std::vector<AssetClipboardEntry> m_AssetClipboard;
    glm::vec3 m_AssetPasteOffset {2.0f, 0.0f, 2.0f};
    unsigned int m_ProfileLineVao {0};
    unsigned int m_ProfileLineVbo {0};
    std::string m_StatusMessage;
};
} // namespace GeoFPS
