# GeoFPS Terrain And Aerial Data Pipeline

This process turns real-world elevation and aerial/satellite imagery into the files GeoFPS already knows how to load:

- `terrain/<name>_terrain.csv` with `latitude,longitude,height`
- `imagery/<image>.jpg` or `.png`
- `config/overlay_reference.json` with the image corners
- `assets/worlds/<name>.geofpsworld` ready for File -> Load World File

## 1. Pick A Bounding Box

Use WGS84 latitude/longitude bounds:

```json
"bbox": {
  "south": 27.86,
  "north": 28.12,
  "west": 86.63,
  "east": 87.03
}
```

Keep first datasets small. A 0.25 x 0.40 degree area at 30 m resolution is already enough for a useful FPS test map.

## 2. Get Terrain

Recommended free DEM sources:

- OpenTopography Global DEM API: https://opentopography.org/developers
- OpenTopography browser: https://opentopography.org/start
- Copernicus DEM documentation: https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM.html

The prep script supports these terrain source modes:

- `opentopography`: downloads an ESRI ASCII DEM from OpenTopography. Set `OPENTOPOGRAPHY_API_KEY` or pass `--opentopography-key`.
- `local_ascii`: uses a local ESRI ASCII `.asc` DEM.
- `local_geotiff`: uses a local GeoTIFF and converts it with `gdal_translate`.

## 3. Get Aerial Or Satellite Imagery

Recommended free imagery source:

- Copernicus Browser: https://dataspace.copernicus.eu/browser/
- Sentinel-2 L2A docs: https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/S2L2A.html

In Copernicus Browser, use the same bounding box, choose Sentinel-2 L2A, pick a low-cloud true-color scene, and export a clipped image. Put that file path in the manifest under `imagery.path`.

GeoFPS currently stores the image path and four image-corner coordinates. The prep script writes those corners from the bounding box:

- top left: north, west
- top right: north, east
- bottom left: south, west
- bottom right: south, east

## 4. Create A Manifest

Copy the example:

```bash
cp RawData/geofps_dataset_manifest.example.json RawData/my_area_manifest.json
```

Edit:

- `name`
- `display_name`
- `bbox`
- `terrain.source`
- `terrain.path` or `terrain.demtype`
- `imagery.path`
- `output.dataset_dir`
- `output.world_path`

## 5. Run The Prep Script

Using a local ESRI ASCII DEM:

```bash
python3 RawData/prepare_geofps_dataset.py RawData/my_area_manifest.json --force
```

Using OpenTopography:

```bash
export OPENTOPOGRAPHY_API_KEY="your-key"
python3 RawData/prepare_geofps_dataset.py RawData/my_area_manifest.json --force
```

The script writes:

- `assets/datasets/<name>/terrain/<name>_terrain.csv`
- `assets/datasets/<name>/imagery/<source-image-name>`
- `assets/datasets/<name>/config/bbox.json`
- `assets/datasets/<name>/config/bbox.geojson`
- `assets/datasets/<name>/config/bbox.wkt`
- `assets/datasets/<name>/config/overlay_reference.json`
- `assets/worlds/<name>.geofpsworld`

## 6. Load In GeoFPS

Build and run:

```bash
cmake --build --preset macos-debug
./build/macos-debug/bin/GeoFPS
```

Then use File -> Load World File and choose the generated `.geofpsworld`.

## Notes

- `terrain.sample_step` reduces DEM density before GeoFPS builds its mesh. Start with `4` or `8`, then lower it if the terrain needs more detail.
- `output.grid_x` and `output.grid_z` control the in-engine terrain mesh resolution. Start at `128 x 128`.
- Sentinel-2 is satellite imagery, not aircraft orthophoto. For true aerial orthophoto, use a local orthophoto export and set `imagery.path` to that file.
