#include "Terrain/TerrainProfile.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace GeoFPS
{
#if defined(__APPLE__)
bool GenerateTerrainIsolinesMetal(const TerrainHeightGrid& heightGrid,
                                  const TerrainIsolineSettings& settings,
                                  std::vector<TerrainIsolineSegment>& segments);
bool GenerateTerrainIsolinesMetal(const TerrainIsolineSampleGrid& sampleGrid,
                                  const TerrainIsolineSettings& settings,
                                  std::vector<TerrainIsolineSegment>& segments);
#endif
namespace
{
constexpr double kCoordinateEpsilon = 1e-10;

std::string Trim(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool ParseBool(const std::string& value)
{
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

double ParseDouble(const std::string& value, double fallback = 0.0)
{
    try
    {
        return std::stod(value);
    }
    catch (...)
    {
        return fallback;
    }
}

std::vector<double> UniqueSortedCoordinates(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [](double a, double b) {
                     return std::abs(a - b) <= kCoordinateEpsilon;
                 }),
                 values.end());
    return values;
}

size_t NearestCoordinateIndex(const std::vector<double>& coordinates, double value)
{
    auto upper = std::lower_bound(coordinates.begin(), coordinates.end(), value);
    if (upper == coordinates.begin())
    {
        return 0;
    }
    if (upper == coordinates.end())
    {
        return coordinates.size() - 1;
    }

    const size_t upperIndex = static_cast<size_t>(std::distance(coordinates.begin(), upper));
    const size_t lowerIndex = upperIndex - 1;
    return std::abs(coordinates[lowerIndex] - value) <= std::abs(coordinates[upperIndex] - value) ? lowerIndex : upperIndex;
}

std::pair<size_t, size_t> BracketingIndices(const std::vector<double>& coordinates, double value)
{
    const double clampedValue = std::clamp(value, coordinates.front(), coordinates.back());
    auto upper = std::lower_bound(coordinates.begin(), coordinates.end(), clampedValue);
    if (upper == coordinates.begin())
    {
        return {0, 0};
    }
    if (upper == coordinates.end())
    {
        const size_t lastIndex = coordinates.size() - 1;
        return {lastIndex, lastIndex};
    }

    const size_t upperIndex = static_cast<size_t>(std::distance(coordinates.begin(), upper));
    if (std::abs(coordinates[upperIndex] - clampedValue) <= kCoordinateEpsilon)
    {
        return {upperIndex, upperIndex};
    }

    return {upperIndex - 1, upperIndex};
}

double InterpolationT(double lower, double upper, double value)
{
    const double span = upper - lower;
    if (std::abs(span) <= kCoordinateEpsilon)
    {
        return 0.0;
    }
    return std::clamp((value - lower) / span, 0.0, 1.0);
}

TerrainProfileVertex VertexAsSampleCoordinates(const GeoConverter& converter,
                                               const TerrainProfileVertex& vertex,
                                               bool useLocalCoordinates,
                                               TerrainCoordinateMode coordinateMode)
{
    if (!useLocalCoordinates)
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

glm::dvec3 GroundPosition(const GeoConverter& converter,
                          const TerrainProfileVertex& vertex,
                          bool useLocalCoordinates,
                          TerrainCoordinateMode coordinateMode)
{
    if (useLocalCoordinates)
    {
        return {vertex.localPosition.x, 0.0, vertex.localPosition.z};
    }

    if (coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {vertex.latitude, 0.0, vertex.longitude};
    }

    return converter.ToLocal(vertex.latitude, vertex.longitude, 0.0);
}

glm::dvec3 SampleLocalPosition(const GeoConverter& converter,
                               double latitude,
                               double longitude,
                               double height,
                               TerrainCoordinateMode coordinateMode)
{
    if (coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {latitude, height, longitude};
    }

    return converter.ToLocal(latitude, longitude, height);
}

} // namespace

bool TerrainHeightGrid::Build(const std::vector<TerrainPoint>& points)
{
    m_Latitudes.clear();
    m_Longitudes.clear();
    m_Cells.clear();
    m_GlobalAverageHeight = 0.0;

    if (points.empty())
    {
        return false;
    }

    m_Latitudes.reserve(points.size());
    m_Longitudes.reserve(points.size());
    for (const TerrainPoint& point : points)
    {
        m_Latitudes.push_back(point.latitude);
        m_Longitudes.push_back(point.longitude);
    }

    m_Latitudes = UniqueSortedCoordinates(std::move(m_Latitudes));
    m_Longitudes = UniqueSortedCoordinates(std::move(m_Longitudes));
    if (m_Latitudes.empty() || m_Longitudes.empty())
    {
        return false;
    }

    m_Cells.assign(m_Latitudes.size() * m_Longitudes.size(), HeightCell {});
    double totalHeight = 0.0;
    int totalCount = 0;
    for (const TerrainPoint& point : points)
    {
        const size_t latitudeIndex = NearestCoordinateIndex(m_Latitudes, point.latitude);
        const size_t longitudeIndex = NearestCoordinateIndex(m_Longitudes, point.longitude);
        HeightCell& cell = m_Cells[CellIndex(longitudeIndex, latitudeIndex)];
        cell.totalHeight += point.height;
        cell.count += 1;
        totalHeight += point.height;
        totalCount += 1;
    }
    m_GlobalAverageHeight = totalCount > 0 ? totalHeight / static_cast<double>(totalCount) : 0.0;

    return true;
}

bool TerrainIsolineSampleGrid::IsValid() const
{
    return resolutionX >= 2 && resolutionZ >= 2 && heights.size() == static_cast<size_t>(resolutionX * resolutionZ);
}

bool TerrainHeightGrid::IsValid() const
{
    return !m_Latitudes.empty() && !m_Longitudes.empty() && m_Cells.size() == m_Latitudes.size() * m_Longitudes.size();
}

bool TerrainHeightGrid::Contains(double latitude, double longitude) const
{
    return IsValid() && latitude >= m_Latitudes.front() && latitude <= m_Latitudes.back() &&
           longitude >= m_Longitudes.front() && longitude <= m_Longitudes.back();
}

size_t TerrainHeightGrid::CellIndex(size_t longitudeIndex, size_t latitudeIndex) const
{
    return latitudeIndex * m_Longitudes.size() + longitudeIndex;
}

double TerrainHeightGrid::ResolveCellHeight(size_t longitudeIndex, size_t latitudeIndex) const
{
    const HeightCell& directCell = m_Cells[CellIndex(longitudeIndex, latitudeIndex)];
    if (directCell.count > 0)
    {
        return directCell.totalHeight / static_cast<double>(directCell.count);
    }

    const size_t maxRadius = std::max(m_Latitudes.size(), m_Longitudes.size());
    for (size_t radius = 1; radius < maxRadius; ++radius)
    {
        double weightedHeight = 0.0;
        double totalWeight = 0.0;
        const size_t minLatitude = latitudeIndex > radius ? latitudeIndex - radius : 0;
        const size_t maxLatitude = std::min(latitudeIndex + radius, m_Latitudes.size() - 1);
        const size_t minLongitude = longitudeIndex > radius ? longitudeIndex - radius : 0;
        const size_t maxLongitude = std::min(longitudeIndex + radius, m_Longitudes.size() - 1);

        for (size_t lat = minLatitude; lat <= maxLatitude; ++lat)
        {
            for (size_t lon = minLongitude; lon <= maxLongitude; ++lon)
            {
                const HeightCell& cell = m_Cells[CellIndex(lon, lat)];
                if (cell.count == 0)
                {
                    continue;
                }

                const double dx = static_cast<double>(lon) - static_cast<double>(longitudeIndex);
                const double dy = static_cast<double>(lat) - static_cast<double>(latitudeIndex);
                const double distanceSquared = std::max((dx * dx) + (dy * dy), 1.0);
                const double weight = 1.0 / distanceSquared;
                weightedHeight += (cell.totalHeight / static_cast<double>(cell.count)) * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight > 0.0)
        {
            return weightedHeight / totalWeight;
        }
    }

    return m_GlobalAverageHeight;
}

double TerrainHeightGrid::SampleHeight(double latitude, double longitude) const
{
    if (!IsValid())
    {
        return 0.0;
    }

    const double clampedLatitude = std::clamp(latitude, m_Latitudes.front(), m_Latitudes.back());
    const double clampedLongitude = std::clamp(longitude, m_Longitudes.front(), m_Longitudes.back());
    const auto [lat0, lat1] = BracketingIndices(m_Latitudes, clampedLatitude);
    const auto [lon0, lon1] = BracketingIndices(m_Longitudes, clampedLongitude);

    const double h00 = ResolveCellHeight(lon0, lat0);
    const double h10 = ResolveCellHeight(lon1, lat0);
    const double h01 = ResolveCellHeight(lon0, lat1);
    const double h11 = ResolveCellHeight(lon1, lat1);
    const double tx = InterpolationT(m_Longitudes[lon0], m_Longitudes[lon1], clampedLongitude);
    const double ty = InterpolationT(m_Latitudes[lat0], m_Latitudes[lat1], clampedLatitude);

    const double lower = h00 + ((h10 - h00) * tx);
    const double upper = h01 + ((h11 - h01) * tx);
    return lower + ((upper - lower) * ty);
}

double TerrainHeightGrid::MinHeight() const
{
    if (!IsValid())
    {
        return 0.0;
    }

    double minHeight = std::numeric_limits<double>::max();
    for (size_t latitudeIndex = 0; latitudeIndex < m_Latitudes.size(); ++latitudeIndex)
    {
        for (size_t longitudeIndex = 0; longitudeIndex < m_Longitudes.size(); ++longitudeIndex)
        {
            minHeight = std::min(minHeight, ResolveCellHeight(longitudeIndex, latitudeIndex));
        }
    }
    return minHeight == std::numeric_limits<double>::max() ? 0.0 : minHeight;
}

double TerrainHeightGrid::MaxHeight() const
{
    if (!IsValid())
    {
        return 0.0;
    }

    double maxHeight = std::numeric_limits<double>::lowest();
    for (size_t latitudeIndex = 0; latitudeIndex < m_Latitudes.size(); ++latitudeIndex)
    {
        for (size_t longitudeIndex = 0; longitudeIndex < m_Longitudes.size(); ++longitudeIndex)
        {
            maxHeight = std::max(maxHeight, ResolveCellHeight(longitudeIndex, latitudeIndex));
        }
    }
    return maxHeight == std::numeric_limits<double>::lowest() ? 0.0 : maxHeight;
}

double TerrainProfileLineAngleDegrees(const TerrainProfileVertex& start,
                                      const TerrainProfileVertex& end,
                                      const GeoConverter& converter,
                                      bool useLocalCoordinates,
                                      TerrainCoordinateMode coordinateMode)
{
    const glm::dvec3 startLocal = GroundPosition(converter, start, useLocalCoordinates, coordinateMode);
    const glm::dvec3 endLocal = GroundPosition(converter, end, useLocalCoordinates, coordinateMode);
    const double dx = endLocal.x - startLocal.x;
    const double dz = endLocal.z - startLocal.z;
    if (std::abs(dx) <= kCoordinateEpsilon && std::abs(dz) <= kCoordinateEpsilon)
    {
        return 0.0;
    }

    double degrees = std::atan2(dz, dx) * (180.0 / 3.14159265358979323846);
    if (degrees < 0.0)
    {
        degrees += 360.0;
    }
    return degrees;
}

std::vector<TerrainProfileSample> SampleTerrainProfile(const std::vector<TerrainProfileVertex>& vertices,
                                                       const TerrainHeightGrid& heightGrid,
                                                       const GeoConverter& converter,
                                                       double sampleSpacingMeters,
                                                       bool useLocalCoordinates,
                                                       TerrainCoordinateMode coordinateMode)
{
    std::vector<TerrainProfileSample> samples;
    if (vertices.size() < 2 || !heightGrid.IsValid())
    {
        return samples;
    }

    sampleSpacingMeters = std::max(sampleSpacingMeters, 0.1);
    double accumulatedDistance = 0.0;

    for (size_t vertexIndex = 0; vertexIndex + 1 < vertices.size(); ++vertexIndex)
    {
        const TerrainProfileVertex& start = vertices[vertexIndex];
        const TerrainProfileVertex& end = vertices[vertexIndex + 1];
        const glm::dvec3 startLocal = GroundPosition(converter, start, useLocalCoordinates, coordinateMode);
        const glm::dvec3 endLocal = GroundPosition(converter, end, useLocalCoordinates, coordinateMode);
        const TerrainProfileVertex startGeo = VertexAsSampleCoordinates(converter, start, useLocalCoordinates, coordinateMode);
        const TerrainProfileVertex endGeo = VertexAsSampleCoordinates(converter, end, useLocalCoordinates, coordinateMode);
        const double segmentLength = glm::length(glm::dvec2(endLocal.x - startLocal.x, endLocal.z - startLocal.z));
        const double lineAngleDegrees = TerrainProfileLineAngleDegrees(start, end, converter, useLocalCoordinates, coordinateMode);
        const int stepCount = std::max(1, static_cast<int>(std::ceil(segmentLength / sampleSpacingMeters)));
        const int firstStep = samples.empty() ? 0 : 1;

        for (int step = firstStep; step <= stepCount; ++step)
        {
            const double t = static_cast<double>(step) / static_cast<double>(stepCount);
            const double latitude = startGeo.latitude + ((endGeo.latitude - startGeo.latitude) * t);
            const double longitude = startGeo.longitude + ((endGeo.longitude - startGeo.longitude) * t);
            const bool valid = heightGrid.Contains(latitude, longitude);
            const double height = valid ? heightGrid.SampleHeight(latitude, longitude) : 0.0;
            TerrainProfileSample sample;
            sample.distanceMeters = accumulatedDistance + (segmentLength * t);
            sample.latitude = latitude;
            sample.longitude = longitude;
            sample.height = height;
            sample.lineAngleDegrees = lineAngleDegrees;
            sample.localPosition = SampleLocalPosition(converter, latitude, longitude, height, coordinateMode);
            sample.valid = valid;
            samples.push_back(sample);
        }

        accumulatedDistance += segmentLength;
    }

    return samples;
}

glm::vec4 IsolineColorForHeight(double height, double minHeight, double maxHeight, float opacity)
{
    const double span = std::max(maxHeight - minHeight, 1.0);
    const float t = static_cast<float>(std::clamp((height - minHeight) / span, 0.0, 1.0));
    glm::vec3 low(0.10f, 0.45f, 0.85f);
    glm::vec3 mid(0.95f, 0.82f, 0.18f);
    glm::vec3 high(0.95f, 0.18f, 0.12f);
    glm::vec3 color = t < 0.5f ? glm::mix(low, mid, t * 2.0f) : glm::mix(mid, high, (t - 0.5f) * 2.0f);
    return glm::vec4(color, std::clamp(opacity, 0.0f, 1.0f));
}

double ResolveContourInterval(double minHeight, double maxHeight, const TerrainIsolineSettings& settings)
{
    if (!settings.autoInterval && settings.contourIntervalMeters > 0.0)
    {
        return settings.contourIntervalMeters;
    }

    const double range = std::max(maxHeight - minHeight, 1.0);
    const double rawInterval = range / 12.0;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rawInterval)));
    const double normalized = rawInterval / magnitude;
    if (normalized <= 2.0)
    {
        return 2.0 * magnitude;
    }
    if (normalized <= 5.0)
    {
        return 5.0 * magnitude;
    }
    return 10.0 * magnitude;
}

TerrainIsolineSampleGrid BuildTerrainIsolineSampleGrid(const TerrainHeightGrid& heightGrid,
                                                       const TerrainIsolineSettings& settings)
{
    TerrainIsolineSampleGrid sampleGrid;
    if (!heightGrid.IsValid() || settings.resolutionX < 2 || settings.resolutionZ < 2)
    {
        return sampleGrid;
    }

    sampleGrid.resolutionX = std::clamp(settings.resolutionX, 2, 512);
    sampleGrid.resolutionZ = std::clamp(settings.resolutionZ, 2, 512);
    sampleGrid.minLatitude = heightGrid.MinLatitude();
    sampleGrid.maxLatitude = heightGrid.MaxLatitude();
    sampleGrid.minLongitude = heightGrid.MinLongitude();
    sampleGrid.maxLongitude = heightGrid.MaxLongitude();
    sampleGrid.heights.assign(static_cast<size_t>(sampleGrid.resolutionX * sampleGrid.resolutionZ), 0.0f);
    sampleGrid.minHeight = std::numeric_limits<double>::max();
    sampleGrid.maxHeight = std::numeric_limits<double>::lowest();

    for (int z = 0; z < sampleGrid.resolutionZ; ++z)
    {
        const double v = static_cast<double>(z) / static_cast<double>(sampleGrid.resolutionZ - 1);
        const double latitude = sampleGrid.minLatitude + ((sampleGrid.maxLatitude - sampleGrid.minLatitude) * v);
        for (int x = 0; x < sampleGrid.resolutionX; ++x)
        {
            const double u = static_cast<double>(x) / static_cast<double>(sampleGrid.resolutionX - 1);
            const double longitude = sampleGrid.minLongitude + ((sampleGrid.maxLongitude - sampleGrid.minLongitude) * u);
            const double height = heightGrid.SampleHeight(latitude, longitude);
            sampleGrid.heights[static_cast<size_t>(z * sampleGrid.resolutionX + x)] = static_cast<float>(height);
            sampleGrid.minHeight = std::min(sampleGrid.minHeight, height);
            sampleGrid.maxHeight = std::max(sampleGrid.maxHeight, height);
        }
    }

    if (sampleGrid.minHeight == std::numeric_limits<double>::max())
    {
        sampleGrid.minHeight = 0.0;
        sampleGrid.maxHeight = 0.0;
    }

    return sampleGrid;
}

std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesFromSampleGrid(const TerrainIsolineSampleGrid& sampleGrid,
                                                                         const TerrainIsolineSettings& settings)
{
    std::vector<TerrainIsolineSegment> segments;
    if (!sampleGrid.IsValid())
    {
        return segments;
    }

    const double interval = ResolveContourInterval(sampleGrid.minHeight, sampleGrid.maxHeight, settings);
    if (interval <= 0.0)
    {
        return segments;
    }

    const int resolutionX = sampleGrid.resolutionX;
    const int resolutionZ = sampleGrid.resolutionZ;
    const double firstLevel = std::ceil(sampleGrid.minHeight / interval) * interval;
    constexpr size_t kMaxIsolineSegments = 20000;

    const auto vertexAt = [](const TerrainProfileVertex& a,
                             const TerrainProfileVertex& b,
                             double heightA,
                             double heightB,
                             double level) {
        const double denominator = heightB - heightA;
        const double t = std::abs(denominator) <= kCoordinateEpsilon ? 0.5 : std::clamp((level - heightA) / denominator, 0.0, 1.0);
        return TerrainProfileVertex {a.latitude + ((b.latitude - a.latitude) * t),
                                     a.longitude + ((b.longitude - a.longitude) * t),
                                     false};
    };

    for (double level = firstLevel; level <= sampleGrid.maxHeight + kCoordinateEpsilon; level += interval)
    {
        if (level <= sampleGrid.minHeight + kCoordinateEpsilon || level >= sampleGrid.maxHeight - kCoordinateEpsilon)
        {
            continue;
        }

        const glm::vec4 color = IsolineColorForHeight(level, sampleGrid.minHeight, sampleGrid.maxHeight, settings.opacity);
        for (int z = 0; z < resolutionZ - 1; ++z)
        {
            const double z0 = static_cast<double>(z) / static_cast<double>(resolutionZ - 1);
            const double z1 = static_cast<double>(z + 1) / static_cast<double>(resolutionZ - 1);
            const double lat0 = sampleGrid.minLatitude + ((sampleGrid.maxLatitude - sampleGrid.minLatitude) * z0);
            const double lat1 = sampleGrid.minLatitude + ((sampleGrid.maxLatitude - sampleGrid.minLatitude) * z1);
            for (int x = 0; x < resolutionX - 1; ++x)
            {
                const double x0 = static_cast<double>(x) / static_cast<double>(resolutionX - 1);
                const double x1 = static_cast<double>(x + 1) / static_cast<double>(resolutionX - 1);
                const double lon0 = sampleGrid.minLongitude + ((sampleGrid.maxLongitude - sampleGrid.minLongitude) * x0);
                const double lon1 = sampleGrid.minLongitude + ((sampleGrid.maxLongitude - sampleGrid.minLongitude) * x1);

                const TerrainProfileVertex bottomLeft {lat0, lon0};
                const TerrainProfileVertex bottomRight {lat0, lon1};
                const TerrainProfileVertex topRight {lat1, lon1};
                const TerrainProfileVertex topLeft {lat1, lon0};
                const double hBottomLeft = sampleGrid.heights[static_cast<size_t>(z * resolutionX + x)];
                const double hBottomRight = sampleGrid.heights[static_cast<size_t>(z * resolutionX + (x + 1))];
                const double hTopRight = sampleGrid.heights[static_cast<size_t>((z + 1) * resolutionX + (x + 1))];
                const double hTopLeft = sampleGrid.heights[static_cast<size_t>((z + 1) * resolutionX + x)];

                std::vector<TerrainProfileVertex> crossings;
                crossings.reserve(4);
                const auto addCrossing = [&](const TerrainProfileVertex& a, const TerrainProfileVertex& b, double heightA, double heightB) {
                    const bool crosses = (heightA < level && heightB >= level) || (heightB < level && heightA >= level);
                    if (crosses && std::abs(heightA - heightB) > kCoordinateEpsilon)
                    {
                        crossings.push_back(vertexAt(a, b, heightA, heightB, level));
                    }
                };

                addCrossing(bottomLeft, bottomRight, hBottomLeft, hBottomRight);
                addCrossing(bottomRight, topRight, hBottomRight, hTopRight);
                addCrossing(topRight, topLeft, hTopRight, hTopLeft);
                addCrossing(topLeft, bottomLeft, hTopLeft, hBottomLeft);

                if (crossings.size() == 2)
                {
                    segments.push_back({level, crossings[0], crossings[1], color});
                }
                else if (crossings.size() == 4)
                {
                    segments.push_back({level, crossings[0], crossings[1], color});
                    segments.push_back({level, crossings[2], crossings[3], color});
                }
                if (segments.size() >= kMaxIsolineSegments)
                {
                    return segments;
                }
            }
        }
    }

    return segments;
}

std::vector<TerrainIsolineSegment> GenerateTerrainIsolines(const TerrainHeightGrid& heightGrid,
                                                           const TerrainIsolineSettings& settings)
{
    return GenerateTerrainIsolinesFromSampleGrid(BuildTerrainIsolineSampleGrid(heightGrid, settings), settings);
}

std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesAccelerated(const TerrainHeightGrid& heightGrid,
                                                                      const TerrainIsolineSettings& settings,
                                                                      bool* usedGpu)
{
    TerrainIsolineSampleGrid sampleGrid = BuildTerrainIsolineSampleGrid(heightGrid, settings);
    return GenerateTerrainIsolinesAccelerated(sampleGrid, settings, true, usedGpu);
}

std::vector<TerrainIsolineSegment> GenerateTerrainIsolinesAccelerated(const TerrainIsolineSampleGrid& sampleGrid,
                                                                      const TerrainIsolineSettings& settings,
                                                                      bool useGpu,
                                                                      bool* usedGpu)
{
    if (usedGpu != nullptr)
    {
        *usedGpu = false;
    }

#if defined(__APPLE__)
    std::vector<TerrainIsolineSegment> gpuSegments;
    if (useGpu && GenerateTerrainIsolinesMetal(sampleGrid, settings, gpuSegments))
    {
        if (usedGpu != nullptr)
        {
            *usedGpu = true;
        }
        return gpuSegments;
    }
#endif

    return GenerateTerrainIsolinesFromSampleGrid(sampleGrid, settings);
}

bool ExportTerrainProfiles(const std::string& path, const std::vector<TerrainProfile>& profiles)
{
    if (path.empty())
    {
        return false;
    }

    std::error_code errorCode;
    const std::filesystem::path targetPath(path);
    if (targetPath.has_parent_path())
    {
        std::filesystem::create_directories(targetPath.parent_path(), errorCode);
    }

    std::ofstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    file << "# GeoFPS terrain profile file\n";
    file << std::fixed << std::setprecision(8);
    for (const TerrainProfile& profile : profiles)
    {
        file << "[terrain_profile]\n";
        file << "name=" << profile.name << '\n';
        file << "visible=" << (profile.visible ? 1 : 0) << '\n';
        file << "show_in_world=" << (profile.showInWorld ? 1 : 0) << '\n';
        file << "color_r=" << profile.color.r << '\n';
        file << "color_g=" << profile.color.g << '\n';
        file << "color_b=" << profile.color.b << '\n';
        file << "color_a=" << profile.color.a << '\n';
        file << "thickness=" << profile.thickness << '\n';
        file << "world_thickness_m=" << profile.worldThicknessMeters << '\n';
        file << "world_ground_offset_m=" << profile.worldGroundOffsetMeters << '\n';
        file << "sample_spacing_m=" << profile.sampleSpacingMeters << '\n';
        file << "use_local_coordinates=" << (profile.useLocalCoordinates ? 1 : 0) << '\n';
        for (const std::string& terrainName : profile.includedTerrainNames)
        {
            file << "terrain=" << terrainName << '\n';
        }

        for (const TerrainProfileVertex& vertex : profile.vertices)
        {
            file << "  [vertex]\n";
            file << "  latitude=" << vertex.latitude << '\n';
            file << "  longitude=" << vertex.longitude << '\n';
            file << "  auxiliary=" << (vertex.auxiliary ? 1 : 0) << '\n';
            file << "  local_x=" << vertex.localPosition.x << '\n';
            file << "  local_y=" << vertex.localPosition.y << '\n';
            file << "  local_z=" << vertex.localPosition.z << '\n';
            file << "  [/vertex]\n";
        }

        file << std::setprecision(3);
        for (const TerrainProfileSample& sample : profile.samples)
        {
            file << "  [sample]\n";
            file << "  distance_m=" << sample.distanceMeters << '\n';
            file << "  x=" << sample.localPosition.x << '\n';
            file << "  y=" << sample.localPosition.y << '\n';
            file << "  z=" << sample.localPosition.z << '\n';
            file << std::setprecision(8);
            file << "  latitude=" << sample.latitude << '\n';
            file << "  longitude=" << sample.longitude << '\n';
            file << std::setprecision(3);
            file << "  height=" << sample.height << '\n';
            file << "  line_angle_degrees=" << sample.lineAngleDegrees << '\n';
            file << "  valid=" << (sample.valid ? 1 : 0) << '\n';
            file << "  [/sample]\n";
        }
        file << std::setprecision(8);
        file << "[/terrain_profile]\n\n";
    }

    return true;
}

bool ImportTerrainProfiles(const std::string& path, std::vector<TerrainProfile>& profiles, std::string& errorMessage)
{
    profiles.clear();
    errorMessage.clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        errorMessage = "Could not open profile file: " + path;
        return false;
    }

    TerrainProfile* currentProfile = nullptr;
    TerrainProfileVertex currentVertex;
    bool insideVertex = false;
    std::string line;

    while (std::getline(file, line))
    {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        if (trimmed == "[terrain_profile]")
        {
            profiles.emplace_back();
            currentProfile = &profiles.back();
            insideVertex = false;
            continue;
        }
        if (trimmed == "[/terrain_profile]")
        {
            currentProfile = nullptr;
            insideVertex = false;
            continue;
        }
        if (trimmed == "[vertex]" && currentProfile != nullptr)
        {
            currentVertex = TerrainProfileVertex {};
            insideVertex = true;
            continue;
        }
        if (trimmed == "[/vertex]" && currentProfile != nullptr && insideVertex)
        {
            currentProfile->vertices.push_back(currentVertex);
            insideVertex = false;
            continue;
        }
        if (trimmed == "[sample]")
        {
            continue;
        }
        if (trimmed == "[/sample]")
        {
            continue;
        }

        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos || currentProfile == nullptr)
        {
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));
        if (insideVertex)
        {
            if (key == "latitude") currentVertex.latitude = ParseDouble(value);
            else if (key == "longitude") currentVertex.longitude = ParseDouble(value);
            else if (key == "auxiliary") currentVertex.auxiliary = ParseBool(value);
            else if (key == "local_x") currentVertex.localPosition.x = ParseDouble(value);
            else if (key == "local_y") currentVertex.localPosition.y = ParseDouble(value);
            else if (key == "local_z") currentVertex.localPosition.z = ParseDouble(value);
            continue;
        }

        if (key == "name") currentProfile->name = value;
        else if (key == "visible") currentProfile->visible = ParseBool(value);
        else if (key == "show_in_world") currentProfile->showInWorld = ParseBool(value);
        else if (key == "use_local_coordinates") currentProfile->useLocalCoordinates = ParseBool(value);
        else if (key == "terrain") currentProfile->includedTerrainNames.push_back(value);
        else if (key == "color_r") currentProfile->color.r = static_cast<float>(ParseDouble(value, currentProfile->color.r));
        else if (key == "color_g") currentProfile->color.g = static_cast<float>(ParseDouble(value, currentProfile->color.g));
        else if (key == "color_b") currentProfile->color.b = static_cast<float>(ParseDouble(value, currentProfile->color.b));
        else if (key == "color_a") currentProfile->color.a = static_cast<float>(ParseDouble(value, currentProfile->color.a));
        else if (key == "thickness") currentProfile->thickness = static_cast<float>(ParseDouble(value, currentProfile->thickness));
        else if (key == "world_thickness_m")
            currentProfile->worldThicknessMeters = static_cast<float>(ParseDouble(value, currentProfile->worldThicknessMeters));
        else if (key == "world_ground_offset_m")
            currentProfile->worldGroundOffsetMeters = static_cast<float>(ParseDouble(value, currentProfile->worldGroundOffsetMeters));
        else if (key == "sample_spacing_m")
            currentProfile->sampleSpacingMeters = static_cast<float>(ParseDouble(value, currentProfile->sampleSpacingMeters));
    }

    if (profiles.empty())
    {
        errorMessage = "Profile file contained no terrain profiles.";
        return false;
    }

    return true;
}

} // namespace GeoFPS
