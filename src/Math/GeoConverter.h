#pragma once

#include <glm/glm.hpp>
#include <string>

namespace GeoFPS
{
enum class CrsKind
{
    GeographicWgs84,
    WebMercator,
    LocalMeters
};

struct CrsMetadata
{
    std::string id {"EPSG:4326"};
    CrsKind kind {CrsKind::GeographicWgs84};
    double falseEasting {0.0};
    double falseNorthing {0.0};
};

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
    [[nodiscard]] glm::dvec3 ToGeographic(const glm::dvec3& localPosition) const;
    [[nodiscard]] const GeoReference& GetReference() const { return m_Reference; }

    [[nodiscard]] static CrsMetadata ParseCrs(std::string crsId);
    [[nodiscard]] static glm::dvec3 SourceToGeographic(const glm::dvec3& sourcePosition, const CrsMetadata& crs);
    [[nodiscard]] static glm::dvec3 GeographicToSource(const glm::dvec3& geographicPosition, const CrsMetadata& crs);

  private:
    GeoReference m_Reference;
    double m_MetersPerDegreeLatitude {111320.0};
    double m_MetersPerDegreeLongitude {111320.0};
};
} // namespace GeoFPS
