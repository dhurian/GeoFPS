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
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace GeoFPS
{
using namespace ApplicationInternal;
namespace
{
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

glm::vec3 TerrainWorldTranslation(const TerrainDataset& dataset, const GeoReference& worldReference)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return glm::vec3(0.0f);
    }

    GeoConverter worldConverter(worldReference);
    const glm::dvec3 localOrigin = worldConverter.ToLocal(dataset.geoReference.originLatitude,
                                                          dataset.geoReference.originLongitude,
                                                          dataset.geoReference.originHeight);
    return glm::vec3(static_cast<float>(localOrigin.x),
                     static_cast<float>(localOrigin.y),
                     static_cast<float>(localOrigin.z));
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

Frustum BuildCameraFrustum(const Camera& camera)
{
    const glm::mat4 viewProjection = camera.GetProjectionMatrix() * camera.GetViewMatrix();
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

    m_FPSController.AttachWindow(m_Window.GetNativeHandle());
    m_FPSController.AttachCamera(&m_Camera);
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
        const float deltaTime = m_Window.PollEventsAndGetDeltaTime();
        ProcessInput(deltaTime);
        Update(deltaTime);
        Render();
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

    const bool shiftPressed = m_Window.IsKeyPressed(GLFW_KEY_LEFT_SHIFT) || m_Window.IsKeyPressed(GLFW_KEY_RIGHT_SHIFT);
    const bool equalPressed = m_Window.IsKeyPressed(GLFW_KEY_EQUAL);
    const bool minusPressed = m_Window.IsKeyPressed(GLFW_KEY_MINUS);
    const bool keypadAddPressed = m_Window.IsKeyPressed(GLFW_KEY_KP_ADD);
    const bool keypadSubtractPressed = m_Window.IsKeyPressed(GLFW_KEY_KP_SUBTRACT);
    const bool increaseSpeedPressed = equalPressed || keypadAddPressed || (shiftPressed && minusPressed);
    if (increaseSpeedPressed && !increaseSpeedPressedLastFrame)
    {
        m_FPSController.SetMoveSpeed(m_FPSController.GetMoveSpeed() * 1.5f);
        m_FPSController.ResetMouseState();
        m_MouseCaptured = true;
        m_Window.SetCursorCaptured(true);
        m_StatusMessage = "Camera speed UP: " + std::to_string(static_cast<int>(m_FPSController.GetMoveSpeed())) + " m/s.";
    }
    increaseSpeedPressedLastFrame = increaseSpeedPressed;

    const bool decreaseSpeedPressed = (!shiftPressed && minusPressed) || keypadSubtractPressed;
    if (decreaseSpeedPressed && !decreaseSpeedPressedLastFrame)
    {
        m_FPSController.SetMoveSpeed(m_FPSController.GetMoveSpeed() / 1.5f);
        m_FPSController.ResetMouseState();
        m_StatusMessage = "Camera speed DOWN: " + std::to_string(static_cast<int>(m_FPSController.GetMoveSpeed())) + " m/s.";
    }
    decreaseSpeedPressedLastFrame = decreaseSpeedPressed;

    m_FPSController.SetEnabled(m_MouseCaptured);
    m_FPSController.Update(deltaTime);
}

void Application::Update(float deltaTime)
{
    m_ElapsedTime += deltaTime;
    ProcessBackgroundJobs();
    m_Camera.SetAspectRatio(static_cast<float>(m_Window.GetWidth()) / static_cast<float>(m_Window.GetHeight()));
}

void Application::Render()
{
    const SunParameters sun = ComputeSunParameters(m_GeoReference, m_SunSettings);
    glViewport(0, 0, m_Window.GetWidth(), m_Window.GetHeight());
    // When the procedural sky is active the skybox fills all empty pixels,
    // so use a black clear instead of the computed sky color.
    const glm::vec3 clearColor = m_SkySettings.enabled ? glm::vec3(0.0f) : sun.skyColor;
    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    BeginImGuiFrame();
    RenderMainMenuBar();

    if (m_TerrainShader)
    {
        const Frustum cameraFrustum = BuildCameraFrustum(m_Camera);
        glDisable(GL_CULL_FACE);
        m_TerrainShader->Bind();
        m_TerrainShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_TerrainShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_TerrainShader->SetVec3("uCameraPos", m_Camera.GetPosition());
        ApplySunUniforms(*m_TerrainShader, sun);

        for (TerrainDataset& dataset : m_TerrainDatasets)
        {
            if (!dataset.visible || !dataset.loaded)
            {
                continue;
            }

            const glm::vec3 terrainTranslation = TerrainWorldTranslation(dataset, m_GeoReference);
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
                    for (const TerrainMeshChunk& chunk : tile.chunks)
                    {
                        if (chunk.mesh && IsTerrainChunkVisible(chunk, cameraFrustum, terrainTranslation))
                        {
                            chunk.mesh->Draw();
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
                        chunk.mesh->Draw();
                    }
                }
            }
            else
            {
                if (!dataset.mesh)
                {
                    continue;
                }
                dataset.mesh->Draw();
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
                                    chunk.mesh->Draw();
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
                                chunk.mesh->Draw();
                            }
                        }
                    }
                    else
                    {
                        if (dataset.mesh)
                        {
                            dataset.mesh->Draw();
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

    if (m_AssetShader)
    {
        m_AssetShader->Bind();
        m_AssetShader->SetMat4("uView", m_Camera.GetViewMatrix());
        m_AssetShader->SetMat4("uProjection", m_Camera.GetProjectionMatrix());
        m_AssetShader->SetVec3("uCameraPos", m_Camera.GetPosition());
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

            glm::mat4 model(1.0f);
            model = glm::translate(model, asset.position);
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(asset.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::scale(model, asset.scale);

            m_AssetShader->SetMat4("uModel", model);
            m_AssetShader->SetVec3("uTintColor", asset.tint);

            for (const ImportedPrimitiveData& primitive : asset.assetData.primitives)
            {
                if (!primitive.mesh)
                {
                    continue;
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
                primitive.mesh->Draw();
                if (primitive.alphaMode == "BLEND")
                {
                    glDisable(GL_BLEND);
                }
            }
        }
    }

    // --- Procedural sky background ---
    // Rendered last: the vertex shader sets gl_Position.z = gl_Position.w so after
    // perspective divide depth = 1.0 (far plane). GL_LEQUAL lets it pass exactly there
    // while all real geometry (depth < 1.0) naturally occludes it — zero overdraw waste.
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
        m_SkyboxMesh->Draw();

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
    }

    ImDrawList* foregroundDrawList = ImGui::GetForegroundDrawList();
    const glm::mat4 viewProjection = m_Camera.GetProjectionMatrix() * m_Camera.GetViewMatrix();
    for (const ImportedAsset& asset : m_ImportedAssets)
    {
        if (!asset.showLabel)
        {
            continue;
        }

        const glm::vec4 clipPosition = viewProjection * glm::vec4(asset.position + glm::vec3(0.0f, 2.0f, 0.0f), 1.0f);
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

    RenderWorldTerrainProfiles();

    RenderMiniMapWindow();
    RenderTerrainDatasetWindow();
    RenderSunWindow();
    RenderAerialOverlayWindow();
    RenderBlenderAssetsWindow();
    RenderTerrainProfilesWindow();
    RenderEditor();
    RenderCameraHud();
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
    m_Window.SwapBuffers();
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
