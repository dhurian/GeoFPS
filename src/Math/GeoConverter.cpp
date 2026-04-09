#include "Math/GeoConverter.h"

#include <cmath>

namespace GeoFPS
{
GeoConverter::GeoConverter(const GeoReference& reference) : m_Reference(reference)
{
    const double latRadians = m_Reference.originLatitude * (3.14159265358979323846 / 180.0);
    m_MetersPerDegreeLongitude = 111320.0 * std::cos(latRadians);
}

glm::dvec3 GeoConverter::ToLocal(double latitude, double longitude, double height) const
{
    const double x = (longitude - m_Reference.originLongitude) * m_MetersPerDegreeLongitude;
    const double z = (latitude - m_Reference.originLatitude) * m_MetersPerDegreeLatitude;
    const double y = height - m_Reference.originHeight;
    return {x, y, z};
}
} // namespace GeoFPS
