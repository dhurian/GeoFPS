#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

namespace GeoFPS
{
// Per-frame instrumentation aggregate: timings, draw/triangle counts, upload
// counters, camera-look deltas, and floating-origin precision metrics.  Owned
// by Application, populated across the frame, and consumed by the diagnostics
// overlay/panel and the performance logger.  The per-frame fields are filled
// in by Application; the frame-time ring buffer maintains itself via
// PushFrameTime().
struct DiagnosticsState
{
    static constexpr int kFrameRingSize = 128;
    std::array<float, kFrameRingSize> frameTimesMs {};
    int   frameRingHead    {0};
    float frameTimeAccum   {0.0f};
    int   frameCount       {0};
    float avgFpsDisplay    {0.0f};
    float avgFrameTimeMs   {0.0f};
    float minFrameTimeMs   {999.0f};
    float maxFrameTimeMs   {0.0f};
    bool  showOverlay      {false};
    bool  lowLatencyMode   {false};
    bool  platformViewportsEnabled {false};

    float inputCpuMs       {0.0f};
    float updateCpuMs      {0.0f};
    float uiBuildCpuMs     {0.0f};
    float cameraApplyCpuMs {0.0f};
    float terrainCpuMs     {0.0f};
    float assetCpuMs       {0.0f};
    float skyCpuMs         {0.0f};
    float worldOverlayCpuMs {0.0f};
    float imguiCpuMs       {0.0f};
    float swapCpuMs        {0.0f};
    float frameCpuMs       {0.0f};
    float gpuFrameMs       {0.0f};
    bool  gpuTimingAvailable {false};

    size_t terrainDrawCalls {0};
    size_t assetDrawCalls   {0};
    size_t skyDrawCalls     {0};
    size_t totalDrawCalls   {0};
    size_t terrainTrianglesDrawn {0};
    size_t assetTrianglesDrawn   {0};
    size_t skyTrianglesDrawn     {0};
    size_t totalTrianglesDrawn   {0};
    size_t visibleTerrainTiles   {0};
    size_t visibleTerrainChunks  {0};
    size_t meshUploadsThisFrame  {0};
    size_t tileChunkUploadsThisFrame {0};
    float  meshUploadCpuMs       {0.0f};

    glm::vec2 queuedLookDeltaDegrees {0.0f};
    glm::vec2 appliedLookDeltaDegrees {0.0f};
    float  renderOriginDistanceMeters {0.0f};
    float  renderOriginFloatStepMeters {0.0f};
    float  maxDatasetWorldTranslationMeters {0.0f};
    float  maxRenderTranslationMeters {0.0f};

    // Record one frame's duration into the rolling ring buffer and refresh the
    // displayed averages every ~500 ms.  Called once per frame by Application.
    void PushFrameTime(float frameMs)
    {
        frameTimesMs[static_cast<size_t>(frameRingHead)] = frameMs;
        frameRingHead = (frameRingHead + 1) % kFrameRingSize;
        frameTimeAccum += frameMs;
        ++frameCount;
        if (frameTimeAccum >= 500.0f)
        {
            avgFpsDisplay  = 1000.0f / (frameTimeAccum / static_cast<float>(frameCount));
            avgFrameTimeMs = frameTimeAccum / static_cast<float>(frameCount);
            frameTimeAccum = 0.0f;
            frameCount     = 0;
            minFrameTimeMs = *std::min_element(frameTimesMs.begin(), frameTimesMs.end());
            maxFrameTimeMs = *std::max_element(frameTimesMs.begin(), frameTimesMs.end());
        }
    }
};
} // namespace GeoFPS
