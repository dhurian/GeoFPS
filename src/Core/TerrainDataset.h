#pragma once

#include "Mapping/GeoImage.h"
#include "Math/GeoConverter.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Terrain/TerrainImporter.h"
#include "Terrain/TerrainMeshBuilder.h"
#include "Terrain/TerrainProfile.h"

#include <memory>
#include <string>
#include <vector>

namespace GeoFPS
{
// Terrain data model — the datasets and their constituent tiles/chunks/overlays.
// Lifted out of Application.h so a TerrainSystem (and any other code) can hold a
// vector<TerrainDataset> without pulling in the whole Application header.
struct OverlayEntry
{
    std::string name {"Overlay 1"};
    GeoImageDefinition image {};
    Texture texture;
};

struct TerrainDatasetBounds
{
    double minLatitude {0.0};
    double maxLatitude {0.0};
    double minLongitude {0.0};
    double maxLongitude {0.0};
    double minHeight {0.0};
    double maxHeight {0.0};
    bool valid {false};
};

struct TerrainMeshChunk
{
    MeshData meshData;
    std::unique_ptr<Mesh> mesh;
    float minX {0.0f};
    float maxX {0.0f};
    float minY {0.0f};
    float maxY {0.0f};
    float minZ {0.0f};
    float maxZ {0.0f};
};

struct TerrainTile
{
    std::string path;
    int row {0};
    int col {0};
    size_t pointCount {0};
    TerrainDatasetBounds bounds;
    std::vector<TerrainPoint> points;
    TerrainHeightGrid heightGrid;
    MeshData terrainMeshData;
    std::vector<TerrainMeshChunk> chunks;
    bool loaded {false};
    bool meshLoaded {false};
    bool loading {false};
};

struct TerrainDataset
{
    std::string name {"Terrain 1"};
    std::string path {"assets/data/sample_terrain.csv"};
    std::string tileManifestPath;
    bool hasTileManifest {false};
    std::vector<TerrainPoint> points;
    GeoReference geoReference {};
    TerrainBuildSettings settings {};
    TerrainHeightGrid heightGrid;
    MeshData terrainMeshData;
    TerrainDatasetBounds bounds;
    std::vector<TerrainTile> tiles;
    std::vector<TerrainMeshChunk> chunks;
    std::vector<OverlayEntry> overlays;
    std::unique_ptr<Mesh> mesh;
    bool visible {true};
    bool loaded {false};
    int activeOverlayIndex {0};
};
} // namespace GeoFPS
