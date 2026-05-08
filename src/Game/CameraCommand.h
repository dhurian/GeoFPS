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

    // Full reset — used by ApplyCameraCommandFrame after consuming the command.
    void Clear();
    // Partial reset — zeroes only the per-frame FPS input fields (move + look)
    // so that UI-queued commands (teleport, snap, cancelSnap) survive until
    // ApplyCameraCommandFrame consumes them on the next Apply call.
    void ClearMoveLook();
};

// Diagnostic result of consuming one CameraCommandFrame.  The bare vec2 of
// "applied look delta" wasn't enough for callers that need to know whether any
// command actually fired (e.g. tests, telemetry) or whether a snap just began.
struct CameraCommandResult
{
    // True if the command produced a visible state change this frame: a
    // teleport, a snap target being set/cancelled, a mouse-look rotation, or
    // a translation.  Snap *interpolation* alone (no new command) does not
    // flip this — it only reflects the command itself.
    bool applied {false};

    // Mouse-look rotation actually applied this frame, in degrees.
    // (Same value the previous glm::vec2 return type carried.)
    glm::vec2 appliedLookDeltaDegrees {0.0f};

    // True iff this frame's command set a new snap target (i.e. hasSnapTarget
    // was true and we transitioned snapState.active from false to true, or
    // re-armed it with a new target).
    bool snapStarted {false};
};

[[nodiscard]] CameraCommandResult ApplyCameraCommandFrame(Camera& camera,
                                                          CameraCommandFrame& command,
                                                          CameraSnapState& snapState,
                                                          float deltaTime);
} // namespace GeoFPS
