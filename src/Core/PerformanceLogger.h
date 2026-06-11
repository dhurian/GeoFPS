#pragma once

#include "Core/DiagnosticsState.h"

#include <glm/glm.hpp>

#include <fstream>
#include <string>

namespace GeoFPS
{
// Per-frame state that the logger needs but which does not live in
// DiagnosticsState — camera pose, raw mouse delta, background-job depth,
// and the frame-pacer schedule.  Gathered by Application each frame and
// handed to WriteRow alongside the diagnostics aggregate.
struct PerfLogExtras
{
    float     cameraYaw {0.0f};
    float     cameraPitch {0.0f};
    glm::vec3 cameraPosition {0.0f};
    glm::dvec2 rawMouseDelta {0.0};
    size_t    tileJobsPending {0};
    bool      isolineBuildPending {false};
    float     avgSwapWaitMs {0.0f};
    double    paceTargetMs {0.0};
    double    paceActualStartMs {0.0};
};

// CSV performance logger toggled with the R hotkey.  When active it appends
// one row per frame to ~/geofps_perf_<timestamp>.csv capturing every
// diagnostic the in-app panel shows plus a handful of internals, so a short
// recording on a stuttering scene pinpoints which frame phase or counter is
// spiking.  Owns only the file lifecycle and row formatting; Application
// supplies the data each frame.
class PerformanceLogger
{
  public:
    [[nodiscard]] bool IsActive() const { return m_Active; }

    // Start (if stopped) or stop (if running) recording.  Returns a
    // human-readable status line for the caller to surface in the UI status
    // bar; also logs to stdout/stderr.
    std::string Toggle();

    // Append one row.  No-op when the log is not active.
    void WriteRow(const DiagnosticsState& diagnostics, const PerfLogExtras& extras);

  private:
    std::ofstream m_File;
    std::string   m_Path;
    bool          m_Active {false};
    int           m_FrameIdx {0};
    double        m_StartMs {0.0};
};
} // namespace GeoFPS
