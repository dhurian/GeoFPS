#pragma once

#include "Terrain/TerrainProfile.h"

#include <vector>

namespace GeoFPS
{
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

  private:
    std::vector<TerrainProfile> m_Profiles;
    int m_ActiveIndex {0};
};
} // namespace GeoFPS
