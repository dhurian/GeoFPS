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

bool TerrainDatasetContainsCoordinate(const TerrainDataset& dataset, double latitude, double longitude)
{
    if (!dataset.bounds.valid)
    {
        return false;
    }

    return latitude >= dataset.bounds.minLatitude && latitude <= dataset.bounds.maxLatitude &&
           longitude >= dataset.bounds.minLongitude && longitude <= dataset.bounds.maxLongitude;
}
} // namespace GeoFPS
