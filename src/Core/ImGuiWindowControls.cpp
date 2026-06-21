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
} // namespace

void DrawWindowArrangeMenu(const char* windowName)
{
    // Capture everything we need from the *parent* window's scope: the popup
    // opened below renders as its own window, so GetWindowViewport()/GetWindowPos()
    // would read the popup there, not this window.
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const ImVec2 workPos  = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;
    const ImVec2 curPos   = ImGui::GetWindowPos();
    const ImVec2 curSize  = ImGui::GetWindowSize();

    // Right-aligned compact button on its own toolbar row at the top of content.
    const char* label = "Layout";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonWidth = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonWidth);
    if (ImGui::SmallButton(label))
    {
        ImGui::OpenPopup("##win_arrange");
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
