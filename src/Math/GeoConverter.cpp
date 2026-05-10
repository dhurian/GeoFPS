#include "Math/GeoConverter.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace GeoFPS
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kWebMercatorRadiusMeters = 6378137.0;
constexpr double kMaxWebMercatorLatitude = 85.05112878;

std::string NormalizedCrsId(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char character) {
                    return std::isspace(character) != 0;
                }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}
} // namespace

GeoConverter::GeoConverter(const GeoReference& reference) : m_Reference(reference)
{
    const double latRadians = m_Reference.originLatitude * (3.14159265358979323846 / 180.0);
    m_MetersPerDegreeLongitude = 111320.0 * std::cos(latRadians);
}

glm::dvec3 GeoConverter::ToLocal(double latitude, double longitude, double height) const
{
    // Geographic → local-metres convention:
    //   +X = east   (longitude increases east)
    //   +Y = up     (height increases up)
    //   −Z = north  (latitude increases north → −Z direction)
    //
    // We deliberately map *increasing latitude to decreasing Z* (note the unary
    // minus below).  Without it, looking straight down from the standard
    // top-view camera (yaw=−90, pitch=−89, right-handed view matrix) would put
    // SOUTH at the top of the screen — because cross(right, forward) with
    // right=+X and forward≈−Y forces view-up to be −Z.  Flipping the sign here
    // means −Z = north, so screen-up = view-up = −Z = north — matching the
    // top-down convention used by the 2D minimap and profile-maker map (both
    // of which already paint north at the top).  Everything downstream
    // (terrain mesh builder, asset placement, isoline grid, profile lines,
    // camera/HUD readout) goes through this single conversion, so the whole
    // world rotates coherently.
    const double x = (longitude - m_Reference.originLongitude) * m_MetersPerDegreeLongitude;
    const double z = -(latitude - m_Reference.originLatitude) * m_MetersPerDegreeLatitude;
    const double y = height - m_Reference.originHeight;
    return {x, y, z};
}

glm::dvec3 GeoConverter::ToGeographic(const glm::dvec3& localPosition) const
{
    // Inverse of ToLocal — note the matching minus on the latitude axis so a
    // round-trip ToLocal → ToGeographic yields the original lat/lon/height.
    const double longitude = m_Reference.originLongitude + (localPosition.x / m_MetersPerDegreeLongitude);
    const double latitude = m_Reference.originLatitude - (localPosition.z / m_MetersPerDegreeLatitude);
    const double height = m_Reference.originHeight + localPosition.y;
    return {latitude, longitude, height};
}

CrsMetadata GeoConverter::ParseCrs(std::string crsId)
{
    CrsMetadata metadata;
    if (crsId.empty())
    {
        return metadata;
    }

    metadata.id = crsId;
    const std::string normalized = NormalizedCrsId(crsId);
    if (normalized == "EPSG:3857" || normalized == "EPSG:900913" || normalized == "WEBMERCATOR")
    {
        metadata.kind = CrsKind::WebMercator;
        metadata.id = "EPSG:3857";
    }
    else if (normalized == "LOCAL" || normalized == "LOCAL_METERS" || normalized == "PROJECTED_LOCAL")
    {
        metadata.kind = CrsKind::LocalMeters;
        metadata.id = "LOCAL_METERS";
    }
    else
    {
        metadata.kind = CrsKind::GeographicWgs84;
        metadata.id = "EPSG:4326";
    }
    return metadata;
}

glm::dvec3 GeoConverter::SourceToGeographic(const glm::dvec3& sourcePosition, const CrsMetadata& crs)
{
    if (crs.kind == CrsKind::WebMercator)
    {
        const double x = sourcePosition.x - crs.falseEasting;
        const double y = sourcePosition.y - crs.falseNorthing;
        const double longitude = (x / kWebMercatorRadiusMeters) * (180.0 / kPi);
        const double latitude = (2.0 * std::atan(std::exp(y / kWebMercatorRadiusMeters)) - (kPi * 0.5)) * (180.0 / kPi);
        return {std::clamp(latitude, -kMaxWebMercatorLatitude, kMaxWebMercatorLatitude), longitude, sourcePosition.z};
    }

    return sourcePosition;
}

glm::dvec3 GeoConverter::GeographicToSource(const glm::dvec3& geographicPosition, const CrsMetadata& crs)
{
    if (crs.kind == CrsKind::WebMercator)
    {
        const double latitude = std::clamp(geographicPosition.x, -kMaxWebMercatorLatitude, kMaxWebMercatorLatitude);
        const double longitude = geographicPosition.y;
        const double x = kWebMercatorRadiusMeters * longitude * (kPi / 180.0) + crs.falseEasting;
        const double y = kWebMercatorRadiusMeters *
                         std::log(std::tan((kPi * 0.25) + (latitude * (kPi / 180.0) * 0.5))) +
                         crs.falseNorthing;
        return {x, y, geographicPosition.z};
    }

    return geographicPosition;
}
} // namespace GeoFPS
