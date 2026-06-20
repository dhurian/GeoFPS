#pragma once

#include "Assets/GltfImporter.h"

#include <future>
#include <string>

namespace GeoFPS
{
// Async imported-asset load pipeline state — the result payload produced on a
// worker thread and the in-flight job that uploads it.  Lifted out of class
// Application (where AssetLoadResult was public only so the worker free
// function could construct it) so an AssetSystem can own the job vector.
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
} // namespace GeoFPS
