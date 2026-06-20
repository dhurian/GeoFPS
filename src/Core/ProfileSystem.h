#pragma once

#include "Core/ProfileJobs.h"
#include "Terrain/TerrainProfile.h"

#include <string>
#include <vector>

namespace GeoFPS
{
// Everything ProcessSampleJobs needs from outside the profile store: just the
// status string it sets when a resample finishes (no diagnostics, no upload
// budget, no callbacks).
struct ProfileJobContext
{
    std::string& statusMessage;
};

// Owns the terrain-profile data store: the profiles and which one is active.
// First step of lifting the (large, UI-coupled) profile subsystem out of the
// Application god-class.
//
// Scope is deliberately narrow for now: this holds the *data*.  Application
// still drives all the orchestration — drawing/editing the profiles, the map
// and elevation-graph view state, async sample rebuilding, and persistence —
// reaching the store through these accessors.  The async sample-job vector and
// the view-state fields move in later steps.
class ProfileSystem
{
  public:
    [[nodiscard]] std::vector<TerrainProfile>& profiles() { return m_Profiles; }
    [[nodiscard]] const std::vector<TerrainProfile>& profiles() const { return m_Profiles; }

    [[nodiscard]] int activeIndex() const { return m_ActiveIndex; }
    void setActiveIndex(int index) { m_ActiveIndex = index; }

    [[nodiscard]] std::vector<ProfileSampleBuildJob>& sampleBuildJobs() { return m_SampleBuildJobs; }
    [[nodiscard]] const std::vector<ProfileSampleBuildJob>& sampleBuildJobs() const { return m_SampleBuildJobs; }

    // Harvest finished async profile-sampling jobs: install the resampled
    // profiles, re-clamp the active index, and set the status string.  Called
    // once per frame from Application::ProcessBackgroundJobs.
    void ProcessSampleJobs(ProfileJobContext& ctx);

  private:
    std::vector<TerrainProfile> m_Profiles;
    int m_ActiveIndex {0};
    std::vector<ProfileSampleBuildJob> m_SampleBuildJobs;
};
} // namespace GeoFPS
