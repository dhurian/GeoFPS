#include "Core/TerrainCoordinateHelpers.h"

#include "Math/GeoConverter.h"

namespace GeoFPS
{
glm::dvec3 TerrainCoordinateToLocal(const TerrainDataset& dataset, double latitude, double longitude, double height)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {latitude, height, longitude};
    }

    return GeoConverter(dataset.geoReference).ToLocal(latitude, longitude, height);
}

glm::dvec3 LocalToTerrainCoordinate(const TerrainDataset& dataset, const glm::dvec3& localPosition)
{
    if (dataset.settings.coordinateMode == TerrainCoordinateMode::LocalMeters)
    {
        return {localPosition.x, localPosition.z, localPosition.y};
    }

    return GeoConverter(dataset.geoReference).ToGeographic(localPosition);
}

bool TerrainDatasetContainsCoordinate(const TerrainDataset& dataset, double latitude, double longitude)
{
    if (!dataset.bounds.valid)
    {
        return false;
    }

    return latitude >= dataset.bounds.minLatitude && latitude <= dataset.bounds.maxLatitude &&
           longitude >= dataset.bounds.minLongitude && longitude <= dataset.bounds.maxLongitude;
}

bool TerrainDatasetHasCoverage(const TerrainDataset& dataset)
{
    return dataset.visible && dataset.loaded && dataset.bounds.valid;
}

const char* TerrainCoordinateModeLabel(TerrainCoordinateMode mode)
{
    if (mode == TerrainCoordinateMode::LocalMeters)
    {
        return "Local meters X/Z/height";
    }
    if (mode == TerrainCoordinateMode::Projected)
    {
        return "Projected CRS meters";
    }
    return "Geographic lat/lon/height";
}
} // namespace GeoFPS
