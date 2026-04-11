# Nepal Everest Demo Dataset

This folder is prepared for a free Everest / Khumbu test dataset for `GeoFPS`.

## Area

- North: `28.1200`
- South: `27.8600`
- West: `86.6300`
- East: `87.0300`

## What to download

### Terrain

Download a DEM for the bounding box above from one of these:

- OpenTopography `COP30`
- OpenTopography `NASADEM`
- Copernicus DEM 30 m

The current `GeoFPS` build expects terrain as CSV:

```csv
latitude,longitude,height
28.0000,86.8000,4500.0
```

So after you download a DEM GeoTIFF, convert or export it into point CSV and save it as:

- `terrain/everest_terrain.csv`

### Imagery

Download a clipped image for the same area from:

- Copernicus Browser using `Sentinel-2 L2A`

Save the image as:

- `imagery/everest_overlay.jpg`

## Overlay corners for the image

Use these values in the app:

- Top Left: `28.1200, 86.6300`
- Top Right: `28.1200, 87.0300`
- Bottom Left: `27.8600, 86.6300`
- Bottom Right: `27.8600, 87.0300`

## How to load in GeoFPS

Terrain:

- Terrain CSV: `assets/datasets/nepal_everest_demo/terrain/everest_terrain.csv`

Image:

- Image Path: `assets/datasets/nepal_everest_demo/imagery/everest_overlay.jpg`

## Notes

- The imagery is satellite imagery, not aircraft orthophoto.
- The terrain CSV file is not included here because it must be generated from the DEM you download.
- A starter CSV header is included in `terrain/everest_terrain.csv`.
