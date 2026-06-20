#pragma once

#include "Terrain/TerrainProfile.h"

#include <glm/glm.hpp>

namespace GeoFPS
{
enum class ProfileElevationScaleMode
{
    Auto,
    OneX,
    TwoX,
    FiveX,
    TenX,
    Fixed
};

enum class ProfileMapSizeMode
{
    Small,
    Medium,
    Large,
    Fill
};

// Presentation state for the terrain-profile window — the map view, the
// elevation graph, the draw/edit modes, and the current selection/hover.  This
// is a plain aggregate (like DiagnosticsState): the rendering code in
// Application reads and writes these fields directly.  Grouping them here keeps
// the Application header from carrying ~30 loose profile-UI fields; it is not an
// encapsulation boundary.
struct ProfileViewState
{
    bool profileDrawMode {false};
    bool profileEditMode {true};
    bool showProfileHeightLabels {true};
    bool showProfileAerialImage {true};
    bool showProfileSamples {false};
    bool profileAuxiliaryDrawMode {false};
    bool profileGraphAuxiliaryInsertMode {false};
    bool profileGraphHoverActive {false};
    // ── Line of sight (Tier 1) ───────────────────────────────────────────────
    // When enabled, the elevation graph colours its line by what an observer
    // standing at the profile's first vertex (eye observerEyeHeightMeters above
    // the ground) can see: green = visible, red = hidden behind terrain.
    bool  lineOfSightEnabled {false};
    float observerEyeHeightMeters {2.0f};
    bool profileMapViewInitialized {false};
    bool profileMapIsPanning {false};
    int selectedProfileVertexIndex {-1};
    int selectedProfileSampleIndex {-1};
    int hoveredProfileSampleIndex {-1};
    TerrainProfileSample profileGraphHoverSample {};
    // Sample captured when the elevation graph's right-click context menu was
    // opened, so the menu acts on the point under the cursor at open time.
    TerrainProfileSample profileGraphContextSample {};
    ProfileElevationScaleMode profileScaleMode {ProfileElevationScaleMode::Auto};
    ProfileMapSizeMode profileMapSizeMode {ProfileMapSizeMode::Large};
    float profileDetailsWidth {360.0f};
    float profileFixedMinHeight {0.0f};
    float profileFixedMaxHeight {100.0f};
    double profileMapMinLatitude {0.0};
    double profileMapMaxLatitude {0.0};
    double profileMapMinLongitude {0.0};
    double profileMapMaxLongitude {0.0};
    float profileMapLastWidth {720.0f};
    glm::vec2 profileMapLastPanMouse {0.0f};
    double profileGraphZoomMinDist {0.0};   // metres along path — left edge of graph view
    double profileGraphZoomMaxDist {-1.0};  // right edge; negative = show full profile
    unsigned int profileLineVao {0};
    unsigned int profileLineVbo {0};
};
} // namespace GeoFPS
