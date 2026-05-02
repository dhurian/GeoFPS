#include "Renderer/RenderOrigin.h"

#include <cmath>
#include <limits>

namespace GeoFPS
{
glm::vec3 MakeCameraRelative(const glm::dvec3& worldPosition, const glm::dvec3& renderOrigin)
{
    const glm::dvec3 relative = worldPosition - renderOrigin;
    return {static_cast<float>(relative.x),
            static_cast<float>(relative.y),
            static_cast<float>(relative.z)};
}

glm::vec3 MakeCameraRelative(const glm::vec3& worldPosition, const glm::dvec3& renderOrigin)
{
    return MakeCameraRelative(glm::dvec3(worldPosition), renderOrigin);
}

double EstimateFloatSpacing(double magnitude)
{
    const float value = static_cast<float>(std::abs(magnitude));
    if (value <= 0.0f || !std::isfinite(value))
    {
        return 0.0;
    }

    const float next = std::nextafter(value, std::numeric_limits<float>::infinity());
    return static_cast<double>(next - value);
}
} // namespace GeoFPS
