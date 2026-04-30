#include "Terrain/TerrainProfile.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace GeoFPS
{
namespace
{
struct MetalGridInfo
{
    uint32_t resolutionX;
    uint32_t resolutionZ;
    uint32_t levelCount;
    uint32_t maxSegments;
    float minLatitude;
    float maxLatitude;
    float minLongitude;
    float maxLongitude;
};

struct MetalOutputSegment
{
    float levelHeight;
    float startLatitude;
    float startLongitude;
    float endLatitude;
    float endLongitude;
};

NSString* IsolineKernelSource()
{
    return @R"METAL(
#include <metal_stdlib>
using namespace metal;

struct GridInfo
{
    uint resolutionX;
    uint resolutionZ;
    uint levelCount;
    uint maxSegments;
    float minLatitude;
    float maxLatitude;
    float minLongitude;
    float maxLongitude;
};

struct OutputSegment
{
    float levelHeight;
    float startLatitude;
    float startLongitude;
    float endLatitude;
    float endLongitude;
};

float2 interpolate_crossing(float2 a, float2 b, float heightA, float heightB, float level)
{
    float denominator = heightB - heightA;
    float t = fabs(denominator) <= 0.000001f ? 0.5f : clamp((level - heightA) / denominator, 0.0f, 1.0f);
    return mix(a, b, t);
}

kernel void generate_isolines(device const float* heights [[buffer(0)]],
                              device const float* levels [[buffer(1)]],
                              constant GridInfo& info [[buffer(2)]],
                              device OutputSegment* output [[buffer(3)]],
                              device atomic_uint* counter [[buffer(4)]],
                              uint threadId [[thread_position_in_grid]])
{
    uint cellsX = info.resolutionX - 1;
    uint cellsZ = info.resolutionZ - 1;
    uint cellCount = cellsX * cellsZ;
    uint totalThreadCount = cellCount * info.levelCount;
    if (threadId >= totalThreadCount)
    {
        return;
    }

    uint levelIndex = threadId / cellCount;
    uint cellIndex = threadId - (levelIndex * cellCount);
    uint x = cellIndex % cellsX;
    uint z = cellIndex / cellsX;
    float level = levels[levelIndex];

    float x0 = float(x) / float(info.resolutionX - 1);
    float x1 = float(x + 1) / float(info.resolutionX - 1);
    float z0 = float(z) / float(info.resolutionZ - 1);
    float z1 = float(z + 1) / float(info.resolutionZ - 1);

    float lat0 = mix(info.minLatitude, info.maxLatitude, z0);
    float lat1 = mix(info.minLatitude, info.maxLatitude, z1);
    float lon0 = mix(info.minLongitude, info.maxLongitude, x0);
    float lon1 = mix(info.minLongitude, info.maxLongitude, x1);

    float hBottomLeft = heights[z * info.resolutionX + x];
    float hBottomRight = heights[z * info.resolutionX + (x + 1)];
    float hTopLeft = heights[(z + 1) * info.resolutionX + x];
    float hTopRight = heights[(z + 1) * info.resolutionX + (x + 1)];

    float2 bottomLeft = float2(lat0, lon0);
    float2 bottomRight = float2(lat0, lon1);
    float2 topRight = float2(lat1, lon1);
    float2 topLeft = float2(lat1, lon0);

    float2 crossings[4];
    uint crossingCount = 0;

    bool bottomCrosses = (hBottomLeft < level && hBottomRight >= level) || (hBottomRight < level && hBottomLeft >= level);
    if (bottomCrosses && fabs(hBottomLeft - hBottomRight) > 0.000001f)
    {
        crossings[crossingCount++] = interpolate_crossing(bottomLeft, bottomRight, hBottomLeft, hBottomRight, level);
    }
    bool rightCrosses = (hBottomRight < level && hTopRight >= level) || (hTopRight < level && hBottomRight >= level);
    if (rightCrosses && fabs(hBottomRight - hTopRight) > 0.000001f)
    {
        crossings[crossingCount++] = interpolate_crossing(bottomRight, topRight, hBottomRight, hTopRight, level);
    }
    bool topCrosses = (hTopRight < level && hTopLeft >= level) || (hTopLeft < level && hTopRight >= level);
    if (topCrosses && fabs(hTopRight - hTopLeft) > 0.000001f)
    {
        crossings[crossingCount++] = interpolate_crossing(topRight, topLeft, hTopRight, hTopLeft, level);
    }
    bool leftCrosses = (hTopLeft < level && hBottomLeft >= level) || (hBottomLeft < level && hTopLeft >= level);
    if (leftCrosses && fabs(hTopLeft - hBottomLeft) > 0.000001f)
    {
        crossings[crossingCount++] = interpolate_crossing(topLeft, bottomLeft, hTopLeft, hBottomLeft, level);
    }

    uint emitCount = crossingCount == 4 ? 2 : (crossingCount == 2 ? 1 : 0);
    if (emitCount == 0)
    {
        return;
    }

    uint baseIndex = atomic_fetch_add_explicit(counter, emitCount, memory_order_relaxed);
    if (baseIndex + emitCount > info.maxSegments)
    {
        return;
    }

    output[baseIndex] = {level, crossings[0].x, crossings[0].y, crossings[1].x, crossings[1].y};
    if (emitCount == 2)
    {
        output[baseIndex + 1] = {level, crossings[2].x, crossings[2].y, crossings[3].x, crossings[3].y};
    }
}
)METAL";
}

std::vector<float> BuildContourLevels(double minHeight, double maxHeight, double interval)
{
    std::vector<float> levels;
    if (interval <= 0.0)
    {
        return levels;
    }

    const double firstLevel = std::ceil(minHeight / interval) * interval;
    for (double level = firstLevel; level <= maxHeight + 1e-10; level += interval)
    {
        if (level <= minHeight + 1e-10 || level >= maxHeight - 1e-10)
        {
            continue;
        }
        levels.push_back(static_cast<float>(level));
        if (levels.size() >= 256)
        {
            break;
        }
    }

    return levels;
}
} // namespace

bool GenerateTerrainIsolinesMetal(const TerrainIsolineSampleGrid& sampleGrid,
                                  const TerrainIsolineSettings& settings,
                                  std::vector<TerrainIsolineSegment>& segments);

bool GenerateTerrainIsolinesMetal(const TerrainHeightGrid& heightGrid,
                                  const TerrainIsolineSettings& settings,
                                  std::vector<TerrainIsolineSegment>& segments)
{
    return GenerateTerrainIsolinesMetal(BuildTerrainIsolineSampleGrid(heightGrid, settings), settings, segments);
}

bool GenerateTerrainIsolinesMetal(const TerrainIsolineSampleGrid& sampleGrid,
                                  const TerrainIsolineSettings& settings,
                                  std::vector<TerrainIsolineSegment>& segments)
{
    segments.clear();
    if (!sampleGrid.IsValid())
    {
        return false;
    }

    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil)
        {
            return false;
        }

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:IsolineKernelSource() options:nil error:&error];
        if (library == nil)
        {
            return false;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"generate_isolines"];
        if (function == nil)
        {
            return false;
        }

        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        if (pipeline == nil)
        {
            return false;
        }

        const int resolutionX = sampleGrid.resolutionX;
        const int resolutionZ = sampleGrid.resolutionZ;
        const double minHeight = sampleGrid.minHeight;
        const double maxHeight = sampleGrid.maxHeight;
        const double interval = ResolveContourInterval(minHeight, maxHeight, settings);
        std::vector<float> levels = BuildContourLevels(minHeight, maxHeight, interval);
        if (levels.empty())
        {
            return true;
        }

        constexpr uint32_t kMaxSegments = 20000;
        std::vector<MetalOutputSegment> output(kMaxSegments);
        uint32_t counter = 0;
        MetalGridInfo info {};
        info.resolutionX = static_cast<uint32_t>(resolutionX);
        info.resolutionZ = static_cast<uint32_t>(resolutionZ);
        info.levelCount = static_cast<uint32_t>(levels.size());
        info.maxSegments = kMaxSegments;
        info.minLatitude = static_cast<float>(sampleGrid.minLatitude);
        info.maxLatitude = static_cast<float>(sampleGrid.maxLatitude);
        info.minLongitude = static_cast<float>(sampleGrid.minLongitude);
        info.maxLongitude = static_cast<float>(sampleGrid.maxLongitude);

        id<MTLBuffer> heightsBuffer = [device newBufferWithBytes:sampleGrid.heights.data()
                                                          length:sampleGrid.heights.size() * sizeof(float)
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> levelsBuffer = [device newBufferWithBytes:levels.data()
                                                         length:levels.size() * sizeof(float)
                                                        options:MTLResourceStorageModeShared];
        id<MTLBuffer> infoBuffer = [device newBufferWithBytes:&info length:sizeof(info) options:MTLResourceStorageModeShared];
        id<MTLBuffer> outputBuffer = [device newBufferWithLength:output.size() * sizeof(MetalOutputSegment)
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> counterBuffer = [device newBufferWithBytes:&counter length:sizeof(counter) options:MTLResourceStorageModeShared];
        if (heightsBuffer == nil || levelsBuffer == nil || infoBuffer == nil || outputBuffer == nil || counterBuffer == nil)
        {
            return false;
        }

        id<MTLCommandQueue> commandQueue = [device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
        if (commandQueue == nil || commandBuffer == nil || encoder == nil)
        {
            return false;
        }

        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:heightsBuffer offset:0 atIndex:0];
        [encoder setBuffer:levelsBuffer offset:0 atIndex:1];
        [encoder setBuffer:infoBuffer offset:0 atIndex:2];
        [encoder setBuffer:outputBuffer offset:0 atIndex:3];
        [encoder setBuffer:counterBuffer offset:0 atIndex:4];

        const NSUInteger totalThreads = static_cast<NSUInteger>((resolutionX - 1) * (resolutionZ - 1) * levels.size());
        const NSUInteger threadgroupSize = std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, 256);
        [encoder dispatchThreads:MTLSizeMake(totalThreads, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(threadgroupSize, 1, 1)];
        [encoder endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];

        if (commandBuffer.status != MTLCommandBufferStatusCompleted)
        {
            return false;
        }

        const uint32_t emittedCount = std::min(*static_cast<uint32_t*>(counterBuffer.contents), kMaxSegments);
        const MetalOutputSegment* gpuOutput = static_cast<const MetalOutputSegment*>(outputBuffer.contents);
        segments.reserve(emittedCount);
        for (uint32_t index = 0; index < emittedCount; ++index)
        {
            const MetalOutputSegment& segment = gpuOutput[index];
            TerrainIsolineSegment outputSegment;
            outputSegment.levelHeight = segment.levelHeight;
            outputSegment.start = {segment.startLatitude, segment.startLongitude};
            outputSegment.end = {segment.endLatitude, segment.endLongitude};
            outputSegment.color = IsolineColorForHeight(segment.levelHeight, minHeight, maxHeight, settings.opacity);
            segments.push_back(outputSegment);
        }

        return true;
    }
}

} // namespace GeoFPS
