#include "Core/ProfileSystem.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <string>
#include <utility>

namespace GeoFPS
{
void ProfileSystem::ProcessSampleJobs(ProfileJobContext& ctx)
{
    for (auto iterator = m_SampleBuildJobs.begin(); iterator != m_SampleBuildJobs.end();)
    {
        if (iterator->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++iterator;
            continue;
        }

        ProfileSampleBuildResult result;
        try
        {
            result = iterator->future.get();
        }
        catch (const std::exception& exception)
        {
            result.statusMessage = std::string("Background profile sampling failed: ") + exception.what();
        }

        if (result.success)
        {
            size_t totalSamples = 0;
            for (const auto& p : result.profiles) totalSamples += p.samples.size();
            std::cout << "[GeoFPS] Profile samples rebuilt: " << result.profiles.size()
                      << " profiles, " << totalSamples << " total samples\n";
            m_Profiles = std::move(result.profiles);
            m_ActiveIndex = m_Profiles.empty() ?
                                -1 :
                                std::clamp(m_ActiveIndex, 0, static_cast<int>(m_Profiles.size()) - 1);
        }
        ctx.statusMessage = result.statusMessage;
        iterator = m_SampleBuildJobs.erase(iterator);
    }
}
} // namespace GeoFPS
