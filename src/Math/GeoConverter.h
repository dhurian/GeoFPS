#pragma once

#include <glm/glm.hpp>

namespace GeoFPS
{
struct GeoReference
{
    double originLatitude {0.0};
    double originLongitude {0.0};
    double originHeight {0.0};
};

class GeoConverter
{
  public:
    explicit GeoConverter(const GeoReference& reference);
    [[nodiscard]] glm::dvec3 ToLocal(double latitude, double longitude, double height) const;

  private:
    GeoReference m_Reference;
    double m_MetersPerDegreeLatitude {111320.0};
    double m_MetersPerDegreeLongitude {111320.0};
};
} // namespace GeoFPS
