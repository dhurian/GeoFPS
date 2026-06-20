#pragma once

#include "Terrain/TerrainProfile.h"

#include <future>
#include <string>
#include <vector>

namespace GeoFPS
{
// Async terrain-profile sampling state — the result payload produced on a
// worker thread (a freshly resampled set of profiles) and the in-flight job
// that delivers it.  Lifted out of class Application (where
// ProfileSampleBuildResult was public only so the worker free function could
// construct it) so ProfileSystem can own the job vector.
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
} // namespace GeoFPS
