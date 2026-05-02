#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace GeoFPS
{
using namespace ApplicationInternal;
namespace
{
double NowMs()
{
    return glfwGetTime() * 1000.0;
}

glm::dvec3 TerrainCoordinateToLocal(const TerrainDataset& dataset, double latitude, double longitude, double height)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {latitude, height, longitude};
    }

    return GeoConverter(dataset.geoReference).ToLocal(latitude, longitude, height);
}

struct RenderOverlayPlacement
{
    const OverlayEntry* overlay {nullptr};
    glm::vec2 origin {0.0f};
    glm::vec2 axisU {0.0f};
    glm::vec2 axisV {0.0f};
};

std::vector<RenderOverlayPlacement> BuildRenderableOverlays(const TerrainDataset& dataset)
{
    std::vector<RenderOverlayPlacement> overlays;
    for (const OverlayEntry& overlay : dataset.overlays)
    {
        const GeoImageDefinition& overlayDefinition = overlay.image;
        if (!overlayDefinition.enabled || !overlayDefinition.loaded || !overlay.texture.IsLoaded())
        {
            continue;
        }

        RenderOverlayPlacement placement;
        placement.overlay = &overlay;
        placement.origin = ToGroundPlane(TerrainCoordinateToLocal(
            dataset, overlayDefinition.topLeft.latitude, overlayDefinition.topLeft.longitude, dataset.geoReference.originHeight));
        placement.axisU = ToGroundPlane(TerrainCoordinateToLocal(
                              dataset, overlayDefinition.topRight.latitude, overlayDefinition.topRight.longitude, dataset.geoReference.originHeight)) -
                          placement.origin;
        placement.axisV = ToGroundPlane(TerrainCoordinateToLocal(
                              dataset,
                              overlayDefinition.bottomLeft.latitude,
                              overlayDefinition.bottomLeft.longitude,
                              dataset.geoReference.originHeight)) -
                          placement.origin;
        overlays.push_back(placement);
    }

    return overlays;
}

void ApplyOverlayWorldTranslation(std::vector<RenderOverlayPlacement>& overlays, const glm::vec3& translation)
{
    const glm::vec2 groundTranslation(translation.x, translation.z);
    for (RenderOverlayPlacement& overlay : overlays)
    {
        overlay.origin += groundTranslation;
    }
}

glm::vec2 TerrainHeightColorRange(const TerrainDataset& dataset, const glm::vec3& terrainTranslation)
{
    if (!dataset.settings.autoHeightColorRange)
    {
        return {dataset.settings.heightColorMin + terrainTranslation.y,
                dataset.settings.heightColorMax + terrainTranslation.y};
    }

    const double minHeight = dataset.bounds.valid ? dataset.bounds.minHeight : dataset.geoReference.originHeight;
    const double maxHeight = dataset.bounds.valid ? dataset.bounds.maxHeight : dataset.geoReference.originHeight + 1.0;
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {static_cast<float>(minHeight * static_cast<double>(dataset.settings.heightScale)) + terrainTranslation.y,
                static_cast<float>(maxHeight * static_cast<double>(dataset.settings.heightScale)) + terrainTranslation.y};
    }

    return {static_cast<float>((minHeight - dataset.geoReference.originHeight) *
                               static_cast<double>(dataset.settings.heightScale)) +
                terrainTranslation.y,
            static_cast<float>((maxHeight - dataset.geoReference.originHeight) *
                               static_cast<double>(dataset.settings.heightScale)) +
                terrainTranslation.y};
}

void ApplyTerrainColorUniforms(Shader& shader, const TerrainDataset& dataset, const glm::vec3& terrainTranslation)
{
    const glm::vec2 heightRange = TerrainHeightColorRange(dataset, terrainTranslation);
    shader.SetInt("uColorByHeight", dataset.settings.colorByHeight ? 1 : 0);
    shader.SetFloat("uHeightColorMin", std::min(heightRange.x, heightRange.y));
    shader.SetFloat("uHeightColorMax", std::max(heightRange.x, heightRange.y));
    shader.SetVec3("uLowHeightColor", dataset.settings.lowHeightColor);
    shader.SetVec3("uMidHeightColor", dataset.settings.midHeightColor);
    shader.SetVec3("uHighHeightColor", dataset.settings.highHeightColor);
}

void ApplyOverlayUniforms(Shader& shader, const RenderOverlayPlacement& placement, int overlayOnly)
{
    const GeoImageDefinition& overlayDefinition = placement.overlay->image;
    shader.SetInt("uUseOverlay", 1);
    shader.SetInt("uOverlayOnly", overlayOnly);
    shader.SetFloat("uOverlayOpacity", overlayDefinition.opacity);
    shader.SetVec2("uOverlayOrigin", placement.origin);
    shader.SetVec2("uOverlayAxisU", placement.axisU);
    shader.SetVec2("uOverlayAxisV", placement.axisV);
    shader.SetInt("uOverlayTexture", 0);
    placement.overlay->texture.Bind(0);
}

struct Frustum
{
    std::array<glm::vec4, 6> planes {};
};

glm::vec4 MatrixRow(const glm::mat4& matrix, int row)
{
    return {matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]};
}

glm::vec4 NormalizePlane(const glm::vec4& plane)
{
    const float length = glm::length(glm::vec3(plane));
    return length > 0.00001f ? plane / length : plane;
}

Frustum BuildFrustumFromViewProjection(const glm::mat4& viewProjection)
{
    const glm::vec4 row0 = MatrixRow(viewProjection, 0);
    const glm::vec4 row1 = MatrixRow(viewProjection, 1);
    const glm::vec4 row2 = MatrixRow(viewProjection, 2);
    const glm::vec4 row3 = MatrixRow(viewProjection, 3);

    Frustum frustum;
    frustum.planes[0] = NormalizePlane(row3 + row0);
    frustum.planes[1] = NormalizePlane(row3 - row0);
    frustum.planes[2] = NormalizePlane(row3 + row1);
    frustum.planes[3] = NormalizePlane(row3 - row1);
    frustum.planes[4] = NormalizePlane(row3 + row2);
    frustum.planes[5] = NormalizePlane(row3 - row2);
    return frustum;
}

bool IsAabbVisible(const Frustum& frustum,
                   const glm::vec3& minBounds,
                   const glm::vec3& maxBounds)
{
    for (const glm::vec4& plane : frustum.planes)
    {
        const glm::vec3 positiveVertex(plane.x >= 0.0f ? maxBounds.x : minBounds.x,
                                       plane.y >= 0.0f ? maxBounds.y : minBounds.y,
                                       plane.z >= 0.0f ? maxBounds.z : minBounds.z);
        if (glm::dot(glm::vec3(plane), positiveVertex) + plane.w < 0.0f)
        {
            return false;
        }
    }
    return true;
}

bool IsTerrainChunkVisible(const TerrainMeshChunk& chunk, const Frustum& frustum, const glm::vec3& terrainTranslation)
{
    constexpr float kChunkBoundsPadding = 4.0f;
    const glm::vec3 minBounds(chunk.minX + terrainTranslation.x - kChunkBoundsPadding,
                              chunk.minY + terrainTranslation.y - kChunkBoundsPadding,
                              chunk.minZ + terrainTranslation.z - kChunkBoundsPadding);
    const glm::vec3 maxBounds(chunk.maxX + terrainTranslation.x + kChunkBoundsPadding,
                              chunk.maxY + terrainTranslation.y + kChunkBoundsPadding,
                              chunk.maxZ + terrainTranslation.z + kChunkBoundsPadding);
    return IsAabbVisible(frustum, minBounds, maxBounds);
}

bool IsTerrainTileVisible(const TerrainDataset& dataset,
                          const TerrainTile& tile,
                          const Frustum& frustum,
                          const glm::vec3& terrainTranslation)
{
    if (!tile.bounds.valid)
    {
        return true;
    }

    constexpr float kTileBoundsPadding = 16.0f;
    const glm::dvec3 corners[] = {
        TerrainCoordinateToLocal(dataset, tile.bounds.minLatitude, tile.bounds.minLongitude, tile.bounds.minHeight),
        TerrainCoordinateToLocal(dataset, tile.bounds.minLatitude, tile.bounds.maxLongitude, tile.bounds.minHeight),
        TerrainCoordinateToLocal(dataset, tile.bounds.maxLatitude, tile.bounds.minLongitude, tile.bounds.maxHeight),
        TerrainCoordinateToLocal(dataset, tile.bounds.maxLatitude, tile.bounds.maxLongitude, tile.bounds.maxHeight),
    };

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (const glm::dvec3& corner : corners)
    {
        const glm::vec3 local(static_cast<float>(corner.x),
                              static_cast<float>(corner.y),
                              static_cast<float>(corner.z));
        minBounds = glm::min(minBounds, local + terrainTranslation);
        maxBounds = glm::max(maxBounds, local + terrainTranslation);
    }

    minBounds -= glm::vec3(kTileBoundsPadding);
    maxBounds += glm::vec3(kTileBoundsPadding);
    return IsAabbVisible(frustum, minBounds, maxBounds);
}
} // namespace

bool Application::Initialize()
{
    if (!m_Window.Create(1600, 900, "GeoFPS"))
    {
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        throw std::runtime_error("glad failed to load OpenGL functions");
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));
    m_Camera.SetPosition({0.0f, 8.0f, 16.0f});

    m_FPSController.AttachWindow(&m_Window);
    m_BaseMoveSpeed = m_FPSController.GetMoveSpeed();
    m_BackgroundJobs = std::make_unique<BackgroundJobQueue>();

    m_TerrainShader = std::make_unique<Shader>();
    if (!m_TerrainShader->LoadFromFiles("assets/shaders/terrain.vert", "assets/shaders/terrain.frag"))
    {
        throw std::runtime_error("Failed to load terrain shaders");
    }

    m_AssetShader = std::make_unique<Shader>();
    if (!m_AssetShader->LoadFromFiles("assets/shaders/asset.vert", "assets/shaders/asset.frag"))
    {
        throw std::runtime_error("Failed to load asset shaders");
    }

    m_LineShader = std::make_unique<Shader>();
    if (!m_LineShader->LoadFromFiles("assets/shaders/line.vert", "assets/shaders/line.frag"))
    {
        throw std::runtime_error("Failed to load line shaders");
    }

    m_SkyShader = std::make_unique<Shader>();
    if (!m_SkyShader->LoadFromFiles("assets/shaders/sky.vert", "assets/shaders/sky.frag"))
    {
        throw std::runtime_error("Failed to load sky shaders");
    }

    {
        // Unit cube rendered from the inside — camera is always inside this box.
        // Position data doubles as the sky direction vector in the fragment shader.
        MeshData skyData;
        skyData.vertices = {
            Vertex{{-1.0f, -1.0f, -1.0f}, {}, {}},
            Vertex{{ 1.0f, -1.0f, -1.0f}, {}, {}},
            Vertex{{ 1.0f,  1.0f, -1.0f}, {}, {}},
            Vertex{{-1.0f,  1.0f, -1.0f}, {}, {}},
            Vertex{{-1.0f, -1.0f,  1.0f}, {}, {}},
            Vertex{{ 1.0f, -1.0f,  1.0f}, {}, {}},
            Vertex{{ 1.0f,  1.0f,  1.0f}, {}, {}},
            Vertex{{-1.0f,  1.0f,  1.0f}, {}, {}},
        };
        // Inside-out winding (front faces point inward so the cube is visible from the inside)
        skyData.indices = {
            0, 2, 1,  0, 3, 2,  // -Z face
            4, 5, 6,  4, 6, 7,  // +Z face
            0, 1, 5,  0, 5, 4,  // -Y face
            2, 3, 7,  2, 7, 6,  // +Y face
            0, 4, 7,  0, 7, 3,  // -X face
            1, 2, 6,  1, 6, 5,  // +X face
        };
        m_SkyboxMesh = std::make_unique<Mesh>(skyData);
    }

    SetupImGui();
    m_Window.SetCursorCaptured(true);

    InitializeProject();
    if (!ActivateTerrainDataset(m_ActiveTerrainIndex))
    {
        return false;
    }

    return true;
}

void Application::Run()
{
    while (!m_Window.ShouldClose())
    {
        const double frameStartMs = NowMs();
        const float deltaTime = m_Window.PollEventsAndGetDeltaTime();
        const double afterPollMs = NowMs();
        m_PendingCameraCommand.Clear();
        m_JumpRequestedThisFrame = false;
        m_Diagnostics.queuedLookDeltaDegrees = {0.0f, 0.0f};
        BeginImGuiFrame();
        ProcessOrientationGizmoInput();
        ProcessInput(deltaTime);
        const double afterInputMs = NowMs();
        Update(deltaTime);
        const double afterUpdateMs = NowMs();

        m_Diagnostics.inputCpuMs = static_cast<float>(afterInputMs - afterPollMs);
        m_Diagnostics.updateCpuMs = static_cast<float>(afterUpdateMs - afterInputMs);
        Render(deltaTime);
        m_Diagnostics.frameCpuMs = static_cast<float>(NowMs() - frameStartMs);
    }
}

void Application::Shutdown()
{
    m_TerrainBuildJobs.clear();
    m_TerrainTileBuildJobs.clear();
    m_AssetLoadJobs.clear();
    m_ProfileSampleBuildJobs.clear();
    m_BackgroundJobs.reset();
    ShutdownImGui();
    if (m_ProfileLineVbo != 0)
    {
        glDeleteBuffers(1, &m_ProfileLineVbo);
        m_ProfileLineVbo = 0;
    }
    if (m_ProfileLineVao != 0)
    {
        glDeleteVertexArrays(1, &m_ProfileLineVao);
        m_ProfileLineVao = 0;
    }
    if (m_GpuTimerInitialized)
    {
        glDeleteQueries(static_cast<GLsizei>(m_GpuTimerQueries.size()), m_GpuTimerQueries.data());
        m_GpuTimerQueries = {};
        m_GpuTimerPending = {};
        m_GpuTimerInitialized = false;
    }
    for (TerrainDataset& dataset : m_TerrainDatasets)
    {
        dataset.mesh.reset();
        for (TerrainMeshChunk& chunk : dataset.chunks)
        {
            chunk.mesh.reset();
        }
        for (TerrainTile& tile : dataset.tiles)
        {
            for (TerrainMeshChunk& chunk : tile.chunks)
            {
                chunk.mesh.reset();
            }
        }
        for (OverlayEntry& overlay : dataset.overlays)
        {
            overlay.texture.Reset();
        }
    }
    for (ImportedAsset& asset : m_ImportedAssets)
    {
        for (ImportedPrimitiveData& primitive : asset.assetData.primitives)
        {
            primitive.mesh.reset();
            primitive.baseColorTexture.Reset();
            primitive.metallicRoughnessTexture.Reset();
            primitive.normalTexture.Reset();
            primitive.emissiveTexture.Reset();
        }
    }
    m_AssetShader.reset();
    m_LineShader.reset();
    m_TerrainShader.reset();
    m_SkyboxMesh.reset();
    m_SkyShader.reset();
    m_Window.Destroy();
}

void Application::ProcessInput(float deltaTime)
{
    static bool increaseSpeedPressedLastFrame = false;
    static bool decreaseSpeedPressedLastFrame = false;
    static bool pickClickedLastFrame = false;

    // ── Raycast picking — left-click when cursor is free (not over gizmo) ─────
    {
        const bool pickPressed = !m_MouseCaptured && !m_GizmoHovered &&
            glfwGetMouseButton(m_Window.GetNativeHandle(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (pickPressed && !pickClickedLastFrame)
        {
            double mx = 0.0, my = 0.0;
            glfwGetCursorPos(m_Window.GetNativeHandle(), &mx, &my);
            PickAssetAtScreenPos(static_cast<float>(mx), static_cast<float>(my));
        }
        pickClickedLastFrame = pickPressed;
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_ESCAPE))
    {
        m_MouseCaptured = false;
        m_Window.SetCursorCaptured(false);
    }
    if (m_Window.IsKeyPressed(GLFW_KEY_TAB))
    {
        m_MouseCaptured = true;
        m_Window.SetCursorCaptured(true);
    }
    if (m_MouseCaptured)
    {
        m_Window.RefreshCursorCapture();
    }

    const bool shiftPressed = m_Window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || m_Window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    const bool equalPressed = m_Window.IsKeyPressed(GLFW_KEY_EQUAL);
    const bool minusPressed = m_Window.IsKeyPressed(GLFW_KEY_MINUS);
    const bool keypadAddPressed = m_Window.IsKeyPressed(GLFW_KEY_KP_ADD);
    const bool keypadSubtractPressed = m_Window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT);
    const bool increaseSpeedPressed = equalPressed || keypadAddPressed || (shiftPressed && minusPressed);
    if (increaseSpeedPressed && !increaseSpeedPressedLastFrame)
    {
        m_BaseMoveSpeed = std::clamp(m_BaseMoveSpeed * 1.5f, 0.5f, 3000.0f);
        m_FPSController.ResetMouseState();
        m_MouseCaptured = true;
        m_Window.SetCursorCaptured(true);
        m_StatusMessage = "Camera speed UP: " + std::to_string(static_cast<int>(m_BaseMoveSpeed)) + " m/s.";
    }
    increaseSpeedPressedLastFrame = increaseSpeedPressed;

    const bool decreaseSpeedPressed = (!shiftPressed && minusPressed) || keypadSubtractPressed;
    if (decreaseSpeedPressed && !decreaseSpeedPressedLastFrame)
    {
        m_BaseMoveSpeed = std::clamp(m_BaseMoveSpeed / 1.5f, 0.5f, 3000.0f);
        m_FPSController.ResetMouseState();
        m_StatusMessage = "Camera speed DOWN: " + std::to_string(static_cast<int>(m_BaseMoveSpeed)) + " m/s.";
    }
    decreaseSpeedPressedLastFrame = decreaseSpeedPressed;

    // ── View snap keyboard shortcuts (Numpad, works in FPS mode) ─────────────
    // Numpad 1 = Front (-Z), Numpad 3 = Right (+X), Numpad 7 = Top (down),
    // Numpad 9 = Bottom (up), Numpad 4/6 = orbit left/right 45°, Numpad 8/2 = orbit up/down
    {
        static bool kp1Last=false, kp3Last=false, kp7Last=false, kp9Last=false;
        static bool kp4Last=false, kp6Last=false, kp8Last=false, kp2Last=false;
        const bool kp1 = m_Window.IsKeyPressed(GLFW_KEY_KP_1);
        const bool kp3 = m_Window.IsKeyPressed(GLFW_KEY_KP_3);
        const bool kp7 = m_Window.IsKeyPressed(GLFW_KEY_KP_7);
        const bool kp9 = m_Window.IsKeyPressed(GLFW_KEY_KP_9);
        const bool kp4 = m_Window.IsKeyPressed(GLFW_KEY_KP_4);
        const bool kp6 = m_Window.IsKeyPressed(GLFW_KEY_KP_6);
        const bool kp8 = m_Window.IsKeyPressed(GLFW_KEY_KP_8);
        const bool kp2 = m_Window.IsKeyPressed(GLFW_KEY_KP_2);
        if (kp1 && !kp1Last) SnapCameraView(  0.0f,   0.0f);  // Front  (look -Z)
        if (kp3 && !kp3Last) SnapCameraView( 90.0f,   0.0f);  // Right  (look -X)
        if (kp7 && !kp7Last) SnapCameraView(  0.0f, -89.0f);  // Top    (look down)
        if (kp9 && !kp9Last) SnapCameraView(  0.0f,  89.0f);  // Bottom (look up)
        if (kp4 && !kp4Last) SnapCameraView(m_CameraSnapState.active ? m_CameraSnapState.targetYaw - 45.0f : m_Camera.GetYaw() - 45.0f, m_Camera.GetPitch());
        if (kp6 && !kp6Last) SnapCameraView(m_CameraSnapState.active ? m_CameraSnapState.targetYaw + 45.0f : m_Camera.GetYaw() + 45.0f, m_Camera.GetPitch());
        if (kp8 && !kp8Last) SnapCameraView(m_Camera.GetYaw(), std::clamp(m_Camera.GetPitch() + 15.0f, -89.0f, 89.0f));
        if (kp2 && !kp2Last) SnapCameraView(m_Camera.GetYaw(), std::clamp(m_Camera.GetPitch() - 15.0f, -89.0f, 89.0f));
        kp1Last=kp1; kp3Last=kp3; kp7Last=kp7; kp9Last=kp9;
        kp4Last=kp4; kp6Last=kp6; kp8Last=kp8; kp2Last=kp2;
    }

    // ── Elevation-scaled speed ────────────────────────────────────────────────
    float speedThisFrame = m_BaseMoveSpeed;
    if (m_ElevationSpeedSettings.enabled)
    {
        const glm::vec3 pos    = m_Camera.GetPosition();
        const float terrainY   = GetTerrainLocalHeightAt(pos.x, pos.z);
        const float heightAbove = std::max(pos.y - terrainY, 0.1f);
        const float ratio  = heightAbove / std::max(m_ElevationSpeedSettings.referenceHeight, 1.0f);
        const float factor = std::clamp(
            std::log(ratio + 1.0f) * m_ElevationSpeedSettings.logScale / std::log(2.0f) + 1.0f,
            m_ElevationSpeedSettings.minMultiplier,
            m_ElevationSpeedSettings.maxMultiplier);
        speedThisFrame *= factor;
    }
    m_FPSController.SetMoveSpeed(speedThisFrame);

    // ── Gravity / terrain collision ───────────────────────────────────────────
    if (m_GravitySettings.enabled)
    {
        const bool jumpPressed = m_Window.IsKeyPressed(GLFW_KEY_SPACE);

        if (jumpPressed && !m_JumpPressedLastFrame && m_OnGround)
        {
            m_JumpRequestedThisFrame = true;
        }
        m_JumpPressedLastFrame = jumpPressed;
    }

    m_FPSController.SetEnabled(m_MouseCaptured);
    CameraCommandFrame fpsCommand = m_FPSController.BuildFrameCommand(deltaTime);
    m_PendingCameraCommand.localMoveAxes += fpsCommand.localMoveAxes;
    if (fpsCommand.moveDistanceMeters > 0.0f)
    {
        m_PendingCameraCommand.moveDistanceMeters = std::max(m_PendingCameraCommand.moveDistanceMeters,
                                                             fpsCommand.moveDistanceMeters);
    }
    m_PendingCameraCommand.lookDeltaDegrees += fpsCommand.lookDeltaDegrees;
    m_Diagnostics.queuedLookDeltaDegrees += fpsCommand.lookDeltaDegrees;
}

void Application::ApplyPendingCameraCommands(float deltaTime)
{
    const double startMs = NowMs();
    m_Diagnostics.appliedLookDeltaDegrees = ApplyCameraCommandFrame(m_Camera, m_PendingCameraCommand, m_CameraSnapState, deltaTime);

    if (m_GravitySettings.enabled)
    {
        if (m_JumpRequestedThisFrame && m_OnGround)
        {
            m_VerticalVelocity =
                std::sqrt(2.0f * m_GravitySettings.gravityAcceleration * m_GravitySettings.jumpHeightMeters);
            m_OnGround = false;
        }

        glm::vec3 pos = m_Camera.GetPosition();
        float terrainY = GetTerrainLocalHeightAt(pos.x, pos.z);
        float eyeY = terrainY + m_GravitySettings.playerHeightMeters;

        if (!m_OnGround || pos.y > eyeY + 0.05f)
        {
            m_VerticalVelocity -= m_GravitySettings.gravityAcceleration * deltaTime;
            pos.y += m_VerticalVelocity * deltaTime;

            terrainY = GetTerrainLocalHeightAt(pos.x, pos.z);
            eyeY = terrainY + m_GravitySettings.playerHeightMeters;
            if (pos.y <= eyeY)
            {
                pos.y = eyeY;
                m_VerticalVelocity = 0.0f;
                m_OnGround = true;
            }
            else
            {
                m_OnGround = false;
            }
            m_Camera.SetPosition(pos);
        }
        else
        {
            m_Camera.SetPosition({pos.x, eyeY, pos.z});
            m_VerticalVelocity = 0.0f;
            m_OnGround = true;
        }
    }

    m_Diagnostics.cameraApplyCpuMs = static_cast<float>(NowMs() - startMs);
}

void Application::PollGpuFrameTiming()
{
    if (!m_GpuTimerInitialized)
    {
        m_Diagnostics.gpuTimingAvailable = GLAD_GL_VERSION_3_3 != 0;
        if (!m_Diagnostics.gpuTimingAvailable)
        {
            return;
        }
        glGenQueries(static_cast<GLsizei>(m_GpuTimerQueries.size()), m_GpuTimerQueries.data());
        m_GpuTimerInitialized = true;
    }

    for (size_t index = 0; index < m_GpuTimerQueries.size(); ++index)
    {
        if (!m_GpuTimerPending[index])
        {
            continue;
        }

        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(m_GpuTimerQueries[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE)
        {
            GLuint64 elapsedNs = 0;
            glGetQueryObjectui64v(m_GpuTimerQueries[index], GL_QUERY_RESULT, &elapsedNs);
            m_Diagnostics.gpuFrameMs = static_cast<float>(static_cast<double>(elapsedNs) / 1000000.0);
            m_GpuTimerPending[index] = false;
        }
    }
}

void Application::BeginGpuFrameTiming()
{
    if (!m_GpuTimerInitialized || m_GpuTimerPending[static_cast<size_t>(m_GpuTimerWriteIndex)])
    {
        m_GpuTimerActive = false;
        return;
    }

    glBeginQuery(GL_TIME_ELAPSED, m_GpuTimerQueries[static_cast<size_t>(m_GpuTimerWriteIndex)]);
    m_GpuTimerActive = true;
}

void Application::EndGpuFrameTiming()
{
    if (!m_GpuTimerActive)
    {
        return;
    }

    glEndQuery(GL_TIME_ELAPSED);
    m_GpuTimerPending[static_cast<size_t>(m_GpuTimerWriteIndex)] = true;
    m_GpuTimerWriteIndex = (m_GpuTimerWriteIndex + 1) % static_cast<int>(m_GpuTimerQueries.size());
    m_GpuTimerActive = false;
}

glm::dvec3 Application::GetRenderOrigin() const
{
    return glm::dvec3(m_Camera.GetPosition());
}

glm::vec3 Application::ToRenderRelative(const glm::dvec3& worldPosition) const
{
    return MakeCameraRelative(worldPosition, GetRenderOrigin());
}

glm::vec3 Application::ToRenderRelative(const glm::vec3& worldPosition) const
{
    return MakeCameraRelative(worldPosition, GetRenderOrigin());
}

glm::mat4 Application::GetRenderViewMatrix() const
{
    return m_Camera.GetViewMatrixRotationOnly();
}

glm::mat4 Application::GetRenderViewProjectionMatrix() const
{
    return m_Camera.GetProjectionMatrix() * GetRenderViewMatrix();
}

glm::dvec3 Application::GetDatasetWorldTranslation(const TerrainDataset& dataset) const
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return glm::dvec3(0.0);
    }

    return GeoConverter(m_GeoReference).ToLocal(dataset.geoReference.originLatitude,
                                                dataset.geoReference.originLongitude,
                                                dataset.geoReference.originHeight);
}

void Application::Update(float deltaTime)
{
    m_ElapsedTime += deltaTime;

    // ── Diagnostics ring buffer ───────────────────────────────────────────────
    {
        const float dtMs = deltaTime * 1000.0f;
        m_Diagnostics.frameTimesMs[static_cast<size_t>(m_Diagnostics.frameRingHead)] = dtMs;
        m_Diagnostics.frameRingHead = (m_Diagnostics.frameRingHead + 1) % DiagnosticsState::kFrameRingSize;
        m_Diagnostics.frameTimeAccum += dtMs;
        m_Diagnostics.frameCount++;
        if (m_Diagnostics.frameTimeAccum >= 500.0f)
        {
            m_Diagnostics.avgFpsDisplay  = 1000.0f / (m_Diagnostics.frameTimeAccum / static_cast<float>(m_Diagnostics.frameCount));
            m_Diagnostics.avgFrameTimeMs = m_Diagnostics.frameTimeAccum / static_cast<float>(m_Diagnostics.frameCount);
            m_Diagnostics.frameTimeAccum = 0.0f;
            m_Diagnostics.frameCount     = 0;
            m_Diagnostics.minFrameTimeMs = *std::min_element(m_Diagnostics.frameTimesMs.begin(), m_Diagnostics.frameTimesMs.end());
            m_Diagnostics.maxFrameTimeMs = *std::max_element(m_Diagnostics.frameTimesMs.begin(), m_Diagnostics.frameTimesMs.end());
        }
    }

    // ── LOD tile streaming ────────────────────────────────────────────────────
    if (m_TileLODSettings.enabled)
    {
        const glm::vec3 camPos = m_Camera.GetPosition();
        const int activeLoads  = static_cast<int>(m_TerrainTileBuildJobs.size());
        // Collect load/unload decisions
        struct TileRef { int terrainIdx; int tileIdx; float dist; };
        std::vector<TileRef> toLoad;
        std::vector<TileRef> toUnload;
        for (int ti = 0; ti < static_cast<int>(m_TerrainDatasets.size()); ++ti)
        {
            const TerrainDataset& ds = m_TerrainDatasets[static_cast<size_t>(ti)];
            if (!ds.hasTileManifest) continue;
            for (int tidx = 0; tidx < static_cast<int>(ds.tiles.size()); ++tidx)
            {
                const TerrainTile& tile = ds.tiles[static_cast<size_t>(tidx)];
                // Compute tile centre in local space via geographic midpoint
                const double midLat = (tile.bounds.minLatitude  + tile.bounds.maxLatitude)  * 0.5;
                const double midLon = (tile.bounds.minLongitude + tile.bounds.maxLongitude) * 0.5;
                const glm::vec3 tileCenter = glm::vec3(GeoConverter(m_GeoReference).ToLocal(midLat, midLon, 0.0));
                const float dist = glm::length(tileCenter - camPos);
                if (dist < m_TileLODSettings.midRadiusMeters && !tile.loaded && !tile.loading)
                    toLoad.push_back({ti, tidx, dist});
                else if (dist > m_TileLODSettings.unloadRadiusMeters && tile.loaded && !tile.loading)
                    toUnload.push_back({ti, tidx, dist});
            }
        }
        // Sort load candidates by distance (nearest first)
        std::sort(toLoad.begin(), toLoad.end(), [](const TileRef& a, const TileRef& b){ return a.dist < b.dist; });
        int scheduledLoads = activeLoads;
        for (const TileRef& ref : toLoad)
        {
            if (scheduledLoads >= m_TileLODSettings.maxConcurrentLoads) break;
            StartTerrainTileLoadJob(ref.terrainIdx, ref.tileIdx);
            ++scheduledLoads;
        }
        for (const TileRef& ref : toUnload)
        {
            TerrainTile& tile = m_TerrainDatasets[static_cast<size_t>(ref.terrainIdx)].tiles[static_cast<size_t>(ref.tileIdx)];
            tile.chunks.clear();
            tile.meshLoaded = false;
            tile.loaded     = false;
            tile.loading    = false;
            tile.points.clear();
            tile.heightGrid = {};
        }
    }

    ProcessBackgroundJobs();
    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));

    // Advance skeletal animation playback for every skinned asset.
    for (ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.loaded)
            continue;

        // --- Skin (armature) animation ---
        if (asset.assetData.hasSkin)
        {
            AnimationState& s = asset.animState;
            if (s.isPlaying && s.activeClipIndex >= 0 &&
                s.activeClipIndex < static_cast<int>(asset.assetData.animations.size()))
            {
                const float dur = asset.assetData.animations[static_cast<size_t>(s.activeClipIndex)].duration;
                s.currentTime += deltaTime * s.playbackSpeed;
                if (s.loop)
                    s.currentTime = std::fmod(s.currentTime, std::max(dur, 0.001f));
                else
                    s.currentTime = std::min(s.currentTime, dur);
            }
            UpdateAnimationState(asset);
        }

        // --- Node-transform animation (rigid-body object animation) ---
        if (asset.assetData.hasNodeAnimation)
        {
            NodeAnimationState& ns = asset.nodeAnimState;
            if (ns.isPlaying && !asset.assetData.nodeAnimations.empty())
            {
                // Use the maximum duration across all clips as the loop length.
                float maxDur = 0.0f;
                for (const NodeAnimationClip& clip : asset.assetData.nodeAnimations)
                    maxDur = std::max(maxDur, clip.duration);
                maxDur = std::max(maxDur, 0.001f);

                ns.currentTime += deltaTime * ns.playbackSpeed;
                if (ns.loop)
                    ns.currentTime = std::fmod(ns.currentTime, maxDur);
                else
                    ns.currentTime = std::min(ns.currentTime, maxDur);
            }
            UpdateNodeAnimationState(asset);
        }
    }
}

// ---------------------------------------------------------------------------
//  Keyframe sampler helpers
// ---------------------------------------------------------------------------
namespace
{
// Finds the lower-bound keyframe index for time t in a sorted times array.
// Returns 0 when t is before the first key; returns (count-2) at the latest.
size_t FindKeyIndex(const std::vector<float>& times, float t)
{
    if (times.size() <= 1u)
        return 0u;
    for (size_t k = 0u; k + 1u < times.size(); ++k)
    {
        if (t < times[k + 1u])
            return k;
    }
    return times.size() - 2u;
}

glm::vec3 SampleVec3(const AnimationChannel& chan, float t)
{
    if (chan.valuesVec3.empty())
        return glm::vec3(0.0f);
    if (chan.valuesVec3.size() == 1u || chan.times.empty())
        return chan.valuesVec3[0];

    const size_t k  = FindKeyIndex(chan.times, t);
    const size_t k1 = k + 1u;

    if (chan.interpolation == "STEP")
        return chan.valuesVec3[k];

    const float t0 = chan.times[k];
    const float t1 = chan.times[k1];
    const float f  = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return glm::mix(chan.valuesVec3[k], chan.valuesVec3[k1], f);
}

glm::quat SampleQuat(const AnimationChannel& chan, float t)
{
    if (chan.valuesQuat.empty())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (chan.valuesQuat.size() == 1u || chan.times.empty())
        return chan.valuesQuat[0];

    const size_t k  = FindKeyIndex(chan.times, t);
    const size_t k1 = k + 1u;

    if (chan.interpolation == "STEP")
        return chan.valuesQuat[k];

    const float t0 = chan.times[k];
    const float t1 = chan.times[k1];
    const float f  = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return glm::normalize(glm::slerp(chan.valuesQuat[k], chan.valuesQuat[k1], f));
}
} // anonymous namespace

void Application::UpdateAnimationState(ImportedAsset& asset)
{
    const SkeletonData& skeleton = asset.assetData.skeleton;
    const size_t jointCount = skeleton.joints.size();
    AnimationState& s = asset.animState;

    s.skinningMatrices.assign(jointCount, glm::mat4(1.0f));
    if (jointCount == 0u)
        return;

    // Collect per-joint local TRS from the active clip.
    std::vector<glm::vec3> translations(jointCount, glm::vec3(0.0f));
    std::vector<glm::quat> rotations(jointCount, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::vector<glm::vec3> scales(jointCount, glm::vec3(1.0f));

    if (s.activeClipIndex >= 0 &&
        s.activeClipIndex < static_cast<int>(asset.assetData.animations.size()))
    {
        const AnimationClip& clip = asset.assetData.animations[static_cast<size_t>(s.activeClipIndex)];
        const float t = s.currentTime;

        for (const AnimationChannel& chan : clip.channels)
        {
            if (chan.jointIndex < 0 || chan.jointIndex >= static_cast<int>(jointCount))
                continue;
            const size_t ji = static_cast<size_t>(chan.jointIndex);

            if (chan.path == "translation")
                translations[ji] = SampleVec3(chan, t);
            else if (chan.path == "rotation")
                rotations[ji] = SampleQuat(chan, t);
            else if (chan.path == "scale")
                scales[ji] = SampleVec3(chan, t);
        }
    }

    // Build per-joint local matrices and accumulate global transforms.
    // glTF guarantees parent index < child index, so iterating 0…N-1 is safe.
    std::vector<glm::mat4> globalTransforms(jointCount, glm::mat4(1.0f));
    for (size_t ji = 0u; ji < jointCount; ++ji)
    {
        glm::mat4 local(1.0f);
        local = glm::translate(local, translations[ji]);
        local = local * glm::mat4_cast(rotations[ji]);
        local = glm::scale(local, scales[ji]);

        const int parent = skeleton.joints[ji].parentIndex;
        if (parent >= 0 && parent < static_cast<int>(jointCount))
            globalTransforms[ji] = globalTransforms[static_cast<size_t>(parent)] * local;
        else
            globalTransforms[ji] = local;

        s.skinningMatrices[ji] = globalTransforms[ji] * skeleton.joints[ji].inverseBindMatrix;
    }
}

// ---------------------------------------------------------------------------
//  Node-transform animation evaluation
// ---------------------------------------------------------------------------
namespace
{
// Sample helpers for NodeAnimationChannel (same logic as skin helpers above).
glm::vec3 SampleNodeVec3(const NodeAnimationChannel& chan, float t)
{
    if (chan.valuesVec3.empty())
        return glm::vec3(0.0f);
    if (chan.valuesVec3.size() == 1u || chan.times.empty())
        return chan.valuesVec3[0];

    const size_t k  = FindKeyIndex(chan.times, t);
    const size_t k1 = k + 1u;

    if (chan.interpolation == "STEP")
        return chan.valuesVec3[k];

    const float t0 = chan.times[k];
    const float t1 = chan.times[k1];
    const float f  = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return glm::mix(chan.valuesVec3[k], chan.valuesVec3[k1], f);
}

glm::quat SampleNodeQuat(const NodeAnimationChannel& chan, float t)
{
    if (chan.valuesQuat.empty())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (chan.valuesQuat.size() == 1u || chan.times.empty())
        return chan.valuesQuat[0];

    const size_t k  = FindKeyIndex(chan.times, t);
    const size_t k1 = k + 1u;

    if (chan.interpolation == "STEP")
        return chan.valuesQuat[k];

    const float t0 = chan.times[k];
    const float t1 = chan.times[k1];
    const float f  = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
    return glm::normalize(glm::slerp(chan.valuesQuat[k], chan.valuesQuat[k1], f));
}
} // anonymous namespace

void Application::UpdateNodeAnimationState(ImportedAsset& asset)
{
    const std::vector<NodeData>& nodes = asset.assetData.nodes;
    NodeAnimationState& s = asset.nodeAnimState;

    const size_t nodeCount = nodes.size();
    s.nodeWorldTransforms.assign(nodeCount, glm::mat4(1.0f));
    if (nodeCount == 0u)
        return;

    // Start each node's local transform from its static rest-pose TRS.
    struct NodeTRS { glm::vec3 T; glm::quat R; glm::vec3 S; };
    std::vector<NodeTRS> localTRS(nodeCount);
    for (size_t i = 0u; i < nodeCount; ++i)
    {
        localTRS[i].T = nodes[i].translation;
        localTRS[i].R = nodes[i].rotation;
        localTRS[i].S = nodes[i].scale;
    }

    // Override TRS components with animated values from all clips simultaneously.
    const float t = s.currentTime;
    for (const NodeAnimationClip& clip : asset.assetData.nodeAnimations)
    {
        for (const NodeAnimationChannel& chan : clip.channels)
        {
            if (chan.nodeIndex < 0 || chan.nodeIndex >= static_cast<int>(nodeCount))
                continue;
            NodeTRS& trs = localTRS[static_cast<size_t>(chan.nodeIndex)];

            if (chan.path == "translation")
                trs.T = SampleNodeVec3(chan, t);
            else if (chan.path == "rotation")
                trs.R = SampleNodeQuat(chan, t);
            else if (chan.path == "scale")
                trs.S = SampleNodeVec3(chan, t);
        }
    }

    // Build local matrices from TRS (or raw matrix for matrix-spec nodes).
    std::vector<glm::mat4> localMats(nodeCount, glm::mat4(1.0f));
    for (size_t i = 0u; i < nodeCount; ++i)
    {
        if (nodes[i].hasMatrix)
        {
            localMats[i] = nodes[i].matrix;
        }
        else
        {
            glm::mat4 m(1.0f);
            m = glm::translate(m, localTRS[i].T);
            m = m * glm::mat4_cast(localTRS[i].R);
            m = glm::scale(m, localTRS[i].S);
            localMats[i] = m;
        }
    }

    // Accumulate world transforms.  glTF doesn't guarantee parent-before-child
    // for regular nodes, so use a recursive helper with a "computed" flag.
    std::vector<bool> computed(nodeCount, false);
    std::function<void(size_t)> computeWorld = [&](size_t ni)
    {
        if (computed[ni])
            return;
        const int parent = nodes[ni].parentIndex;
        if (parent >= 0 && parent < static_cast<int>(nodeCount))
        {
            computeWorld(static_cast<size_t>(parent));
            s.nodeWorldTransforms[ni] = s.nodeWorldTransforms[static_cast<size_t>(parent)] * localMats[ni];
        }
        else
        {
            s.nodeWorldTransforms[ni] = localMats[ni];
        }
        computed[ni] = true;
    };
    for (size_t i = 0u; i < nodeCount; ++i)
        computeWorld(i);
}

void Application::Render(float deltaTime)
{
    m_Diagnostics.terrainDrawCalls = 0;
    m_Diagnostics.assetDrawCalls = 0;
    m_Diagnostics.skyDrawCalls = 0;
    m_Diagnostics.totalDrawCalls = 0;
    m_Diagnostics.terrainTrianglesDrawn = 0;
    m_Diagnostics.assetTrianglesDrawn = 0;
    m_Diagnostics.skyTrianglesDrawn = 0;
    m_Diagnostics.totalTrianglesDrawn = 0;
    m_Diagnostics.visibleTerrainTiles = 0;
    m_Diagnostics.visibleTerrainChunks = 0;
    m_Diagnostics.terrainCpuMs = 0.0f;
    m_Diagnostics.assetCpuMs = 0.0f;
    m_Diagnostics.skyCpuMs = 0.0f;
    m_Diagnostics.worldOverlayCpuMs = 0.0f;
    m_Diagnostics.imguiCpuMs = 0.0f;
    m_Diagnostics.swapCpuMs = 0.0f;
    m_Diagnostics.maxDatasetWorldTranslationMeters = 0.0f;
    m_Diagnostics.maxRenderTranslationMeters = 0.0f;

    const double uiBuildStartMs = NowMs();
    RenderMainMenuBar();
    RenderMiniMapWindow();
    RenderTerrainDatasetWindow();
    RenderSunWindow();
    RenderAerialOverlayWindow();
    RenderBlenderAssetsWindow();
    RenderTerrainProfilesWindow();
    RenderEditor();
    m_Diagnostics.uiBuildCpuMs = static_cast<float>(NowMs() - uiBuildStartMs);

    ApplyPendingCameraCommands(deltaTime);
    PollGpuFrameTiming();
    const glm::dvec3 renderOrigin = GetRenderOrigin();
    const glm::mat4 renderView = GetRenderViewMatrix();
    const glm::mat4 renderViewProjection = m_Camera.GetProjectionMatrix() * renderView;
    m_Diagnostics.renderOriginDistanceMeters = static_cast<float>(glm::length(renderOrigin));
    m_Diagnostics.renderOriginFloatStepMeters =
        static_cast<float>(EstimateFloatSpacing(glm::length(renderOrigin)));

    const SunParameters sun = ComputeSunParameters(m_GeoReference, m_SunSettings);
    glViewport(0, 0, m_Window.GetWidth(), m_Window.GetHeight());
    // When the procedural sky is active the skybox fills all empty pixels,
    // so use a black clear instead of the computed sky color.
    const glm::vec3 clearColor = m_SkySettings.enabled ? glm::vec3(0.0f) : sun.skyColor;
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    auto recordTerrainDraw = [&](const Mesh& mesh, bool countVisibleChunk) {
        ++m_Diagnostics.terrainDrawCalls;
        m_Diagnostics.terrainTrianglesDrawn += mesh.GetTriangleCount();
        if (countVisibleChunk)
        {
            ++m_Diagnostics.visibleTerrainChunks;
        }
        mesh.Draw();
    };
    auto recordAssetDraw = [&](size_t triangleCount, const auto& drawable) {
        ++m_Diagnostics.assetDrawCalls;
        m_Diagnostics.assetTrianglesDrawn += triangleCount;
        drawable.Draw();
    };

    BeginGpuFrameTiming();

    const double terrainStartMs = NowMs();
    if (m_TerrainShader)
    {
        const Frustum cameraFrustum = BuildFrustumFromViewProjection(renderViewProjection);
        glDisable(GL_CULL_FACE);
        m_TerrainShader->Bind();
        m_TerrainShader->SetMat4("uView", renderView);
        m_TerrainShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_TerrainShader->SetVec3("uCameraPos", glm::vec3(0.0f));
        ApplySunUniforms(*m_TerrainShader, sun);

        for (TerrainDataset& dataset : m_TerrainDatasets)
        {
            if (!dataset.visible || !dataset.loaded)
            {
                continue;
            }

            const glm::dvec3 terrainWorldTranslation = GetDatasetWorldTranslation(dataset);
            const glm::vec3 terrainTranslation = MakeCameraRelative(terrainWorldTranslation, renderOrigin);
            m_Diagnostics.maxDatasetWorldTranslationMeters = std::max(
                m_Diagnostics.maxDatasetWorldTranslationMeters,
                static_cast<float>(glm::length(terrainWorldTranslation)));
            m_Diagnostics.maxRenderTranslationMeters = std::max(
                m_Diagnostics.maxRenderTranslationMeters,
                glm::length(terrainTranslation));
            const glm::mat4 terrainModel = glm::translate(glm::mat4(1.0f), terrainTranslation);
            m_TerrainShader->SetMat4("uModel", terrainModel);
            ApplyTerrainColorUniforms(*m_TerrainShader, dataset, terrainTranslation);

            std::vector<RenderOverlayPlacement> renderableOverlays = BuildRenderableOverlays(dataset);
            ApplyOverlayWorldTranslation(renderableOverlays, terrainTranslation);
            if (renderableOverlays.size() == 1u)
            {
                ApplyOverlayUniforms(*m_TerrainShader, renderableOverlays.front(), 0);
            }
            else
            {
                m_TerrainShader->SetInt("uUseOverlay", 0);
                m_TerrainShader->SetInt("uOverlayOnly", 0);
                m_TerrainShader->SetInt("uOverlayTexture", 0);
                Texture::BindFallback(0);
            }
            if (dataset.hasTileManifest)
            {
                for (TerrainTile& tile : dataset.tiles)
                {
                    if (!IsTerrainTileVisible(dataset, tile, cameraFrustum, terrainTranslation))
                    {
                        continue;
                    }
                    if (!tile.loaded || !tile.meshLoaded)
                    {
                        StartTerrainTileLoadJob(static_cast<int>(&dataset - m_TerrainDatasets.data()),
                                                static_cast<int>(&tile - dataset.tiles.data()));
                        continue;
                    }
                    ++m_Diagnostics.visibleTerrainTiles;
                    for (const TerrainMeshChunk& chunk : tile.chunks)
                    {
                        if (chunk.mesh && IsTerrainChunkVisible(chunk, cameraFrustum, terrainTranslation))
                        {
                            recordTerrainDraw(*chunk.mesh, true);
                        }
                    }
                }
            }
            else if (!dataset.chunks.empty())
            {
                for (const TerrainMeshChunk& chunk : dataset.chunks)
                {
                    if (chunk.mesh && IsTerrainChunkVisible(chunk, cameraFrustum, terrainTranslation))
                    {
                        recordTerrainDraw(*chunk.mesh, true);
                    }
                }
            }
            else
            {
                if (!dataset.mesh)
                {
                    continue;
                }
                recordTerrainDraw(*dataset.mesh, false);
            }

            if (renderableOverlays.size() > 1u)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glDepthFunc(GL_LEQUAL);
                for (const RenderOverlayPlacement& placement : renderableOverlays)
                {
                    ApplyOverlayUniforms(*m_TerrainShader, placement, 1);
                    if (dataset.hasTileManifest)
                    {
                        for (TerrainTile& tile : dataset.tiles)
                        {
                            if (!tile.loaded || !tile.meshLoaded ||
                                !IsTerrainTileVisible(dataset, tile, cameraFrustum, terrainTranslation))
                            {
                                continue;
                            }
                            for (const TerrainMeshChunk& chunk : tile.chunks)
                            {
                                if (chunk.mesh && IsTerrainChunkVisible(chunk, cameraFrustum, terrainTranslation))
                                {
                                    recordTerrainDraw(*chunk.mesh, false);
                                }
                            }
                        }
                    }
                    else if (!dataset.chunks.empty())
                    {
                        for (const TerrainMeshChunk& chunk : dataset.chunks)
                        {
                            if (chunk.mesh && IsTerrainChunkVisible(chunk, cameraFrustum, terrainTranslation))
                            {
                                recordTerrainDraw(*chunk.mesh, false);
                            }
                        }
                    }
                    else
                    {
                        if (dataset.mesh)
                        {
                            recordTerrainDraw(*dataset.mesh, false);
                        }
                    }
                }
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }
        glEnable(GL_CULL_FACE);
    }
    m_Diagnostics.terrainCpuMs = static_cast<float>(NowMs() - terrainStartMs);

    const double assetStartMs = NowMs();
    if (m_AssetShader)
    {
        m_AssetShader->Bind();
        m_AssetShader->SetMat4("uView", renderView);
        m_AssetShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_AssetShader->SetVec3("uCameraPos", glm::vec3(0.0f));
        ApplySunUniforms(*m_AssetShader, sun);
        m_AssetShader->SetInt("uBaseColorTexture", 0);
        m_AssetShader->SetInt("uMetallicRoughnessTexture", 1);
        m_AssetShader->SetInt("uNormalTexture", 2);
        m_AssetShader->SetInt("uEmissiveTexture", 3);

        for (const ImportedAsset& asset : m_ImportedAssets)
        {
            if (!asset.loaded)
            {
                continue;
            }

            const glm::vec3 renderPosition = MakeCameraRelative(asset.position, renderOrigin);
            glm::mat4 model(1.0f);
            model = glm::translate(model, renderPosition);
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, asset.scale);

            m_AssetShader->SetVec3("uTintColor", asset.tint);

            // Upload bone palette once per asset (only if skinned).
            const bool skinned = asset.assetData.hasSkin;
            m_AssetShader->SetInt("uSkinned", skinned ? 1 : 0);
            if (skinned && !asset.animState.skinningMatrices.empty())
            {
                m_AssetShader->SetMat4Array("uBoneMatrices", asset.animState.skinningMatrices);
            }

            // For non-node-animated assets, one uModel upload is enough.
            const bool hasNodeAnim = asset.assetData.hasNodeAnimation;
            if (!hasNodeAnim)
                m_AssetShader->SetMat4("uModel", model);

            for (const ImportedPrimitiveData& primitive : asset.assetData.primitives)
            {
                // Each primitive is either a skinned or a static draw.
                const bool drawSkinned = primitive.isSkinned && primitive.skinnedMesh;
                if (!drawSkinned && !primitive.mesh)
                {
                    continue;
                }

                // Node-animated primitives need their own uModel each draw call.
                if (hasNodeAnim)
                {
                    glm::mat4 primModel = model;
                    const int ni = primitive.nodeIndex;
                    if (ni >= 0 && ni < static_cast<int>(asset.nodeAnimState.nodeWorldTransforms.size()))
                        primModel = model * asset.nodeAnimState.nodeWorldTransforms[static_cast<size_t>(ni)];
                    m_AssetShader->SetMat4("uModel", primModel);
                }

                m_AssetShader->SetInt("uUseBaseColorTexture", primitive.hasBaseColorTexture ? 1 : 0);
                m_AssetShader->SetInt("uUseMetallicRoughnessTexture", primitive.hasMetallicRoughnessTexture ? 1 : 0);
                m_AssetShader->SetInt("uUseNormalTexture", primitive.hasNormalTexture ? 1 : 0);
                m_AssetShader->SetInt("uUseEmissiveTexture", primitive.hasEmissiveTexture ? 1 : 0);
                m_AssetShader->SetVec4("uBaseColorFactor", primitive.baseColorFactor);
                m_AssetShader->SetFloat("uMetallicFactor", primitive.metallicFactor);
                m_AssetShader->SetFloat("uRoughnessFactor", primitive.roughnessFactor);
                m_AssetShader->SetVec3("uEmissiveFactor", primitive.emissiveFactor);
                m_AssetShader->SetFloat("uAlphaCutoff", primitive.alphaCutoff);
                m_AssetShader->SetInt("uAlphaMode", primitive.alphaMode == "MASK" ? 1 : (primitive.alphaMode == "BLEND" ? 2 : 0));
                if (primitive.hasBaseColorTexture)
                {
                    primitive.baseColorTexture.Bind(0);
                }
                if (primitive.hasMetallicRoughnessTexture)
                {
                    primitive.metallicRoughnessTexture.Bind(1);
                }
                if (primitive.hasNormalTexture)
                {
                    primitive.normalTexture.Bind(2);
                }
                if (primitive.hasEmissiveTexture)
                {
                    primitive.emissiveTexture.Bind(3);
                }
                if (primitive.alphaMode == "BLEND")
                {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
                if (drawSkinned)
                {
                    recordAssetDraw(primitive.skinMeshData.indices.size() / 3u, *primitive.skinnedMesh);
                }
                else
                {
                    recordAssetDraw(primitive.meshData.indices.size() / 3u, *primitive.mesh);
                }
                if (primitive.alphaMode == "BLEND")
                {
                    glDisable(GL_BLEND);
                }
            }
        }
    }
    m_Diagnostics.assetCpuMs = static_cast<float>(NowMs() - assetStartMs);

    // --- Procedural sky background ---
    // Rendered last: the vertex shader sets gl_Position.z = gl_Position.w so after
    // perspective divide depth = 1.0 (far plane). GL_LEQUAL lets it pass exactly there
    // while all real geometry (depth < 1.0) naturally occludes it — zero overdraw waste.
    const double skyStartMs = NowMs();
    if (m_SkySettings.enabled && m_SkyShader && m_SkyboxMesh)
    {
        glm::vec3 zenith  = m_SkySettings.zenithColor;
        glm::vec3 horizon = m_SkySettings.horizonColor;
        if (m_SkySettings.useSunDrivenColor)
        {
            zenith  = sun.skyColor * 0.65f;
            horizon = sun.skyColor;
        }

        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        m_SkyShader->Bind();
        m_SkyShader->SetMat4("uProjection",        m_Camera.GetProjectionMatrix());
        m_SkyShader->SetMat4("uView",              m_Camera.GetViewMatrixRotationOnly());
        m_SkyShader->SetVec3("uZenithColor",       zenith);
        m_SkyShader->SetVec3("uHorizonColor",      horizon);
        m_SkyShader->SetFloat("uHorizonSharpness", m_SkySettings.horizonSharpness);
        m_SkyShader->SetVec3("uSunDirection",      sun.direction);
        m_SkyShader->SetVec3("uSunColor",          sun.color);
        const float diskSize = m_SkySettings.showSunDisk ? m_SkySettings.sunDiskSize : 0.0f;
        m_SkyShader->SetFloat("uSunDiskSize",      diskSize);
        m_SkyShader->SetFloat("uSunDiskIntensity", m_SkySettings.sunDiskIntensity);
        // Cloud uniforms
        m_SkyShader->SetInt  ("uCloudsEnabled",  m_SkySettings.cloudsEnabled ? 1 : 0);
        m_SkyShader->SetFloat("uCloudCoverage",  m_SkySettings.cloudCoverage);
        m_SkyShader->SetFloat("uCloudDensity",   m_SkySettings.cloudDensity);
        m_SkyShader->SetFloat("uCloudScale",     m_SkySettings.cloudScale);
        // Convert m/s wind speed to noise-domain scroll rate (1 m/s = 0.001 UV/s at 1500 m reference)
        constexpr float kCloudMsToUV = 0.001f;
        m_SkyShader->SetVec2 ("uCloudSpeed",     glm::vec2(m_SkySettings.cloudSpeedX  * kCloudMsToUV,
                                                            m_SkySettings.cloudSpeedY  * kCloudMsToUV));
        m_SkyShader->SetFloat("uCloudAltitude",  m_SkySettings.cloudAltitude);
        m_SkyShader->SetFloat("uTime",           m_ElapsedTime);
        glm::vec3 cloudCol = m_SkySettings.cloudColor;
        if (m_SkySettings.cloudAutoColor)
        {
            // Warm orange tint at low sun elevation, neutral white in daylight
            const float sunH = glm::clamp(sun.direction.y, 0.0f, 1.0f);
            cloudCol = glm::mix(glm::vec3(1.0f, 0.78f, 0.55f), glm::vec3(1.0f, 1.0f, 1.0f), sunH);
        }
        m_SkyShader->SetVec3("uCloudColor", cloudCol);
        ++m_Diagnostics.skyDrawCalls;
        m_Diagnostics.skyTrianglesDrawn += m_SkyboxMesh->GetTriangleCount();
        m_SkyboxMesh->Draw();

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
    }
    m_Diagnostics.skyCpuMs = static_cast<float>(NowMs() - skyStartMs);
    EndGpuFrameTiming();
    m_Diagnostics.totalDrawCalls = m_Diagnostics.terrainDrawCalls +
                                   m_Diagnostics.assetDrawCalls +
                                   m_Diagnostics.skyDrawCalls;
    m_Diagnostics.totalTrianglesDrawn = m_Diagnostics.terrainTrianglesDrawn +
                                        m_Diagnostics.assetTrianglesDrawn +
                                        m_Diagnostics.skyTrianglesDrawn;

    ImDrawList* foregroundDrawList = ImGui::GetForegroundDrawList();
    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.showLabel)
        {
            continue;
        }

        const glm::vec3 labelPosition = MakeCameraRelative(asset.position + glm::vec3(0.0f, 2.0f, 0.0f),
                                                           renderOrigin);
        const glm::vec4 clipPosition = renderViewProjection * glm::vec4(labelPosition, 1.0f);
        if (clipPosition.w <= 0.0f)
        {
            continue;
        }

        const glm::vec3 ndc = glm::vec3(clipPosition) / clipPosition.w;
        if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f)
        {
            continue;
        }

        const float screenX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(m_Window.GetWidth());
        const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(m_Window.GetHeight());
        const ImVec2 textSize = ImGui::CalcTextSize(asset.name.c_str());
        const ImVec2 textPosition(screenX - (textSize.x * 0.5f), screenY - textSize.y);
        foregroundDrawList->AddRectFilled(ImVec2(textPosition.x - 5.0f, textPosition.y - 3.0f),
                                          ImVec2(textPosition.x + textSize.x + 5.0f, textPosition.y + textSize.y + 3.0f),
                                          IM_COL32(12, 18, 24, 190),
                                          4.0f);
        foregroundDrawList->AddText(textPosition, IM_COL32(235, 243, 248, 245), asset.name.c_str());
    }

    const double worldOverlayStartMs = NowMs();
    RenderWorldTerrainProfiles();
    m_Diagnostics.worldOverlayCpuMs = static_cast<float>(NowMs() - worldOverlayStartMs);

    RenderOrientationGizmo();
    RenderCameraHud();
    RenderAssetLabels();
    const double imguiStartMs = NowMs();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
    {
        GLFWwindow* backupContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backupContext);
    }
    m_Diagnostics.imguiCpuMs = static_cast<float>(NowMs() - imguiStartMs);
    const double swapStartMs = NowMs();
    m_Window.SwapBuffers();
    m_Diagnostics.swapCpuMs = static_cast<float>(NowMs() - swapStartMs);
}

void Application::InitializeProject()
{
    TerrainDataset dataset;
    dataset.name = "Terrain 1";
    dataset.path = "assets/data/sample_terrain.csv";
    dataset.settings.gridResolutionX = 256;
    dataset.settings.gridResolutionZ = 256;
    dataset.settings.heightScale = 1.0f;

    OverlayEntry overlay;
    overlay.name = "Overlay 1";
    overlay.image.opacity = 0.85f;
    dataset.overlays.push_back(std::move(overlay));

    m_TerrainDatasets.push_back(std::move(dataset));
    m_ActiveTerrainIndex = 0;

    ImportedAsset asset;
    asset.name = "Asset 1";
    m_ImportedAssets.push_back(std::move(asset));
    m_ActiveImportedAssetIndex = 0;

    TerrainProfile profile;
    profile.name = "Profile 1";
    m_TerrainProfiles.push_back(profile);
    m_ActiveTerrainProfileIndex = 0;

    m_StatusMessage = "Project initialized.";
}


} // namespace GeoFPS
