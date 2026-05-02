#pragma once

#include "Renderer/Camera.h"

#include <glm/glm.hpp>

namespace GeoFPS
{
struct CameraSnapState
{
    float targetYaw {0.0f};
    float targetPitch {0.0f};
    bool active {false};
};

struct CameraCommandFrame
{
    bool hasTeleport {false};
    glm::vec3 teleportPosition {0.0f};

    glm::vec3 localMoveAxes {0.0f};
    float moveDistanceMeters {0.0f};

    glm::vec2 lookDeltaDegrees {0.0f};
    bool cancelSnap {false};

    bool hasSnapTarget {false};
    float snapTargetYaw {0.0f};
    float snapTargetPitch {0.0f};

    void Clear();
};

// Returns the look delta that was actually applied this frame (degrees).
[[nodiscard]] glm::vec2 ApplyCameraCommandFrame(Camera& camera,
                                                CameraCommandFrame& command,
                                                CameraSnapState& snapState,
                                                float deltaTime);
} // namespace GeoFPS
