#pragma once

#include <glm/glm.hpp>

namespace GeoFPS
{
[[nodiscard]] glm::vec3 MakeCameraRelative(const glm::dvec3& worldPosition,
                                           const glm::dvec3& renderOrigin);
[[nodiscard]] glm::vec3 MakeCameraRelative(const glm::vec3& worldPosition,
                                           const glm::dvec3& renderOrigin);
[[nodiscard]] double EstimateFloatSpacing(double magnitude);
} // namespace GeoFPS
