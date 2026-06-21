#pragma once

#include "Core/TerrainDataset.h"

#include <glm/glm.hpp>

namespace GeoFPS
{
// Per-dataset coordinate helpers shared by the app/scene/UI code, which all
// need the same math.  Previously each translation unit carried its own
// identical anonymous-namespace copy.

// Convert a terrain-native coordinate to scene-local space.  For a local-meters
// dataset the inputs are already metric (x = latitude arg, z = longitude arg,
// y = height); otherwise they are geographic and run through the dataset's
// GeoReference.
glm::dvec3 TerrainCoordinateToLocal(const TerrainDataset& dataset, double latitude, double longitude, double height);

// Inverse of TerrainCoordinateToLocal: scene-local space back to the dataset's
// native coordinate (geographic lat/lon/height, or x/z/y for local-meters).
glm::dvec3 LocalToTerrainCoordinate(const TerrainDataset& dataset, const glm::dvec3& localPosition);

// True when (latitude, longitude) falls inside the dataset's valid bounds.
bool TerrainDatasetContainsCoordinate(const TerrainDataset& dataset, double latitude, double longitude);

// True when the dataset is visible, loaded, and has valid bounds — i.e. it
// contributes terrain to the scene/profile sampling right now.
bool TerrainDatasetHasCoverage(const TerrainDataset& dataset);
} // namespace GeoFPS
