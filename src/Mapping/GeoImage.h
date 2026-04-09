#pragma once

#include <string>

namespace GeoFPS
{
struct GeoImageCoordinate
{
    double latitude {0.0};
    double longitude {0.0};
};

struct GeoImageDefinition
{
    std::string imagePath;
    GeoImageCoordinate topLeft {};
    GeoImageCoordinate topRight {};
    GeoImageCoordinate bottomLeft {};
    GeoImageCoordinate bottomRight {};
    float opacity {0.85f};
    bool enabled {false};
    bool loaded {false};
};
} // namespace GeoFPS
