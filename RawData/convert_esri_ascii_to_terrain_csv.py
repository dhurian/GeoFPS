#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an ESRI ASCII raster grid into GeoFPS terrain CSV format."
    )
    parser.add_argument("input", type=Path, help="Input .asc raster file")
    parser.add_argument("output", type=Path, help="Output CSV file")
    parser.add_argument(
        "--sample-step",
        type=int,
        default=1,
        help="Write every Nth row/column to reduce CSV size. Default: 1",
    )
    return parser.parse_args()


def read_header(stream) -> tuple[dict[str, float], list[str]]:
    header: dict[str, float] = {}
    data_lines: list[str] = []

    required_keys = {"ncols", "nrows", "cellsize"}
    location_keys = {"xllcorner", "xllcenter", "yllcorner", "yllcenter"}

    while True:
        line = stream.readline()
        if line == "":
            raise ValueError("Unexpected end of file while reading raster header.")

        stripped = line.strip()
        if stripped == "":
            continue

        parts = stripped.split()
        key = parts[0].lower()

        if len(parts) >= 2 and (
            key in required_keys or key in location_keys or key == "nodata_value"
        ):
            header[key] = float(parts[1])
            continue

        data_lines.append(stripped)
        break

    if "ncols" not in header or "nrows" not in header or "cellsize" not in header:
        raise ValueError("Raster header missing ncols, nrows, or cellsize.")

    if not (("xllcorner" in header or "xllcenter" in header) and ("yllcorner" in header or "yllcenter" in header)):
        raise ValueError("Raster header missing x/y lower-left origin.")

    return header, data_lines


def grid_origin_from_header(header: dict[str, float]) -> tuple[float, float]:
    cellsize = header["cellsize"]

    if "xllcorner" in header:
        x_center_start = header["xllcorner"] + (cellsize * 0.5)
    else:
        x_center_start = header["xllcenter"]

    if "yllcorner" in header:
        y_center_start = header["yllcorner"] + (cellsize * 0.5)
    else:
        y_center_start = header["yllcenter"]

    return x_center_start, y_center_start


def convert_ascii_to_csv(input_path: Path, output_path: Path, sample_step: int) -> tuple[int, int]:
    if sample_step <= 0:
        raise ValueError("--sample-step must be greater than 0.")

    with input_path.open("r", encoding="utf-8") as stream:
        header, buffered_lines = read_header(stream)

        ncols = int(header["ncols"])
        nrows = int(header["nrows"])
        nodata_value = header.get("nodata_value")
        cellsize = header["cellsize"]
        x_center_start, y_center_start = grid_origin_from_header(header)

        output_path.parent.mkdir(parents=True, exist_ok=True)

        written_rows = 0
        total_valid_points = 0

        with output_path.open("w", encoding="utf-8", newline="") as out_stream:
            writer = csv.writer(out_stream)
            writer.writerow(["latitude", "longitude", "height"])

            all_lines = buffered_lines + [line.strip() for line in stream if line.strip()]
            if len(all_lines) != nrows:
                raise ValueError(f"Expected {nrows} raster rows, found {len(all_lines)}.")

            for row_index, line in enumerate(all_lines):
                values = line.split()
                if len(values) != ncols:
                    raise ValueError(
                        f"Expected {ncols} columns on raster row {row_index}, found {len(values)}."
                    )

                if row_index % sample_step != 0:
                    continue

                latitude = y_center_start + ((nrows - 1 - row_index) * cellsize)

                for col_index, raw_value in enumerate(values):
                    if col_index % sample_step != 0:
                        continue

                    height = float(raw_value)
                    if nodata_value is not None and height == nodata_value:
                        continue

                    longitude = x_center_start + (col_index * cellsize)
                    writer.writerow([f"{latitude:.12f}", f"{longitude:.12f}", f"{height:.3f}"])
                    written_rows += 1

                total_valid_points += ncols

    return written_rows, total_valid_points


def main() -> int:
    args = parse_args()
    written_rows, _ = convert_ascii_to_csv(args.input, args.output, args.sample_step)
    print(f"Wrote {written_rows} terrain points to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
