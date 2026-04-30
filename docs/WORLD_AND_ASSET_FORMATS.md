# GeoFPS World And Asset File Formats

GeoFPS uses text files with INI-like `key=value` fields and explicit block markers.

## `.geofpsworld`

Use `File -> Save World As...` to create a world file and `File -> Open World...` to load it.

Required world content:

- At least one `[terrain] ... [/terrain]` block.
- Each terrain block must include `path` or `tile_manifest`. If both are present, `tile_manifest` is used and `path` remains as a single-CSV fallback.

Common world-level fields:

- `world_name`
- `active_terrain_index`
- `active_asset_index`
- `sun.use_geographic`
- `sun.year`, `sun.month`, `sun.day`
- `sun.local_time_hours`, `sun.utc_offset_hours`
- `sun.illuminance`, `sun.ambient_strength`, `sun.sky_brightness`
- `sun.manual_azimuth_degrees`, `sun.manual_elevation_degrees`

Sky background fields (all optional — defaults shown below apply when absent):

- `sky.enabled` — `1` to render the procedural skybox (default), `0` to fall back to the legacy flat clear color
- `sky.use_sun_color` — `1` (default) derives zenith/horizon colors automatically from the sun simulation; `0` uses the custom colors below
- `sky.zenith_r`, `sky.zenith_g`, `sky.zenith_b` — RGB zenith color used when `sky.use_sun_color=0` (defaults: `0.10 0.20 0.50`)
- `sky.horizon_r`, `sky.horizon_g`, `sky.horizon_b` — RGB horizon color used when `sky.use_sun_color=0` (defaults: `0.50 0.70 0.90`)
- `sky.horizon_sharpness` — gradient exponent controlling how quickly the color transitions from horizon to zenith; higher = sharper horizon band (default: `4.0`)
- `sky.show_sun_disk` — `1` (default) renders a bright disk at the sun position; `0` disables it
- `sky.sun_disk_size` — dot-product threshold that controls the angular radius of the sun disk; larger values produce a bigger disk (default: `0.008`)
- `sky.sun_disk_intensity` — brightness multiplier applied to the sun disk (default: `3.0`)

Old world files without `sky.*` keys load with the defaults above (sky enabled, sun-driven colors, sun disk visible). No migration is required.

Cloud layer fields (all optional — clouds are **disabled** by default):

- `sky.clouds_enabled` — `1` to render animated procedural clouds, `0` (default) to disable
- `sky.cloud_coverage` — `0.0` = completely clear sky, `1.0` = fully overcast (default: `0.45`)
- `sky.cloud_density` — maximum cloud opacity in thick regions, `0.0`–`1.0` (default: `0.85`)
- `sky.cloud_scale` — formation size; smaller values produce larger clouds (default: `1.0`)
- `sky.cloud_speed_x` — wind X component; scrolls the cloud noise domain horizontally (default: `0.012`)
- `sky.cloud_speed_y` — wind Y component / cross-wind (default: `0.005`)
- `sky.cloud_auto_color` — `1` (default) tints clouds warm orange at sunset, white in daylight; `0` uses the fixed color below
- `sky.cloud_color_r`, `sky.cloud_color_g`, `sky.cloud_color_b` — manual cloud RGB when `sky.cloud_auto_color=0` (default: `1.0 1.0 1.0`)

Terrain blocks:

- `name`
- `path`
- `visible`
- `grid_x`, `grid_z`
- `height_scale`
- `smoothing_passes` applies terrain mesh smoothing after sampling the source height grid. Use `0` for raw DEM geometry, `1` for the default, and `2-4` only when the source raster looks noisy or terraced.
- `import_sample_step` keeps every Nth source CSV row during import. Use `1` for full import, or higher values for first-pass large raster work.
- `chunk_resolution` controls terrain render chunk size in vertices. Chunks are drawn by camera range so large live meshes do not have to render as one monolith.
- `tile_manifest=assets/datasets/<name>/terrain_tiles/tile_manifest.json` loads terrain from an offline tiled CSV dataset. The manifest is read first, and visible tile meshes are loaded from `tile_<row>_<col>.csv` files as needed.
- `color_by_height` enables terrain coloring by elevation.
- `auto_height_color_range` uses the loaded terrain height bounds for the color ramp. When disabled, use `height_color_min` and `height_color_max`.
- `low_height_color_r/g/b`, `mid_height_color_r/g/b`, and `high_height_color_r/g/b` define the elevation color ramp.
- `coordinate_mode=geographic` for latitude/longitude/height CSV files.
- `coordinate_mode=local_meters` for local X/Z/height CSV files, where CSV column 1 is world X, column 2 is world Z, and column 3 is height.
- `coordinate_mode=projected` for projected CRS meter CSV files, where CSV column 1 is source CRS X/easting, column 2 is source CRS Y/northing, and column 3 is height.
- `crs=EPSG:4326`, `crs=EPSG:3857`, or `crs=LOCAL_METERS`. EPSG:3857 is converted to WGS84 before terrain, overlay, profile, and mixed-dataset placement.
- `crs_false_easting`, `crs_false_northing` are optional offsets applied before projected CRS conversion.
- `origin_latitude`, `origin_longitude`, `origin_height`
- `active_overlay_index`

When `coordinate_mode` is omitted, GeoFPS uses `geographic` for backward compatibility.

Projected CRS support is currently built in for WGS84 geographic coordinates and Web Mercator. Other projected CRSs should be converted before import or added through a future projection registry.

Overlay blocks must be nested inside a terrain block. If `enabled=1`, `image_path` is required.

Asset blocks:

- `name`
- `path` is required.
- `position_mode=geographic` uses `latitude`, `longitude`, and optional `height`.
- `position_mode=local` uses `position_x`, `position_y`, `position_z`.
- `rotation_z`, `scale_x`, `scale_y`, `scale_z`, `tint_r`, `tint_g`, `tint_b` are optional.

Terrain profile blocks preserve profile display settings, selected terrains, vertices, and local-coordinate mode.

## External Blender Asset Text Files

Use `File -> Import Blender Asset List...`.

Each asset must be in an `[asset] ... [/asset]` block. `path` is required.

Example:

```txt
[asset]
name=Tower 1
path=assets/models/tower.glb
position_mode=geographic
latitude=55.67650000
longitude=12.56900000
height=8.0
rotation_z=0.0
scale_x=1.0
scale_y=1.0
scale_z=1.0
[/asset]
```

## Validation Behavior

- Parser errors include line numbers when the error is tied to a file line.
- Missing required terrain or asset `path` fields are errors.
- Geographic assets without `latitude` or `longitude` are errors.
- Local assets with latitude/longitude/height fields are accepted, but GeoFPS prints a terminal warning because those fields are ignored.
- Local assets without local position fields are accepted and placed at local origin with a warning.
- World readout files are human-readable reports, not machine-readable world files.
