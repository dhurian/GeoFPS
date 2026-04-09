# GeoFPS

GeoFPS is a custom C++ starter engine for your geospatial first-person terrain-mapping project.

It is set up for **VS Code** and targets:
- macOS
- Linux
- Windows

The current template includes:
- GLFW window creation
- OpenGL 3.3 rendering
- GLAD loader
- Dear ImGui editor panel
- FPS-style camera controls
- CSV terrain import for `latitude,longitude,height`
- local geospatial conversion scaffold
- starter terrain mesh generation

## Project layout

```text
GeoFPS/
  .vscode/
  assets/
  src/
  CMakeLists.txt
  CMakePresets.json
```

## Requirements

### All platforms
- CMake 3.24+
- Ninja
- Git
- VS Code
- C/C++ compiler with C++20 support

### macOS
- Xcode Command Line Tools

Install quickly:
```bash
xcode-select --install
brew install cmake ninja git
```

### Linux
Ubuntu/Debian example:
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
```

### Windows
Recommended:
- Visual Studio 2022 Build Tools or Visual Studio Community with C++ workload
- CMake
- Ninja
- Git

## Open in VS Code

1. Open the `GeoFPS` folder in VS Code.
2. Install the recommended extensions when prompted.
3. Use the CMake Tools extension, or run the VS Code tasks.

## Build from terminal

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
- `Mouse` look around
- `Shift` move faster
- `Esc` release mouse cursor
- `Tab` capture mouse cursor again

## Terrain CSV format

Place CSV files in `assets/data/`.

Example:
```csv
latitude,longitude,height
48.123400,11.567800,520.4
48.123500,11.567900,521.1
48.123600,11.568000,519.8
```

The engine currently loads `assets/data/sample_terrain.csv` at startup.

## Notes

- Dependencies are pulled with `FetchContent` the first time CMake configures.
- On macOS, Apple deprecates OpenGL, but it still works for starter projects like this.
- The terrain builder is intentionally simple in v0.1 and is meant to be replaced with a stronger interpolation and chunking pipeline next.

## Next recommended steps

1. Add shader-based terrain image draping.
2. Add project JSON save/load.
3. Add image georeference input panel.
4. Replace the simple grid builder with chunked terrain generation.


## Note on GLAD

This project now uses a pre-generated GLAD repository through CMake `FetchContent`, so building does **not** require Python or Jinja2.
