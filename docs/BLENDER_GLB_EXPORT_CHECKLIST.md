# Blender GLB Export Checklist For GeoFPS

Use `.glb` as the default export format. It keeps mesh geometry and material texture references together better than `.obj/.mtl`.

Before export:

- Set object scale with `Ctrl+A -> Apply -> Scale`.
- Put the asset origin at a meaningful base point, usually the tower footing or center foundation point.
- Use meters in Blender so `1 Blender unit = 1 meter`.
- Name important objects clearly; GeoFPS shows the imported asset name as an optional world label.
- Prefer simple PBR materials with base color textures for the current renderer.
- Keep tower assets reasonably optimized; use repeated parts in Blender before export, but export final geometry as GLB.

Export settings:

- File format: `glTF 2.0`.
- Format: `GLB`.
- Include: selected objects if exporting one asset.
- Transform: `+Y Up` can stay at Blender default unless the asset appears rotated; correct in GeoFPS with `Rotation Z` if needed.
- Materials: export materials and images.

GeoFPS placement:

- Use `position_mode=geographic` for real-world placement with `latitude`, `longitude`, and `height`.
- Use `position_mode=local` for quick layout with `position_x`, `position_y`, and `position_z`.
- Use `Snap Selected To Terrain` after import to place selected assets on the active terrain surface.
