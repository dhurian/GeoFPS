# GeoFPS

GeoFPS is a custom C++ FPS-oriented terrain engine starter with geospatial terrain import.

## Included in this package

This **v0.2 terrain step 2** snapshot already includes:

- cross-platform VS Code + CMake project
- macOS / Windows / Linux CMake presets
- GLFW + GLAD + GLM + Dear ImGui
- FPS camera controls
- terrain CSV loading (`latitude,longitude,height`)
- geospatial local-coordinate conversion
- terrain mesh generation from geographic points
- terrain rendering in OpenGL
- no Python/Jinja2 build dependency for GLAD

## Terrain step 2 overview

The terrain pipeline is:

1. `TerrainImporter::LoadCSV(...)`
2. `GeoConverter::ToLocal(...)`
3. `TerrainMeshBuilder::BuildFromGeographicPoints(...)`
4. `Application::RebuildTerrain()`

### Coordinate convention

- `X = East`
- `Y = Up`
- `Z = North`

`GeoConverter` uses a local geospatial origin stored in `GeoReference` and converts
latitude/longitude/height into local engine-space meters.

## Important files

- `src/Core/Application.cpp`
- `src/Math/GeoConverter.h`
- `src/Math/GeoConverter.cpp`
- `src/Terrain/TerrainImporter.h`
- `src/Terrain/TerrainImporter.cpp`
- `src/Terrain/TerrainMeshBuilder.h`
- `src/Terrain/TerrainMeshBuilder.cpp`
- `assets/data/sample_terrain.csv`

## Build

### macOS
```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
./build/macos-debug/bin/GeoFPS
```

### Linux
```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bin/GeoFPS
```

### Windows
```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\windows-debug\bin\GeoFPS.exe
```

## Controls

- `W A S D` move
- mouse look
- `Esc` releases mouse
- `Tab` recaptures mouse

## Status

This package is the clean baseline for the next milestone:
**image draping / terrain mapping overlay**.
