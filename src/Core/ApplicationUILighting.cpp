#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "imgui.h"
#include <algorithm>
#include <string>

namespace GeoFPS
{
using namespace ApplicationInternal;

void Application::RenderSunWindow()
{
    if (!m_ShowSunWindow)
    {
        return;
    }

    if (!ImGui::Begin("Sun Illumination", &m_ShowSunWindow))
    {
        ImGui::End();
        return;
    }

    RenderSunControls();
    ImGui::End();
}

void Application::RenderSunControls()
{
    ImGui::Separator();
    ImGui::Text("Sun And Illumination");

    const CityPreset& selectedCityPreset = kCityPresets[static_cast<size_t>(std::clamp(
        m_SelectedCityPresetIndex, 0, static_cast<int>(kCityPresets.size()) - 1))];
    if (ImGui::BeginCombo("Major City", selectedCityPreset.name))
    {
        for (int index = 0; index < static_cast<int>(kCityPresets.size()); ++index)
        {
            const bool isSelected = index == m_SelectedCityPresetIndex;
            if (ImGui::Selectable(kCityPresets[static_cast<size_t>(index)].name, isSelected))
            {
                m_SelectedCityPresetIndex = index;
            }
            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (m_SelectedCityPresetIndex > 0 && ImGui::Button("Apply City Preset"))
    {
        const CityPreset& city = kCityPresets[static_cast<size_t>(m_SelectedCityPresetIndex)];
        m_GeoReference.originLatitude = city.latitude;
        m_GeoReference.originLongitude = city.longitude;
        m_SunSettings.utcOffsetHours = SuggestedUtcOffsetForCityPreset(m_SelectedCityPresetIndex, m_SunSettings);
        m_StatusMessage = std::string("Applied city preset: ") + city.name;
    }
    if (m_SelectedCityPresetIndex > 0)
    {
        ImGui::Text("Preset lat/lon: %.4f, %.4f", selectedCityPreset.latitude, selectedCityPreset.longitude);
    }

    ImGui::Checkbox("Use Geographic Sun", &m_SunSettings.useGeographicSun);
    if (m_SunSettings.useGeographicSun)
    {
        ImGui::InputInt("Sun Year", &m_SunSettings.year);
        m_SunSettings.year = std::max(m_SunSettings.year, 1900);
        ImGui::SliderInt("Sun Month", &m_SunSettings.month, 1, 12);
        const int maxDay = DaysInMonth(m_SunSettings.year, m_SunSettings.month);
        m_SunSettings.day = std::clamp(m_SunSettings.day, 1, maxDay);
        ImGui::SliderInt("Sun Day", &m_SunSettings.day, 1, maxDay);
        ImGui::SliderFloat("Local Time", &m_SunSettings.localTimeHours, 0.0f, 24.0f, "%.2f h");
        ImGui::SliderFloat("UTC Offset", &m_SunSettings.utcOffsetHours, -12.0f, 14.0f, "%.1f h");
    }
    else
    {
        ImGui::SliderFloat("Sun Azimuth", &m_SunSettings.manualAzimuthDegrees, 0.0f, 360.0f, "%.1f deg");
        ImGui::SliderFloat("Sun Elevation", &m_SunSettings.manualElevationDegrees, -10.0f, 90.0f, "%.1f deg");
    }

    ImGui::SliderFloat("Sun Strength", &m_SunSettings.illuminance, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Ambient Light", &m_SunSettings.ambientStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Sky Brightness", &m_SunSettings.skyBrightness, 0.2f, 2.0f, "%.2f");

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Sky Background", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Enable Sky", &m_SkySettings.enabled);
        if (m_SkySettings.enabled)
        {
            ImGui::Checkbox("Sun-Driven Colors", &m_SkySettings.useSunDrivenColor);
            if (!m_SkySettings.useSunDrivenColor)
            {
                ImGui::ColorEdit3("Zenith Color",  &m_SkySettings.zenithColor.x);
                ImGui::ColorEdit3("Horizon Color", &m_SkySettings.horizonColor.x);
            }
            ImGui::SliderFloat("Horizon Sharpness", &m_SkySettings.horizonSharpness, 0.5f, 16.0f, "%.1f");
            ImGui::Checkbox("Show Sun Disk", &m_SkySettings.showSunDisk);
            if (m_SkySettings.showSunDisk)
            {
                ImGui::SliderFloat("Sun Disk Size",      &m_SkySettings.sunDiskSize,      0.001f, 0.05f,  "%.4f");
                ImGui::SliderFloat("Sun Disk Intensity", &m_SkySettings.sunDiskIntensity, 0.5f,   10.0f,  "%.1f");
            }

            ImGui::Separator();
            ImGui::Checkbox("Enable Clouds", &m_SkySettings.cloudsEnabled);
            if (m_SkySettings.cloudsEnabled)
            {
                ImGui::SliderFloat("Cloud Coverage",  &m_SkySettings.cloudCoverage, 0.0f,    1.0f,    "%.2f");
                ImGui::SliderFloat("Cloud Density",   &m_SkySettings.cloudDensity,  0.0f,    1.0f,    "%.2f");
                ImGui::SliderFloat("Cloud Scale",     &m_SkySettings.cloudScale,    0.1f,    4.0f,    "%.2f");

                ImGui::Separator();
                ImGui::Text("Wind");
                // DragFloat lets the user type an exact value (Ctrl+Click) or drag for quick adjustment
                ImGui::DragFloat("Wind W→E (m/s)",    &m_SkySettings.cloudSpeedX,  0.5f, -150.0f, 150.0f, "%.1f m/s");
                ImGui::DragFloat("Wind S→N (m/s)",    &m_SkySettings.cloudSpeedY,  0.5f, -150.0f, 150.0f, "%.1f m/s");
                // Show approximate wind speed magnitude as a quick read-out
                const float windMag = std::sqrt(m_SkySettings.cloudSpeedX * m_SkySettings.cloudSpeedX +
                                                m_SkySettings.cloudSpeedY * m_SkySettings.cloudSpeedY);
                ImGui::Text("  Wind speed: %.1f m/s  (%.0f km/h)", windMag, windMag * 3.6f);

                ImGui::Separator();
                ImGui::Text("Cloud Altitude");
                ImGui::SliderFloat("Altitude (m)",    &m_SkySettings.cloudAltitude, 100.0f, 8000.0f, "%.0f m");
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Height of the cloud layer above the ground.\n"
                                      "Low: 100–500 m (fog/stratus)\n"
                                      "Cumulus: 600–2000 m\n"
                                      "High cirrus: 6000–8000 m");

                ImGui::Separator();
                ImGui::Checkbox("Auto Cloud Color",   &m_SkySettings.cloudAutoColor);
                if (!m_SkySettings.cloudAutoColor)
                {
                    ImGui::ColorEdit3("Cloud Color", &m_SkySettings.cloudColor.x);
                }
            }
        }
    }

    ImGui::Separator();
    const SunParameters sun = ComputeSunParameters(m_GeoReference, m_SunSettings);
    ImGui::Text("Computed Azimuth: %.1f deg", sun.azimuthDegrees);
    ImGui::Text("Computed Elevation: %.1f deg", sun.elevationDegrees);
    ImGui::Text("Solar Intensity: %.2f", sun.intensity);
}
} // namespace GeoFPS
