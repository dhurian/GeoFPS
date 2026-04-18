#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from typing import Any

from convert_esri_ascii_to_terrain_csv import convert_ascii_to_csv


OPENTOPOGRAPHY_GLOBALDEM_URL = "https://portal.opentopography.org/API/globaldem"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare a GeoFPS terrain + aerial overlay dataset from a manifest."
    )
    parser.add_argument("manifest", type=Path, help="Dataset manifest JSON")
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path.cwd(),
        help="Repository root used to resolve relative paths. Default: current directory",
    )
    parser.add_argument(
        "--opentopography-key",
        default=os.environ.get("OPENTOPOGRAPHY_API_KEY", ""),
        help="OpenTopography API key. Defaults to OPENTOPOGRAPHY_API_KEY.",
    )
    parser.add_argument(
        "--skip-download",
        action="store_true",
        help="Do not download remote data; use local files declared in the manifest.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite generated dataset outputs.",
    )
    return parser.parse_args()


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        manifest = json.load(stream)
    require_keys(manifest, ["name", "bbox", "terrain", "imagery", "output"], "manifest")
    require_keys(manifest["bbox"], ["south", "north", "west", "east"], "bbox")
    return manifest


def require_keys(data: dict[str, Any], keys: list[str], label: str) -> None:
    missing = [key for key in keys if key not in data]
    if missing:
        raise ValueError(f"{label} missing required key(s): {', '.join(missing)}")


def resolve_path(repo_root: Path, raw_path: str | None) -> Path | None:
    if not raw_path:
        return None
    path = Path(raw_path)
    if path.is_absolute():
        return path
    return repo_root / path


def repo_relative(repo_root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def validate_bbox(bbox: dict[str, Any]) -> None:
    south = float(bbox["south"])
    north = float(bbox["north"])
    west = float(bbox["west"])
    east = float(bbox["east"])
    if not (-90.0 <= south < north <= 90.0):
        raise ValueError("bbox latitude must satisfy -90 <= south < north <= 90.")
    if not (-180.0 <= west < east <= 180.0):
        raise ValueError("bbox longitude must satisfy -180 <= west < east <= 180.")


def download_opentopography_ascii(
    bbox: dict[str, Any],
    demtype: str,
    api_key: str,
    raw_dir: Path,
    force: bool,
) -> Path:
    if not api_key:
        raise ValueError(
            "OpenTopography terrain download requires --opentopography-key "
            "or OPENTOPOGRAPHY_API_KEY."
        )

    raw_dir.mkdir(parents=True, exist_ok=True)
    ascii_path = raw_dir / f"{demtype.lower()}_elevation.asc"
    if ascii_path.exists() and not force:
        return ascii_path

    params = {
        "demtype": demtype,
        "south": str(bbox["south"]),
        "north": str(bbox["north"]),
        "west": str(bbox["west"]),
        "east": str(bbox["east"]),
        "outputFormat": "AAIGrid",
        "API_Key": api_key,
    }
    request_url = f"{OPENTOPOGRAPHY_GLOBALDEM_URL}?{urllib.parse.urlencode(params)}"
    download_path = raw_dir / f"{demtype.lower()}_elevation_download"
    print(f"Downloading DEM from OpenTopography: {demtype}")
    urllib.request.urlretrieve(request_url, download_path)

    data = download_path.read_bytes()
    if data.startswith(b"PK"):
        with zipfile.ZipFile(download_path) as archive:
            asc_names = [name for name in archive.namelist() if name.lower().endswith(".asc")]
            if not asc_names:
                raise ValueError("OpenTopography response zip did not contain an .asc file.")
            with archive.open(asc_names[0]) as source, ascii_path.open("wb") as destination:
                shutil.copyfileobj(source, destination)
    else:
        text = data[:512].decode("utf-8", errors="ignore").lower()
        if "ncols" not in text or "nrows" not in text:
            raise ValueError(
                "OpenTopography response did not look like ESRI ASCII. "
                "Check the API key, bbox, demtype, and service response."
            )
        ascii_path.write_bytes(data)

    download_path.unlink(missing_ok=True)
    return ascii_path


def geotiff_to_ascii(geotiff_path: Path, raw_dir: Path, force: bool) -> Path:
    ascii_path = raw_dir / f"{geotiff_path.stem}.asc"
    if ascii_path.exists() and not force:
        return ascii_path

    if shutil.which("gdal_translate") is None:
        raise ValueError(
            "GeoTIFF input requires gdal_translate on PATH. Install GDAL or export the DEM as ESRI ASCII."
        )

    raw_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["gdal_translate", "-of", "AAIGrid", str(geotiff_path), str(ascii_path)],
        check=True,
    )
    return ascii_path


def terrain_ascii_from_manifest(
    repo_root: Path,
    manifest: dict[str, Any],
    raw_dir: Path,
    api_key: str,
    skip_download: bool,
    force: bool,
) -> Path:
    terrain = manifest["terrain"]
    source = terrain.get("source", "local_ascii")
    if source == "opentopography":
        if skip_download:
            local_path = resolve_path(repo_root, terrain.get("path"))
            if local_path is None:
                raise ValueError("skip-download needs terrain.path for opentopography source.")
            return local_path
        return download_opentopography_ascii(
            manifest["bbox"],
            terrain.get("demtype", "COP30"),
            api_key,
            raw_dir,
            force,
        )
    if source == "local_ascii":
        local_path = resolve_path(repo_root, terrain.get("path"))
        if local_path is None:
            raise ValueError("terrain.path is required for local_ascii source.")
        return local_path
    if source == "local_geotiff":
        local_path = resolve_path(repo_root, terrain.get("path"))
        if local_path is None:
            raise ValueError("terrain.path is required for local_geotiff source.")
        return geotiff_to_ascii(local_path, raw_dir, force)
    raise ValueError(f"Unsupported terrain.source: {source}")


def copy_imagery(repo_root: Path, manifest: dict[str, Any], imagery_dir: Path, force: bool) -> Path | None:
    imagery = manifest["imagery"]
    source_path = resolve_path(repo_root, imagery.get("path"))
    if source_path is None:
        return None
    if not source_path.exists():
        raise FileNotFoundError(f"Imagery file not found: {source_path}")

    imagery_dir.mkdir(parents=True, exist_ok=True)
    destination = imagery_dir / source_path.name
    if source_path.resolve() == destination.resolve():
        return destination
    if destination.exists() and not force:
        return destination
    shutil.copy2(source_path, destination)
    return destination


def read_first_terrain_point(csv_path: Path) -> tuple[float, float, float]:
    with csv_path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        for row in reader:
            return float(row["latitude"]), float(row["longitude"]), float(row["height"])
    raise ValueError(f"Terrain CSV has no points: {csv_path}")


def write_bbox_files(config_dir: Path, bbox: dict[str, Any]) -> None:
    config_dir.mkdir(parents=True, exist_ok=True)
    bbox_json_path = config_dir / "bbox.json"
    bbox_geojson_path = config_dir / "bbox.geojson"
    bbox_wkt_path = config_dir / "bbox.wkt"

    south = float(bbox["south"])
    north = float(bbox["north"])
    west = float(bbox["west"])
    east = float(bbox["east"])
    coordinates = [[west, south], [east, south], [east, north], [west, north], [west, south]]

    bbox_json_path.write_text(json.dumps(bbox, indent=2) + "\n", encoding="utf-8")
    bbox_geojson_path.write_text(
        json.dumps(
            {
                "type": "FeatureCollection",
                "features": [
                    {
                        "type": "Feature",
                        "properties": {"name": "GeoFPS dataset bbox"},
                        "geometry": {"type": "Polygon", "coordinates": [coordinates]},
                    }
                ],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    bbox_wkt_path.write_text(
        "POLYGON((" + ", ".join(f"{lon} {lat}" for lon, lat in coordinates) + "))\n",
        encoding="utf-8",
    )


def write_overlay_reference(
    config_dir: Path,
    bbox: dict[str, Any],
    image_path: str | None,
    enabled: bool,
    opacity: float,
) -> None:
    reference = {
        "image_path": image_path or "",
        "enabled": enabled,
        "opacity": opacity,
        "top_left": {"latitude": bbox["north"], "longitude": bbox["west"]},
        "top_right": {"latitude": bbox["north"], "longitude": bbox["east"]},
        "bottom_left": {"latitude": bbox["south"], "longitude": bbox["west"]},
        "bottom_right": {"latitude": bbox["south"], "longitude": bbox["east"]},
    }
    (config_dir / "overlay_reference.json").write_text(
        json.dumps(reference, indent=2) + "\n",
        encoding="utf-8",
    )


def write_world_file(
    repo_root: Path,
    manifest: dict[str, Any],
    terrain_csv_path: Path,
    imagery_path: Path | None,
    origin: tuple[float, float, float],
) -> Path:
    output = manifest["output"]
    bbox = manifest["bbox"]
    imagery = manifest["imagery"]
    world_path = resolve_path(repo_root, output.get("world_path"))
    if world_path is None:
        world_path = resolve_path(repo_root, output["dataset_dir"]) / f"{manifest['name']}.geofpsworld"

    world_path.parent.mkdir(parents=True, exist_ok=True)
    terrain_rel = repo_relative(repo_root, terrain_csv_path)
    imagery_rel = repo_relative(repo_root, imagery_path) if imagery_path is not None else ""
    enabled = 1 if imagery.get("enabled", imagery_path is not None) and imagery_path is not None else 0
    opacity = float(imagery.get("opacity", 0.85))
    origin_lat, origin_lon, origin_height = origin

    content = f"""# GeoFPS generated world file
# Generated by RawData/prepare_geofps_dataset.py.

world_name={manifest.get("display_name", manifest["name"])}
active_terrain_index=0
active_asset_index=0

sun.use_geographic=1
sun.year=2026
sun.month=4
sun.day=18
sun.local_time_hours=12.0
sun.utc_offset_hours=2.0
sun.illuminance=1.0
sun.ambient_strength=0.22
sun.sky_brightness=1.0
sun.manual_azimuth_degrees=220.0
sun.manual_elevation_degrees=32.0

[terrain]
name={manifest.get("display_name", manifest["name"])}
path={terrain_rel}
grid_x={int(output.get("grid_x", 128))}
grid_z={int(output.get("grid_z", 128))}
height_scale={float(output.get("height_scale", 1.0))}
origin_latitude={origin_lat:.8f}
origin_longitude={origin_lon:.8f}
origin_height={origin_height:.3f}
active_overlay_index=0
  [overlay]
  name={manifest.get("display_name", manifest["name"])} Overlay
  image_path={imagery_rel}
  enabled={enabled}
  opacity={opacity:.3f}
  top_left_latitude={float(bbox["north"]):.8f}
  top_left_longitude={float(bbox["west"]):.8f}
  top_right_latitude={float(bbox["north"]):.8f}
  top_right_longitude={float(bbox["east"]):.8f}
  bottom_left_latitude={float(bbox["south"]):.8f}
  bottom_left_longitude={float(bbox["west"]):.8f}
  bottom_right_latitude={float(bbox["south"]):.8f}
  bottom_right_longitude={float(bbox["east"]):.8f}
  [/overlay]
[/terrain]
"""
    world_path.write_text(content, encoding="utf-8")
    return world_path


def write_dataset_readme(
    dataset_dir: Path,
    manifest: dict[str, Any],
    terrain_csv_path: Path,
    imagery_path: Path | None,
    world_path: Path,
    repo_root: Path,
) -> None:
    bbox = manifest["bbox"]
    lines = [
        f"# {manifest.get('display_name', manifest['name'])}",
        "",
        "Generated GeoFPS dataset.",
        "",
        "## Bounds",
        "",
        f"- North: `{float(bbox['north']):.8f}`",
        f"- South: `{float(bbox['south']):.8f}`",
        f"- West: `{float(bbox['west']):.8f}`",
        f"- East: `{float(bbox['east']):.8f}`",
        "",
        "## GeoFPS files",
        "",
        f"- Terrain CSV: `{repo_relative(repo_root, terrain_csv_path)}`",
        f"- World file: `{repo_relative(repo_root, world_path)}`",
    ]
    if imagery_path is not None:
        lines.append(f"- Aerial overlay image: `{repo_relative(repo_root, imagery_path)}`")
    else:
        lines.append("- Aerial overlay image: not provided")
    lines.extend(
        [
            "",
            "Load the world file in GeoFPS with File -> Load World File.",
            "",
            "## Overlay corners",
            "",
            f"- Top Left: `{float(bbox['north']):.8f}, {float(bbox['west']):.8f}`",
            f"- Top Right: `{float(bbox['north']):.8f}, {float(bbox['east']):.8f}`",
            f"- Bottom Left: `{float(bbox['south']):.8f}, {float(bbox['west']):.8f}`",
            f"- Bottom Right: `{float(bbox['south']):.8f}, {float(bbox['east']):.8f}`",
            "",
        ]
    )
    (dataset_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")


def write_sources(dataset_dir: Path, manifest: dict[str, Any]) -> None:
    terrain = manifest["terrain"]
    imagery = manifest["imagery"]
    content = f"""# Data Sources

## Terrain

- Source mode: `{terrain.get("source", "local_ascii")}`
- DEM type: `{terrain.get("demtype", "")}`
- Input path: `{terrain.get("path", "")}`
- OpenTopography Global DEM API: https://opentopography.org/developers

## Imagery

- Source mode: `{imagery.get("source", "")}`
- Input path: `{imagery.get("path", "")}`
- Copernicus Sentinel-2 L2A docs: https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/S2L2A.html
"""
    (dataset_dir / "SOURCES.md").write_text(content, encoding="utf-8")


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    manifest = load_manifest(args.manifest)
    validate_bbox(manifest["bbox"])

    dataset_dir = resolve_path(repo_root, manifest["output"]["dataset_dir"])
    if dataset_dir is None:
        raise ValueError("output.dataset_dir is required.")
    terrain_dir = dataset_dir / "terrain"
    imagery_dir = dataset_dir / "imagery"
    config_dir = dataset_dir / "config"
    raw_dir = dataset_dir / "raw"
    terrain_dir.mkdir(parents=True, exist_ok=True)

    ascii_path = terrain_ascii_from_manifest(
        repo_root,
        manifest,
        raw_dir,
        args.opentopography_key,
        args.skip_download,
        args.force,
    )
    if not ascii_path.exists():
        raise FileNotFoundError(f"Terrain source not found: {ascii_path}")

    terrain_csv_path = terrain_dir / f"{manifest['name']}_terrain.csv"
    sample_step = int(manifest["terrain"].get("sample_step", 1))
    written_rows, _ = convert_ascii_to_csv(ascii_path, terrain_csv_path, sample_step)
    if written_rows == 0:
        raise ValueError("Terrain conversion wrote zero points.")

    imagery_path = copy_imagery(repo_root, manifest, imagery_dir, args.force)
    origin = read_first_terrain_point(terrain_csv_path)

    write_bbox_files(config_dir, manifest["bbox"])
    write_overlay_reference(
        config_dir,
        manifest["bbox"],
        repo_relative(repo_root, imagery_path) if imagery_path is not None else None,
        bool(manifest["imagery"].get("enabled", imagery_path is not None)),
        float(manifest["imagery"].get("opacity", 0.85)),
    )
    world_path = write_world_file(repo_root, manifest, terrain_csv_path, imagery_path, origin)
    write_dataset_readme(dataset_dir, manifest, terrain_csv_path, imagery_path, world_path, repo_root)
    write_sources(dataset_dir, manifest)

    print(f"Wrote {written_rows} terrain points: {repo_relative(repo_root, terrain_csv_path)}")
    if imagery_path is not None:
        print(f"Prepared overlay image: {repo_relative(repo_root, imagery_path)}")
    print(f"Wrote world file: {repo_relative(repo_root, world_path)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
