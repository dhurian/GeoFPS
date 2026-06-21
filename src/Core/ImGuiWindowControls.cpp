#include "Core/ImGuiWindowControls.h"

#include "imgui.h"

#include <string>
#include <unordered_map>

namespace GeoFPS
{
namespace
{
struct WindowRect
{
    float x {0.0f};
    float y {0.0f};
    float w {0.0f};
    float h {0.0f};
    bool  valid {false};
};

// Per-window "size before the last fit/snap", keyed by window name, so Restore
// is a one-level undo.  Survives across frames; a handful of entries at most.
std::unordered_map<std::string, WindowRect>& SavedRects()
{
    static std::unordered_map<std::string, WindowRect> rects;
    return rects;
}

// Work area to fit/snap against.  For a window docked in the main viewport that
// is just the viewport's work area (the app window).  But a window detached to
// its own OS viewport (multi-viewport) has a viewport sized to the window
// itself, so fitting against it would be a no-op — fit to the monitor the
// window currently sits on instead.
void ResolveWorkArea(const ImVec2& windowPos, const ImVec2& windowSize, ImVec2& outPos, ImVec2& outSize)
{
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    outPos = viewport->WorkPos;
    outSize = viewport->WorkSize;
    if (viewport == ImGui::GetMainViewport())
    {
        return;
    }

    const ImVec2 center(windowPos.x + windowSize.x * 0.5f, windowPos.y + windowSize.y * 0.5f);
    const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
    for (const ImGuiPlatformMonitor& monitor : platformIO.Monitors)
    {
        const float maxX = monitor.MainPos.x + monitor.MainSize.x;
        const float maxY = monitor.MainPos.y + monitor.MainSize.y;
        if (center.x >= monitor.MainPos.x && center.x < maxX &&
            center.y >= monitor.MainPos.y && center.y < maxY)
        {
            outPos = monitor.WorkPos;
            outSize = monitor.WorkSize;
            return;
        }
    }
}
} // namespace

void DrawWindowArrangeMenu(const char* windowName)
{
    const ImVec2 curPos  = ImGui::GetWindowPos();
    const ImVec2 curSize = ImGui::GetWindowSize();
    ImVec2 workPos;
    ImVec2 workSize;
    ResolveWorkArea(curPos, curSize, workPos, workSize);

    // Open the menu from the place that's actually visible in each state: a
    // top-right "Layout" button while expanded, or a right-click on the title
    // bar while collapsed (when the title bar is all that's left on screen).
    if (!ImGui::IsWindowCollapsed())
    {
        const char* label = "Layout";
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonWidth);
        if (ImGui::SmallButton(label))
        {
            ImGui::OpenPopup("##win_arrange");
        }
    }
    else if (ImGui::IsWindowHovered())
    {
        ImGui::SetTooltip("Right-click for layout options (Fit / Snap / Restore)");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("##win_arrange");
        }
    }

    if (ImGui::BeginPopup("##win_arrange"))
    {
        auto saveCurrent = [&]() {
            SavedRects()[windowName] = {curPos.x, curPos.y, curSize.x, curSize.y, true};
        };
        auto place = [&](const ImVec2& pos, const ImVec2& size) {
            ImGui::SetWindowCollapsed(windowName, false, ImGuiCond_Always);
            ImGui::SetWindowPos(windowName, pos, ImGuiCond_Always);
            ImGui::SetWindowSize(windowName, size, ImGuiCond_Always);
        };

        if (ImGui::MenuItem("Fit to screen"))
        {
            saveCurrent();
            place(workPos, workSize);
        }
        if (ImGui::MenuItem("Snap left half"))
        {
            saveCurrent();
            place(workPos, ImVec2(workSize.x * 0.5f, workSize.y));
        }
        if (ImGui::MenuItem("Snap right half"))
        {
            saveCurrent();
            place(ImVec2(workPos.x + workSize.x * 0.5f, workPos.y), ImVec2(workSize.x * 0.5f, workSize.y));
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Minimize"))
        {
            ImGui::SetWindowCollapsed(windowName, true, ImGuiCond_Always);
        }
        const auto saved = SavedRects().find(windowName);
        const bool hasSaved = saved != SavedRects().end() && saved->second.valid;
        if (ImGui::MenuItem("Restore size", nullptr, false, hasSaved))
        {
            const WindowRect& rect = saved->second;
            place(ImVec2(rect.x, rect.y), ImVec2(rect.w, rect.h));
        }
        ImGui::EndPopup();
    }
}
} // namespace GeoFPS
