#pragma once

namespace GeoFPS
{
// Window creature-comforts.  Call once at the top of a window's content (right
// after the Begin()-returned-false guard) to add a compact "Layout" button to
// the top-right of the window.  The button opens a menu with one-click arranging:
//
//   • Fit to screen   — maximize to the viewport's work area
//   • Snap left/right  — half the work area, for side-by-side layouts
//   • Minimize         — collapse to the title bar (the native title-bar triangle
//                        brings it back, so it stays one click from restored)
//   • Restore size     — undo the last fit/snap, back to the prior position+size
//
// Operates on the window by name via ImGui's name-addressed setters, so it works
// whether the window is docked in the main viewport or detached to its own OS
// window (multi-viewport).  Pure ImGui — no Application coupling.
void DrawWindowArrangeMenu(const char* windowName);
} // namespace GeoFPS
