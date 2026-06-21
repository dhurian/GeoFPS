#include "Core/AssetSystem.h"
#include "Core/Time.h"

#include "Renderer/AnimatedMesh.h"
#include "Renderer/Mesh.h"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace GeoFPS
{
namespace
{
void ReleasePixelBuffer(std::vector<unsigned char>& pixels)
{
    std::vector<unsigned char>().swap(pixels);
}
} // namespace

void UploadImportedPrimitiveTextures(ImportedPrimitiveData& primitive)
{
    primitive.hasBaseColorTexture =
        !primitive.baseColorPixels.empty() && primitive.baseColorTexture.LoadFromMemory(primitive.baseColorPixels.data(),
                                                                                        primitive.baseColorWidth,
                                                                                        primitive.baseColorHeight,
                                                                                        primitive.baseColorChannels);
    ReleasePixelBuffer(primitive.baseColorPixels);

    primitive.hasMetallicRoughnessTexture =
        !primitive.metallicRoughnessPixels.empty() &&
        primitive.metallicRoughnessTexture.LoadFromMemory(primitive.metallicRoughnessPixels.data(),
                                                          primitive.metallicRoughnessWidth,
                                                          primitive.metallicRoughnessHeight,
                                                          primitive.metallicRoughnessChannels);
    ReleasePixelBuffer(primitive.metallicRoughnessPixels);

    primitive.hasNormalTexture =
        !primitive.normalPixels.empty() && primitive.normalTexture.LoadFromMemory(primitive.normalPixels.data(),
                                                                                  primitive.normalWidth,
                                                                                  primitive.normalHeight,
                                                                                  primitive.normalChannels);
    ReleasePixelBuffer(primitive.normalPixels);

    primitive.hasEmissiveTexture =
        !primitive.emissivePixels.empty() && primitive.emissiveTexture.LoadFromMemory(primitive.emissivePixels.data(),
                                                                                      primitive.emissiveWidth,
                                                                                      primitive.emissiveHeight,
                                                                                      primitive.emissiveChannels);
    ReleasePixelBuffer(primitive.emissivePixels);
}

void AssetSystem::ProcessLoadJobs(AssetJobContext& ctx)
{
    for (auto iterator = m_LoadJobs.begin(); iterator != m_LoadJobs.end();)
    {
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        AssetLoadResult result;
        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background asset load failed: ") + exception.what();
        }

        if (iterator->assetIndex >= 0 && iterator->assetIndex < static_cast<int>(m_Assets.size()))
        {
            ImportedAsset& asset = m_Assets[static_cast<size_t>(iterator->assetIndex)];
            if (result.success)
            {
                asset.assetData = std::move(result.assetData);
                size_t totalVerts = 0, totalTris = 0;
                for (ImportedPrimitiveData& primitive : asset.assetData.primitives)
                {
                    const double uploadStartMs = NowMs();
                    primitive.mesh = std::make_unique<Mesh>(primitive.meshData);
                    ctx.diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - uploadStartMs);
                    ++ctx.diagnostics.meshUploadsThisFrame;
                    if (primitive.isSkinned && !primitive.skinMeshData.vertices.empty())
                    {
                        const double skinUploadStartMs = NowMs();
                        primitive.skinnedMesh = std::make_unique<AnimatedMesh>(primitive.skinMeshData);
                        ctx.diagnostics.meshUploadCpuMs += static_cast<float>(NowMs() - skinUploadStartMs);
                        ++ctx.diagnostics.meshUploadsThisFrame;
                    }
                    UploadImportedPrimitiveTextures(primitive);
                    totalVerts += primitive.meshData.vertices.size();
                    totalTris  += primitive.meshData.indices.size() / 3u;
                }
                // Reset animation state so it matches the new asset data.
                asset.animState = AnimationState{};
                asset.loaded = true;
                std::cout << "[GeoFPS] Asset '" << asset.name << "' loaded: "
                          << asset.assetData.primitives.size() << " primitives, "
                          << totalVerts << " vertices, " << totalTris << " triangles";
                if (asset.assetData.hasSkin)
                    std::cout << ", skinned (" << asset.assetData.skeleton.joints.size() << " joints, "
                              << asset.assetData.animations.size() << " clips)";
                if (asset.assetData.hasNodeAnimation)
                    std::cout << ", node-anim (" << asset.assetData.nodeAnimations.size() << " clips)";
                std::cout << '\n';

                // Compute AABB for raycast picking (only on background-loaded assets)
                asset.aabbMin   = glm::vec3( std::numeric_limits<float>::max());
                asset.aabbMax   = glm::vec3(-std::numeric_limits<float>::max());
                asset.aabbValid = false;
                for (const ImportedPrimitiveData& prim : asset.assetData.primitives)
                {
                    for (const Vertex& v : prim.meshData.vertices)
                    {
                        asset.aabbMin = glm::min(asset.aabbMin, v.position);
                        asset.aabbMax = glm::max(asset.aabbMax, v.position);
                    }
                }
                if (asset.aabbMin.x <= asset.aabbMax.x)
                {
                    asset.aabbMin  *= asset.scale;
                    asset.aabbMax  *= asset.scale;
                    asset.aabbValid = true;
                }

                ctx.statusMessage = result.statusMessage;
            }
            else
            {
                std::cerr << "[GeoFPS] Asset '" << asset.name << "' FAILED: " << result.statusMessage << '\n';
                asset.loaded = false;
                asset.assetData.primitives.clear();
                ctx.statusMessage = result.statusMessage;
            }
        }
        else
        {
            ctx.statusMessage = result.statusMessage.empty() ? "Background asset job finished for a removed asset." :
                                                               result.statusMessage;
        }

        iterator = m_LoadJobs.erase(iterator);
    }
}
} // namespace GeoFPS
