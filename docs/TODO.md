# GeoFPS TODO

This file tracks the next product and engineering work for GeoFPS.

## Priority 7 - Terrain And Geospatial Accuracy

Status: concluded for visual re-evaluation after the latest terrain and mouse-control stabilization pass.

- [x] Support local-meter terrain CSVs via `coordinate_mode=local_meters` for terrain mesh generation, profile sampling/draping, minimap camera movement, overlays, and world save/load.
- [x] Add first-pass render performance fixes for large terrain plus GLB scenes: cached shader uniforms, released GLB CPU texture buffers after GPU upload, one-pass rendering for single terrain overlays, reduced asset texture state churn, and persistent profile line GPU buffers.
- [x] Improve terrain surface smoothness by sampling mesh heights from the source height grid with bilinear interpolation, adding bounded terrain quality presets up to `512 x 512`, and adding controlled smoothing passes to reduce raster terracing without letting live meshes become too heavy.
- [x] Fix geographic terrain height origin handling so DEM elevations render as local relief instead of absolute altitude.
- [x] Smooth terrain lighting normals from the sampled height grid to reduce diagonal triangle artifacts on rugged DEMs.
- [x] Add raw mouse motion and frame-aware mouse-delta smoothing to reduce left/right camera choppiness.

## Re-Evaluation Queue

- [ ] Re-test Everest terrain without aerial overlay and classify remaining artifacts as geometry resolution, normal/lighting, depth precision, or DEM-source artifacts.
- [ ] Re-test Everest terrain with aerial overlay after the base terrain pass and verify the imagery no longer hides or exaggerates geometry problems.
- [ ] Re-test mouse look after raw motion and smoothing, with special attention to horizontal yaw choppiness versus vertical pitch smoothness.
- [ ] Capture before/after screenshots or short notes for terrain and mouse behavior so the next implementation pass has concrete evidence.

## Future Geospatial Work

- [x] Add first-pass projected CRS support with CRS metadata, built-in WGS84/EPSG:4326 and Web Mercator/EPSG:3857 transforms, local/projected meter metadata, and mixed-dataset conversion through shared geographic/local coordinates.
- [x] Improve handling of large raster-derived terrain CSV files with import decimation controls, chunk mesh generation, and camera-range chunk drawing.
- [x] Add offline terrain CSV tiling, generated tile manifests, world-file `tile_manifest` support, and runtime loading/rendering of visible tiled terrain chunks.
- [x] Add an initial renderer backend interface scaffold so OpenGL can become the reference backend before Metal is implemented.
- [ ] Extend projected CRS support beyond built-in EPSG:3857 with a real projection/datum library or a broader EPSG transform registry.
- [ ] Replace camera-range chunk drawing with true terrain LOD, async tile streaming, residency limits, and screen-error-based refinement for very large DEMs.
- [ ] Move current OpenGL terrain, asset, overlay, and line draw paths fully behind the renderer backend interface before adding Metal terrain rendering.

## Manual Validation

- [ ] Test GLB import with real Blender tower exports, including scale, position, rotation, and material appearance.
- [ ] Run a hands-on world workflow pass: load terrain, add overlay, import assets, save world, reload world, and verify the restored scene.
- [ ] Verify minimap marker visibility and click-to-move behavior across multiple loaded terrains.
- [ ] Verify sun illumination changes visually affect terrain, aerial overlays, and imported assets in a representative scene.
