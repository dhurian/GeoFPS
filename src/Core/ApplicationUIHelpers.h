#pragma once

#include "Core/Application.h"
#include "Core/ApplicationInternal.h"

#include "imgui.h"
#include <algorithm>
#include <filesystem>

namespace GeoFPS::ApplicationUIInternal
{
inline const char* ProfileScaleModeLabel(ProfileElevationScaleMode mode)
{
    switch (mode)
    {
    case ProfileElevationScaleMode::Auto: return "Auto";
    case ProfileElevationScaleMode::OneX: return "1x";
    case ProfileElevationScaleMode::TwoX: return "2x";
    case ProfileElevationScaleMode::FiveX: return "5x";
    case ProfileElevationScaleMode::TenX: return "10x";
    case ProfileElevationScaleMode::Fixed: return "Fixed";
    }
    return "Auto";
}

inline const char* ProfileMapSizeModeLabel(ProfileMapSizeMode mode)
{
    switch (mode)
    {
    case ProfileMapSizeMode::Small: return "Small";
    case ProfileMapSizeMode::Medium: return "Medium";
    case ProfileMapSizeMode::Large: return "Large";
    case ProfileMapSizeMode::Fill: return "Fill";
    }
    return "Large";
}

inline ImU32 ProfileColorU32(const TerrainProfile& profile)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(profile.color.r, profile.color.g, profile.color.b, profile.color.a));
}

inline ImU32 ColorU32(const glm::vec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
}

inline ApplicationInternal::TerrainBounds ToTerrainBounds(const TerrainDatasetBounds& cachedBounds)
{
    ApplicationInternal::TerrainBounds bounds;
    if (!cachedBounds.valid)
    {
        return bounds;
    }

    bounds.minLatitude = cachedBounds.minLatitude;
    bounds.maxLatitude = cachedBounds.maxLatitude;
    bounds.minLongitude = cachedBounds.minLongitude;
    bounds.maxLongitude = cachedBounds.maxLongitude;
    bounds.minHeight = cachedBounds.minHeight;
    bounds.maxHeight = cachedBounds.maxHeight;
    return bounds;
}

inline TerrainProfileVertex ProfileVertexAsGeographic(const TerrainProfile& profile,
                                                      const TerrainProfileVertex& vertex,
                                                      const GeoConverter& converter,
                                                      TerrainCoordinateMode coordinateMode = TerrainCoordinateMode::Geographic)
{
    if (!profile.useLocalCoordinates)
    {
        return vertex;
    }

    if (coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        TerrainProfileVertex converted = vertex;
        converted.latitude = vertex.localPosition.x;
        converted.longitude = vertex.localPosition.z;
        return converted;
    }

    const glm::dvec3 geographic = converter.ToGeographic(vertex.localPosition);
    TerrainProfileVertex converted = vertex;
    converted.latitude = geographic.x;
    converted.longitude = geographic.y;
    return converted;
}

inline TerrainProfileVertex MakeProfileVertexFromGeographic(const TerrainProfile& profile,
                                                            double latitude,
                                                            double longitude,
                                                            bool auxiliary,
                                                            const GeoConverter& converter,
                                                            TerrainCoordinateMode coordinateMode = TerrainCoordinateMode::Geographic)
{
    TerrainProfileVertex vertex;
    vertex.latitude = latitude;
    vertex.longitude = longitude;
    vertex.auxiliary = auxiliary;
    if (profile.useLocalCoordinates)
    {
        vertex.localPosition = coordinateMode == TerrainCoordinateMode::LocalMeters ?
                                   glm::dvec3(latitude, 0.0, longitude) :
                                   converter.ToLocal(latitude, longitude, 0.0);
    }
    return vertex;
}

inline const char* WorkspaceSectionLabel(WorkspaceSection section)
{
    switch (section)
    {
    case WorkspaceSection::World: return "World";
    case WorkspaceSection::Terrain: return "Terrain";
    case WorkspaceSection::Profiles: return "Profiles";
    case WorkspaceSection::Assets: return "Assets";
    case WorkspaceSection::Lighting: return "Lighting";
    case WorkspaceSection::Diagnostics: return "Diagnostics";
    }
    return "World";
}

inline const char* WorkspaceSectionHint(WorkspaceSection section)
{
    switch (section)
    {
    case WorkspaceSection::World: return "Project files, save/load, and global camera settings.";
    case WorkspaceSection::Terrain: return "Terrain datasets, overlays, bounds, and metadata.";
    case WorkspaceSection::Profiles: return "Terrain profile drawing, graph editing, and 3D draped lines.";
    case WorkspaceSection::Assets: return "Blender/GLB imports, transforms, selection, and placement.";
    case WorkspaceSection::Lighting: return "Sun position, city presets, month, time, and brightness.";
    case WorkspaceSection::Diagnostics: return "Scene health, counts, warnings, and runtime state.";
    }
    return "";
}

inline void DrawStatusBadge(const char* label, bool active)
{
    const ImVec4 color = active ? ImVec4(0.20f, 0.52f, 0.36f, 1.0f) : ImVec4(0.42f, 0.30f, 0.23f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
    ImGui::Button(label, ImVec2(0.0f, 0.0f));
    ImGui::PopStyleColor(3);
}

inline bool PathExists(const std::string& path)
{
    std::error_code errorCode;
    return !path.empty() && std::filesystem::exists(path, errorCode);
}

inline std::string FileNameFromPath(const std::string& path, const std::string& fallback)
{
    const std::filesystem::path filePath(path);
    return filePath.filename().empty() ? fallback : filePath.filename().string();
}

inline void AppendProfileRibbonSegment(std::vector<glm::vec3>& vertices,
                                       const glm::vec3& start,
                                       const glm::vec3& end,
                                       const glm::vec3& cameraPosition,
                                       float widthMeters)
{
    const glm::vec3 segment = end - start;
    if (glm::length(segment) <= 0.0001f)
    {
        return;
    }

    const glm::vec3 midPoint = (start + end) * 0.5f;
    const glm::vec3 viewDirection = glm::length(cameraPosition - midPoint) > 0.0001f ?
                                        glm::normalize(cameraPosition - midPoint) :
                                        glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 side = glm::cross(glm::normalize(segment), viewDirection);
    if (glm::length(side) <= 0.0001f)
    {
        side = glm::cross(glm::normalize(segment), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    if (glm::length(side) <= 0.0001f)
    {
        side = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    side = glm::normalize(side) * (std::max(widthMeters, 0.05f) * 0.5f);
    const glm::vec3 a = start - side;
    const glm::vec3 b = start + side;
    const glm::vec3 c = end + side;
    const glm::vec3 d = end - side;
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    vertices.push_back(a);
    vertices.push_back(c);
    vertices.push_back(d);
}
} // namespace GeoFPS::ApplicationUIInternal
