#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
from collections import defaultdict
from pathlib import Path
from statistics import median
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Split a GeoFPS latitude,longitude,height terrain CSV into spatial tiles."
    )
    parser.add_argument("input_csv", type=Path, help="Input CSV with latitude,longitude,height columns.")
    parser.add_argument("output_dir", type=Path, help="Output terrain_tiles directory.")
    parser.add_argument("--name", default="", help="Dataset name for tile_manifest.json.")
    parser.add_argument("--tile-size-degrees", type=float, default=0.01)
    parser.add_argument("--tile-overlap-samples", type=int, default=1)
    parser.add_argument("--coordinate-mode", default="geographic")
    parser.add_argument("--crs", default="EPSG:4326")
    parser.add_argument("--origin-latitude", type=float, default=None)
    parser.add_argument("--origin-longitude", type=float, default=None)
    parser.add_argument("--origin-height", type=float, default=None)
    parser.add_argument("--force", action="store_true", help="Replace an existing output directory.")
    return parser.parse_args()


def _positive_step(values: list[float]) -> float:
    unique_values = sorted(set(values))
    deltas = [
        unique_values[index] - unique_values[index - 1]
        for index in range(1, len(unique_values))
        if unique_values[index] > unique_values[index - 1]
    ]
    return median(deltas) if deltas else 0.0


def _bounds_from_rows(rows: list[dict[str, float]]) -> dict[str, float]:
    return {
        "min_latitude": min(row["latitude"] for row in rows),
        "max_latitude": max(row["latitude"] for row in rows),
        "min_longitude": min(row["longitude"] for row in rows),
        "max_longitude": max(row["longitude"] for row in rows),
        "min_height": min(row["height"] for row in rows),
        "max_height": max(row["height"] for row in rows),
    }


def _read_rows(input_csv: Path) -> list[dict[str, float]]:
    rows: list[dict[str, float]] = []
    with input_csv.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        required = {"latitude", "longitude", "height"}
        if reader.fieldnames is None or not required.issubset(set(reader.fieldnames)):
            raise ValueError("input CSV must contain latitude, longitude, and height columns.")
        for row in reader:
            rows.append(
                {
                    "latitude": float(row["latitude"]),
                    "longitude": float(row["longitude"]),
                    "height": float(row["height"]),
                }
            )
    if not rows:
        raise ValueError(f"input CSV has no terrain rows: {input_csv}")
    return rows


def split_terrain_csv_to_tiles(
    input_csv: Path,
    output_dir: Path,
    *,
    name: str = "",
    tile_size_degrees: float = 0.01,
    tile_overlap_samples: int = 1,
    coordinate_mode: str = "geographic",
    crs: str = "EPSG:4326",
    origin_latitude: float | None = None,
    origin_longitude: float | None = None,
    origin_height: float | None = None,
    force: bool = False,
) -> Path:
    if tile_size_degrees <= 0.0:
        raise ValueError("tile_size_degrees must be positive.")
    if tile_overlap_samples < 0:
        raise ValueError("tile_overlap_samples must be non-negative.")
    if output_dir.exists():
        if not force:
            manifest_path = output_dir / "tile_manifest.json"
            if manifest_path.exists():
                return manifest_path
            raise FileExistsError(f"output directory exists without manifest: {output_dir}")
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    rows = _read_rows(input_csv)
    source_bounds = _bounds_from_rows(rows)
    origin_lat = origin_latitude if origin_latitude is not None else rows[0]["latitude"]
    origin_lon = origin_longitude if origin_longitude is not None else rows[0]["longitude"]
    origin_h = origin_height if origin_height is not None else rows[0]["height"]
    lat_step = _positive_step([row["latitude"] for row in rows])
    lon_step = _positive_step([row["longitude"] for row in rows])
    lat_overlap = lat_step * tile_overlap_samples
    lon_overlap = lon_step * tile_overlap_samples
    min_lat = source_bounds["min_latitude"]
    min_lon = source_bounds["min_longitude"]
    row_count = max(1, math.floor((source_bounds["max_latitude"] - min_lat) / tile_size_degrees) + 1)
    col_count = max(1, math.floor((source_bounds["max_longitude"] - min_lon) / tile_size_degrees) + 1)

    tile_rows: dict[tuple[int, int], list[dict[str, float]]] = defaultdict(list)
    for row in rows:
        base_row = min(
            max(int(math.floor((row["latitude"] - min_lat) / tile_size_degrees)), 0),
            row_count - 1,
        )
        base_col = min(
            max(int(math.floor((row["longitude"] - min_lon) / tile_size_degrees)), 0),
            col_count - 1,
        )
        for tile_row in range(max(0, base_row - 1), min(row_count, base_row + 2)):
            tile_min_lat = min_lat + tile_row * tile_size_degrees - lat_overlap
            tile_max_lat = min_lat + (tile_row + 1) * tile_size_degrees + lat_overlap
            if not (tile_min_lat <= row["latitude"] <= tile_max_lat):
                continue
            for tile_col in range(max(0, base_col - 1), min(col_count, base_col + 2)):
                tile_min_lon = min_lon + tile_col * tile_size_degrees - lon_overlap
                tile_max_lon = min_lon + (tile_col + 1) * tile_size_degrees + lon_overlap
                if tile_min_lon <= row["longitude"] <= tile_max_lon:
                    tile_rows[(tile_row, tile_col)].append(row)

    tiles: list[dict[str, Any]] = []
    for (tile_row, tile_col), points in sorted(tile_rows.items()):
        if not points:
            continue
        filename = f"tile_{tile_row:04d}_{tile_col:04d}.csv"
        tile_path = output_dir / filename
        with tile_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["latitude", "longitude", "height"])
            writer.writeheader()
            writer.writerows(points)

        tile_bounds = _bounds_from_rows(points)
        tiles.append(
            {
                "path": filename,
                "row": tile_row,
                "col": tile_col,
                **tile_bounds,
                "point_count": len(points),
            }
        )

    manifest = {
        "name": name or input_csv.stem,
        "coordinate_mode": coordinate_mode,
        "crs": crs,
        "origin_latitude": origin_lat,
        "origin_longitude": origin_lon,
        "origin_height": origin_h,
        "tile_size_degrees": tile_size_degrees,
        "tile_overlap_samples": tile_overlap_samples,
        **source_bounds,
        "point_count": len(rows),
        "tiles": tiles,
    }
    manifest_path = output_dir / "tile_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def main() -> int:
    args = parse_args()
    manifest_path = split_terrain_csv_to_tiles(
        args.input_csv,
        args.output_dir,
        name=args.name,
        tile_size_degrees=args.tile_size_degrees,
        tile_overlap_samples=args.tile_overlap_samples,
        coordinate_mode=args.coordinate_mode,
        crs=args.crs,
        origin_latitude=args.origin_latitude,
        origin_longitude=args.origin_longitude,
        origin_height=args.origin_height,
        force=args.force,
    )
    print(f"Wrote tile manifest: {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
