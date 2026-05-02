#pragma once

#include "Core/Application.h"

#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace GeoFPS::ApplicationInternal
{
constexpr double kPi = 3.14159265358979323846;

struct TerrainBounds
{
    double minLatitude {std::numeric_limits<double>::max()};
    double maxLatitude {std::numeric_limits<double>::lowest()};
    double minLongitude {std::numeric_limits<double>::max()};
    double maxLongitude {std::numeric_limits<double>::lowest()};
    double minHeight {std::numeric_limits<double>::max()};
    double maxHeight {std::numeric_limits<double>::lowest()};
};

inline glm::vec2 ToGroundPlane(const glm::dvec3& localPosition)
{
    return {static_cast<float>(localPosition.x), static_cast<float>(localPosition.z)};
}

inline TerrainBounds ComputeTerrainBounds(const std::vector<TerrainPoint>& points)
{
    TerrainBounds bounds;
    for (const auto& point : points)
    {
        bounds.minLatitude = std::min(bounds.minLatitude, point.latitude);
        bounds.maxLatitude = std::max(bounds.maxLatitude, point.latitude);
        bounds.minLongitude = std::min(bounds.minLongitude, point.longitude);
        bounds.maxLongitude = std::max(bounds.maxLongitude, point.longitude);
        bounds.minHeight = std::min(bounds.minHeight, point.height);
        bounds.maxHeight = std::max(bounds.maxHeight, point.height);
    }
    return bounds;
}

inline bool FileExists(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

inline void LoadProfessionalUiFont()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float fontSize = 17.0f;

#ifdef __APPLE__
    const std::vector<const char*> candidates = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica Neue.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
#elif defined(_WIN32)
    const std::vector<const char*> candidates = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
#else
    const std::vector<const char*> candidates = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
    };
#endif

    for (const char* path : candidates)
    {
        if (!FileExists(path))
        {
            continue;
        }

        if (io.Fonts->AddFontFromFileTTF(path, fontSize) != nullptr)
        {
            return;
        }
    }

    io.Fonts->AddFontDefault();
}

inline std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

inline bool IsLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

inline int DaysInMonth(int year, int month)
{
    static constexpr std::array<int, 12> kMonthLengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 30;
    }

    if (month == 2 && IsLeapYear(year))
    {
        return 29;
    }

    return kMonthLengths[static_cast<size_t>(month - 1)];
}

inline int DayOfYear(int year, int month, int day)
{
    int total = 0;
    for (int currentMonth = 1; currentMonth < month; ++currentMonth)
    {
        total += DaysInMonth(year, currentMonth);
    }
    return total + day;
}

struct SunParameters
{
    glm::vec3 direction {0.5f, 1.0f, 0.35f};
    glm::vec3 color {1.0f, 0.97f, 0.92f};
    glm::vec3 skyColor {0.08f, 0.10f, 0.14f};
    float intensity {1.0f};
    float ambient {0.22f};
    float elevationDegrees {45.0f};
    float azimuthDegrees {180.0f};
};

struct CityPreset
{
    const char* name;
    double latitude;
    double longitude;
    float suggestedUtcOffsetHours;
};

inline constexpr std::array<CityPreset, 15> kCityPresets = {{
    {"Custom Coordinates", 0.0, 0.0, 0.0f},
    {"Copenhagen, Denmark", 55.6761, 12.5683, 1.0f},
    {"London, United Kingdom", 51.5074, -0.1278, 0.0f},
    {"Paris, France", 48.8566, 2.3522, 1.0f},
    {"New York, USA", 40.7128, -74.0060, -5.0f},
    {"Los Angeles, USA", 34.0522, -118.2437, -8.0f},
    {"Mexico City, Mexico", 19.4326, -99.1332, -6.0f},
    {"Rio de Janeiro, Brazil", -22.9068, -43.1729, -3.0f},
    {"Cairo, Egypt", 30.0444, 31.2357, 2.0f},
    {"Nairobi, Kenya", -1.2921, 36.8219, 3.0f},
    {"Dubai, UAE", 25.2048, 55.2708, 4.0f},
    {"Mumbai, India", 19.0760, 72.8777, 5.5f},
    {"Singapore", 1.3521, 103.8198, 8.0f},
    {"Tokyo, Japan", 35.6762, 139.6503, 9.0f},
    {"Sydney, Australia", -33.8688, 151.2093, 10.0f},
}};

inline SunParameters ComputeSunParameters(const GeoReference& reference, const SunSettings& settings)
{
    SunParameters sun;
    sun.ambient = std::clamp(settings.ambientStrength, 0.0f, 1.0f);

    float elevationDegrees = settings.manualElevationDegrees;
    float azimuthDegrees = settings.manualAzimuthDegrees;

    if (settings.useGeographicSun)
    {
        const int safeMonth = std::clamp(settings.month, 1, 12);
        const int safeDay = std::clamp(settings.day, 1, DaysInMonth(settings.year, safeMonth));
        const int dayOfYear = DayOfYear(settings.year, safeMonth, safeDay);
        const double localTime = static_cast<double>(settings.localTimeHours);
        const double gamma = (2.0 * kPi / 365.0) * (static_cast<double>(dayOfYear) - 1.0 + ((localTime - 12.0) / 24.0));
        const double declination = 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
                                   0.006758 * std::cos(2.0 * gamma) + 0.000907 * std::sin(2.0 * gamma) -
                                   0.002697 * std::cos(3.0 * gamma) + 0.00148 * std::sin(3.0 * gamma);
        const double equationOfTime = 229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                                                0.014615 * std::cos(2.0 * gamma) - 0.040849 * std::sin(2.0 * gamma));
        const double timeOffsetMinutes =
            equationOfTime + (4.0 * reference.originLongitude) - (60.0 * static_cast<double>(settings.utcOffsetHours));
        double trueSolarMinutes = (localTime * 60.0) + timeOffsetMinutes;
        while (trueSolarMinutes < 0.0)
        {
            trueSolarMinutes += 1440.0;
        }
        while (trueSolarMinutes >= 1440.0)
        {
            trueSolarMinutes -= 1440.0;
        }

        const double hourAngle = ((trueSolarMinutes / 4.0) - 180.0) * (kPi / 180.0);
        const double latitudeRadians = reference.originLatitude * (kPi / 180.0);
        const double cosineZenith = std::clamp((std::sin(latitudeRadians) * std::sin(declination)) +
                                                   (std::cos(latitudeRadians) * std::cos(declination) * std::cos(hourAngle)),
                                               -1.0,
                                               1.0);
        const double zenith = std::acos(cosineZenith);
        const double elevationRadians = (kPi * 0.5) - zenith;
        const double azimuthRadians =
            std::atan2(std::sin(hourAngle),
                       (std::cos(hourAngle) * std::sin(latitudeRadians)) -
                           (std::tan(declination) * std::cos(latitudeRadians))) +
            kPi;

        elevationDegrees = static_cast<float>(elevationRadians * (180.0 / kPi));
        azimuthDegrees = static_cast<float>(azimuthRadians * (180.0 / kPi));
    }

    sun.elevationDegrees = elevationDegrees;
    sun.azimuthDegrees = azimuthDegrees;

    const float elevationRadians = glm::radians(elevationDegrees);
    const float azimuthRadians = glm::radians(azimuthDegrees);
    sun.direction = glm::normalize(glm::vec3(std::cos(elevationRadians) * std::sin(azimuthRadians),
                                             std::sin(elevationRadians),
                                             std::cos(elevationRadians) * std::cos(azimuthRadians)));

    const float daylight = std::clamp(std::sin(elevationRadians) * 1.15f + 0.1f, 0.0f, 1.0f);
    sun.intensity = std::max(daylight * settings.illuminance, 0.02f);
    sun.color = glm::mix(glm::vec3(1.0f, 0.62f, 0.38f), glm::vec3(1.0f, 0.97f, 0.92f), daylight);
    sun.skyColor = glm::mix(glm::vec3(0.02f, 0.03f, 0.08f),
                            glm::vec3(0.35f, 0.52f, 0.78f) * settings.skyBrightness,
                            daylight);
    sun.ambient = std::clamp(sun.ambient + (0.18f * daylight), 0.04f, 1.0f);
    return sun;
}

inline void ApplySunUniforms(const Shader& shader, const SunParameters& sun)
{
    shader.SetVec3("uSunDirection", sun.direction);
    shader.SetVec3("uSunColor", sun.color);
    shader.SetFloat("uSunIntensity", sun.intensity);
    shader.SetFloat("uAmbientStrength", sun.ambient);
}

inline float SuggestedUtcOffsetForCityPreset(int presetIndex, const SunSettings& settings)
{
    const int safeIndex = std::clamp(presetIndex, 0, static_cast<int>(kCityPresets.size()) - 1);
    float offset = kCityPresets[static_cast<size_t>(safeIndex)].suggestedUtcOffsetHours;
    const bool isNorthernSummer = settings.month >= 4 && settings.month <= 10;

    if (safeIndex == 1 || safeIndex == 2 || safeIndex == 3 || safeIndex == 4 || safeIndex == 5)
    {
        offset += isNorthernSummer ? 1.0f : 0.0f;
    }
    else if (safeIndex == 14)
    {
        offset += (settings.month >= 10 || settings.month <= 3) ? 1.0f : 0.0f;
    }

    return offset;
}

inline void ApplyProfessionalImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 9.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.IndentSpacing = 12.0f;
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.WindowPadding = ImVec2(14.0f, 14.0f);

    // Lighter mid-dark slate palette — same hue family as before but ~2× brighter
    // backgrounds so the UI feels airy rather than heavy/cramped.
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.93f, 0.95f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.58f, 0.64f, 0.70f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.15f, 0.19f, 0.24f, 0.97f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.18f, 0.23f, 0.28f, 0.92f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.16f, 0.20f, 0.25f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.32f, 0.39f, 0.46f, 0.85f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.22f, 0.28f, 0.34f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.29f, 0.36f, 0.43f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.34f, 0.43f, 0.51f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.13f, 0.17f, 0.21f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.18f, 0.23f, 0.29f, 1.00f);
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.14f, 0.18f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.14f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.36f, 0.43f, 0.50f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.44f, 0.52f, 0.59f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.52f, 0.60f, 0.68f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.40f, 0.84f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.38f, 0.70f, 0.83f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.52f, 0.84f, 0.95f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.24f, 0.32f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.32f, 0.43f, 0.51f, 1.00f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.42f, 0.57f, 0.66f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.24f, 0.33f, 0.40f, 1.00f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.32f, 0.44f, 0.52f, 1.00f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.40f, 0.55f, 0.63f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.30f, 0.36f, 0.43f, 0.90f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.42f, 0.50f, 0.58f, 0.35f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.54f, 0.70f, 0.90f, 0.60f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.62f, 0.80f, 0.98f, 0.80f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.22f, 0.27f, 0.34f, 1.00f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.34f, 0.43f, 0.52f, 1.00f);
    colors[ImGuiCol_TabActive]             = ImVec4(0.28f, 0.36f, 0.44f, 1.00f);
    colors[ImGuiCol_TabUnfocused]          = ImVec4(0.18f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.23f, 0.28f, 0.35f, 1.00f);
}

inline std::string Trim(const std::string& value)
{
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return {};
    }

    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

inline bool ParseBool(const std::string& value)
{
    const std::string lowered = ToLower(Trim(value));
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

inline double ParseDouble(const std::string& value, double fallback = 0.0)
{
    try
    {
        return std::stod(Trim(value));
    }
    catch (...)
    {
        return fallback;
    }
}

inline int ParseInt(const std::string& value, int fallback = 0)
{
    try
    {
        return std::stoi(Trim(value));
    }
    catch (...)
    {
        return fallback;
    }
}
} // namespace GeoFPS::ApplicationInternal
