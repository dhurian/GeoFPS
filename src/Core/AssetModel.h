#pragma once

#include "Assets/AnimationData.h"
#include "Assets/GltfImporter.h"

#include <glm/glm.hpp>

#include <string>

namespace GeoFPS
{
// Imported-asset data model — one placed glTF/OBJ instance and the clipboard
// entry used to copy/paste it.  Lifted out of Application.h so an AssetSystem
// can hold a vector<ImportedAsset> without pulling in the whole Application
// header.
struct ImportedAsset
{
    std::string name {"Asset 1"};
    std::string path;
    ImportedAssetData assetData;
    bool selected {false};
    bool useGeographicPlacement {false};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 rotationDegrees {0.0f, 0.0f, 0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 tint {1.0f, 1.0f, 1.0f};
    bool showLabel {true};
    bool loaded {false};
    AnimationState     animState     {};
    NodeAnimationState nodeAnimState {};
    // Axis-aligned bounding box in object-local space (computed at load time, not persisted).
    glm::vec3 aabbMin {0.0f};
    glm::vec3 aabbMax {0.0f};
    bool      aabbValid {false};
};

struct AssetClipboardEntry
{
    std::string name;
    std::string path;
    bool useGeographicPlacement {false};
    double latitude {0.0};
    double longitude {0.0};
    double height {0.0};
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    float rotationZDegrees {0.0f};
    glm::vec3 scale {1.0f, 1.0f, 1.0f};
    glm::vec3 tint {1.0f, 1.0f, 1.0f};
    bool showLabel {true};
};
} // namespace GeoFPS
