"""
GeoFPS Terrain Profile Importer for Blender
============================================
Paste this script into Blender's Text Editor and press Run Script.

Reads a .geofpsprofile file and creates mesh objects from the terrain
sample data.

QUICKSTART
----------
1. Set FILE_PATH below to your .geofpsprofile file.
2. Choose MODE:
     "elevation"   – X = distance along path (m),  Z = height ASL (m).
                     Good for cross-section visualization.
     "3d"          – Full 3-D path in GeoFPS scene-local coordinates,
                     centered at world origin for easy viewing.
     "geofps_3d"   – ★ USE THIS when you want to place objects on the
                     profile and export them back to GeoFPS as a GLB.
                     No centering applied — vertex positions in Blender
                     map 1-to-1 to GeoFPS local coordinates after export.
     "both"        – imports elevation + 3d objects
3. Run Script (Alt + P in the Text Editor, or the ▶ button).

PLACING OBJECTS FOR RE-EXPORT TO GEOFPS
----------------------------------------
  1. Set MODE = "geofps_3d"
  2. Run the script — the profile polyline appears in the viewport.
  3. Snap objects (cubes, markers, etc.) to the profile vertices using
     vertex snapping (Shift+Tab → Vertex snap, then move with G).
  4. Export selection as GLB (File → Export → glTF 2.0).
  5. Load the GLB in GeoFPS — objects will sit exactly on the terrain.

  ⚠ Do NOT use CENTER_ORIGIN=True with "geofps_3d" mode — centering
     bakes a coordinate offset into your object translations that will
     make them appear at the wrong position in GeoFPS.

OPTIONAL SETTINGS
-----------------
STEP            subsample factor – 1 = every point, 5 = every 5th point
SKIP_INVALID    skip samples marked valid=0
CENTER_ORIGIN   center the mesh at world origin (only for "elevation"/"3d" viewing;
                automatically forced OFF for "geofps_3d" mode)
IMPORT_WAYPOINTS  also place a vertex at each user-drawn waypoint
WAYPOINTS_ONLY  (geofps_3d mode only) skip the full terrain polyline and create
                a short polyline using ONLY the waypoint positions.
                Each waypoint's GeoFPS (x, y, z) is resolved by matching its
                lat/lon to the nearest terrain sample — so the vertices sit
                exactly on the terrain surface at the waypoint locations.
                Useful when you only care about the path control points and
                want to keep the scene lightweight.
PROFILE_NAME    override the object name (leave "" to use the name in the file)
SCALE           multiply all coordinates by this factor
                (1.0 = metres — recommended for GeoFPS re-export)
"""

import bpy
import bmesh

# ──────────────────────────────────────────────────────────────────────────────
#  USER SETTINGS — edit here
# ──────────────────────────────────────────────────────────────────────────────

FILE_PATH = "/Users/dhurianvitoldas/Documents/Development/GeoFPS/assets/datasets/nepal_everest_demo/terrain/LineProfile_Nepal.geofpsprofile"

MODE             = "geofps_3d"   # "elevation" | "3d" | "geofps_3d" | "both"
STEP             = 1             # use every Nth sample  (1 = all 8 827 pts)
SKIP_INVALID     = True          # skip samples where valid=0
CENTER_ORIGIN    = True          # move bounding-box centre to world origin
IMPORT_WAYPOINTS = True          # create a separate object for the 13 waypoints
WAYPOINTS_ONLY   = False         # geofps_3d only: skip terrain, import waypoints polyline only
PROFILE_NAME     = ""            # leave "" to read name from file
SCALE            = 1.0           # unit scale — 1.0 = metres

# ──────────────────────────────────────────────────────────────────────────────
#  PARSER
# ──────────────────────────────────────────────────────────────────────────────

def parse_geofpsprofile(path):
    """Return (profile_name, samples, waypoints).

    samples  = list of dicts with keys:
                 distance_m, x, y, z, latitude, longitude, height, valid
    waypoints = list of dicts with keys:
                 latitude, longitude, auxiliary
    """
    profiles = []
    current_profile_name = "Profile"
    samples = []
    waypoints = []

    in_profile  = False
    in_sample   = False
    in_vertex   = False
    cur_sample  = {}
    cur_vertex  = {}

    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()

            if line == "[terrain_profile]":
                in_profile = True
                samples    = []
                waypoints  = []
                continue

            if line == "[/terrain_profile]":
                profiles.append((current_profile_name, samples, waypoints))
                in_profile = False
                continue

            if not in_profile:
                continue

            if line.startswith("name=") and not in_sample and not in_vertex:
                current_profile_name = line[5:]
                continue

            if line == "[sample]":
                in_sample  = True
                cur_sample = {}
                continue

            if line == "[/sample]":
                in_sample = False
                samples.append(cur_sample)
                continue

            if line == "[vertex]":
                in_vertex  = True
                cur_vertex = {}
                continue

            if line == "[/vertex]":
                in_vertex = False
                waypoints.append(cur_vertex)
                continue

            if "=" not in line:
                continue

            key, _, val = line.partition("=")

            if in_sample:
                if   key == "distance_m":    cur_sample["distance_m"] = float(val)
                elif key == "x":             cur_sample["x"]          = float(val)
                elif key == "y":             cur_sample["y"]          = float(val)
                elif key == "z":             cur_sample["z"]          = float(val)
                elif key == "latitude":      cur_sample["latitude"]   = float(val)
                elif key == "longitude":     cur_sample["longitude"]  = float(val)
                elif key == "height":        cur_sample["height"]     = float(val)
                elif key == "valid":         cur_sample["valid"]      = int(val)

            elif in_vertex:
                if   key == "latitude":   cur_vertex["latitude"]   = float(val)
                elif key == "longitude":  cur_vertex["longitude"]  = float(val)
                elif key == "auxiliary":  cur_vertex["auxiliary"]  = int(val)

    return profiles


# ──────────────────────────────────────────────────────────────────────────────
#  HELPERS
# ──────────────────────────────────────────────────────────────────────────────

def make_polyline_mesh(name, verts_3d):
    """Create a Blender mesh object from a list of (x, y, z) tuples."""
    mesh = bpy.data.meshes.new(name)
    obj  = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()
    bverts = [bm.verts.new(v) for v in verts_3d]
    bm.verts.ensure_lookup_table()
    for i in range(len(bverts) - 1):
        bm.edges.new((bverts[i], bverts[i + 1]))
    bm.to_mesh(mesh)
    bm.free()
    return obj


def center_object(obj):
    """Shift all vertex positions so the bounding-box centre is at origin."""
    mesh = obj.data
    xs = [v.co.x for v in mesh.vertices]
    ys = [v.co.y for v in mesh.vertices]
    zs = [v.co.z for v in mesh.vertices]
    cx = (min(xs) + max(xs)) * 0.5
    cy = (min(ys) + max(ys)) * 0.5
    cz = (min(zs) + max(zs)) * 0.5
    for v in mesh.vertices:
        v.co.x -= cx
        v.co.y -= cy
        v.co.z -= cz


def nearest_sample_by_latlon(lat, lon, sample_list):
    """Return the sample whose lat/lon is closest to (lat, lon).

    Returns the sample dict, or None when sample_list is empty.
    Uses squared-degree distance — fast and accurate enough for short profiles.
    """
    best = None
    best_d2 = 1e18
    for s in sample_list:
        dlat = s.get("latitude",  0.0) - lat
        dlon = s.get("longitude", 0.0) - lon
        d2 = dlat * dlat + dlon * dlon
        if d2 < best_d2:
            best_d2 = d2
            best    = s
    return best


def waypoints_geofps_3d_mesh(name, waypoint_list, sample_list, scale):
    """Create a geofps_3d polyline from waypoints only.

    For each waypoint the GeoFPS local (x, y, z) is resolved from the
    nearest terrain sample by lat/lon match, then mapped to Blender
    Z-up coordinates: Blender(X, Y, Z) = GeoFPS(x, -z, y).
    """
    if not waypoint_list:
        return None

    verts = []
    for wp in waypoint_list:
        s = nearest_sample_by_latlon(wp["latitude"], wp["longitude"], sample_list)
        if s is None:
            verts.append((0.0, 0.0, 0.0))
        else:
            # GeoFPS → Blender Z-up:  Blender(X, Y, Z) = GeoFPS(x, -z, y)
            verts.append((s["x"] * scale,
                          -s["z"] * scale,
                           s["y"] * scale))

    mesh = bpy.data.meshes.new(name)
    obj  = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()
    bverts = [bm.verts.new(v) for v in verts]
    bm.verts.ensure_lookup_table()
    for i in range(len(bverts) - 1):
        bm.edges.new((bverts[i], bverts[i + 1]))
    bm.to_mesh(mesh)
    bm.free()
    return obj


def make_waypoints_mesh(name, waypoint_list, sample_list, scale):
    """
    For each waypoint (lat/lon), find the nearest sample to get an elevation
    and place a vertex there.  Falls back to Z=0 when no match is found.
    """
    # Build a quick lat/lon → height lookup from samples.
    def nearest_height(lat, lon):
        best = None
        best_d2 = 1e18
        for s in sample_list:
            if s.get("valid", 1) == 0:
                continue
            dlat = s["latitude"]  - lat
            dlon = s["longitude"] - lon
            d2   = dlat * dlat + dlon * dlon
            if d2 < best_d2:
                best_d2 = d2
                best    = s
        return best["height"] if best else 0.0

    # Build a fake horizontal position from latitude/longitude differences
    # relative to the first waypoint (rough equirectangular projection).
    if not waypoint_list:
        return None

    lat0 = waypoint_list[0]["latitude"]
    lon0 = waypoint_list[0]["longitude"]
    R    = 6_371_000.0  # Earth radius in metres

    import math
    verts = []
    for wp in waypoint_list:
        dx = math.radians(wp["longitude"] - lon0) * R * math.cos(math.radians(lat0))
        dy = math.radians(wp["latitude"]  - lat0) * R
        h  = nearest_height(wp["latitude"], wp["longitude"])
        verts.append((dx * scale, dy * scale, h * scale))

    mesh = bpy.data.meshes.new(name)
    obj  = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()
    bverts = [bm.verts.new(v) for v in verts]
    bm.verts.ensure_lookup_table()
    # Connect waypoints in order.
    for i in range(len(bverts) - 1):
        bm.edges.new((bverts[i], bverts[i + 1]))
    bm.to_mesh(mesh)
    bm.free()
    return obj


# ──────────────────────────────────────────────────────────────────────────────
#  MAIN
# ──────────────────────────────────────────────────────────────────────────────

def run():
    print(f"\n{'='*60}")
    print(f"GeoFPS Profile Importer — {FILE_PATH}")
    print(f"{'='*60}")

    profiles = parse_geofpsprofile(FILE_PATH)
    if not profiles:
        print("ERROR: no [terrain_profile] block found.")
        return

    for (file_name, raw_samples, waypoints) in profiles:
        obj_name = PROFILE_NAME if PROFILE_NAME else file_name

        # Filter and subsample.
        filtered = [
            s for s in raw_samples
            if not (SKIP_INVALID and s.get("valid", 1) == 0)
        ]
        filtered = filtered[::STEP]

        print(f"\nProfile  : {obj_name}")
        print(f"Waypoints: {len(waypoints)}")
        print(f"Samples  : {len(raw_samples)} total  →  "
              f"{len(filtered)} after filter/step")

        if not filtered:
            print("WARNING: no valid samples — nothing to import.")
            continue

        # ── Elevation profile (2-D cross-section) ──────────────────────────
        if MODE in ("elevation", "both"):
            verts = [
                (s["distance_m"] * SCALE,   # X = distance along path
                 0.0,                        # Y = flat (for 2-D view)
                 s["height"]     * SCALE)    # Z = height ASL (Blender Z-up)
                for s in filtered
            ]
            elev_name = f"{obj_name}_elevation"
            elev_obj  = make_polyline_mesh(elev_name, verts)
            if CENTER_ORIGIN:
                center_object(elev_obj)
            print(f"Created  : '{elev_name}'  ({len(verts)} vertices)")

            # Print stats.
            heights = [s["height"] for s in filtered]
            dists   = [s["distance_m"] for s in filtered]
            print(f"  Height : {min(heights):.1f} – {max(heights):.1f} m ASL "
                  f"(range {max(heights)-min(heights):.1f} m)")
            print(f"  Length : {max(dists)/1000:.2f} km")

        # ── 3-D path using GeoFPS local coords (centered for viewing) ──────
        if MODE in ("3d", "both"):
            # GeoFPS uses Y-up; Blender uses Z-up.
            # Mapping: Blender(X, Y, Z) = GeoFPS(x, -z, y)
            verts = [
                ( s["x"] * SCALE,
                 -s["z"] * SCALE,
                  s["y"] * SCALE)
                for s in filtered
            ]
            path_name = f"{obj_name}_3d_path"
            path_obj  = make_polyline_mesh(path_name, verts)
            if CENTER_ORIGIN:
                center_object(path_obj)
            print(f"Created  : '{path_name}'  ({len(verts)} vertices)")

        # ── GeoFPS-ready 3-D path (NO centering — use for GLB re-export) ──
        if MODE == "geofps_3d":
            # Blender(X, Y, Z) = GeoFPS(x, -z, y)
            # After Blender→glTF export: glTF(X, Y, Z) = GeoFPS(x, y, z)  ✓
            # CENTER_ORIGIN is intentionally ignored here — centering would
            # bake a wrong offset into exported object translations.

            if WAYPOINTS_ONLY and waypoints:
                # ── Waypoints-only polyline ──────────────────────────────────
                # Resolve each waypoint's lat/lon to its GeoFPS (x, y, z) via
                # nearest-sample matching, then convert to Blender Z-up coords.
                path_name = f"{obj_name}_geofps_3d_waypoints"
                path_obj  = waypoints_geofps_3d_mesh(path_name, waypoints,
                                                     raw_samples, SCALE)
                if path_obj:
                    # Collect actual vertex positions for stats.
                    xs = [v.co.x for v in path_obj.data.vertices]
                    zs = [v.co.z for v in path_obj.data.vertices]
                    print(f"Created  : '{path_name}'  ({len(waypoints)} waypoint vertices)")
                    print(f"  Blender X range : {min(xs):.1f} – {max(xs):.1f} m")
                    print(f"  Blender Z range : {min(zs):.1f} – {max(zs):.1f} m  (GeoFPS height offset)")
                    print(f"  ★ Snap objects to these vertices, export GLB → loads correctly in GeoFPS")
                else:
                    print("WARNING: no waypoints found — nothing created for geofps_3d.")

            else:
                # ── Full terrain polyline ────────────────────────────────────
                verts = [
                    ( s["x"] * SCALE,
                     -s["z"] * SCALE,
                      s["y"] * SCALE)
                    for s in filtered
                ]
                path_name = f"{obj_name}_geofps_3d"
                path_obj  = make_polyline_mesh(path_name, verts)
                # No centering — coordinates must stay at real GeoFPS values.
                xs = [v[0] for v in verts]
                zs = [v[2] for v in verts]  # Blender Z = GeoFPS Y (height offset)
                print(f"Created  : '{path_name}'  ({len(verts)} vertices)")
                print(f"  Blender X range : {min(xs):.1f} – {max(xs):.1f} m")
                print(f"  Blender Z range : {min(zs):.1f} – {max(zs):.1f} m  (GeoFPS height offset)")
                print(f"  ★ Snap objects to these vertices, export GLB → loads correctly in GeoFPS")

        # ── Waypoints object ───────────────────────────────────────────────
        if IMPORT_WAYPOINTS and waypoints:
            wp_name = f"{obj_name}_waypoints"
            wp_obj  = make_waypoints_mesh(wp_name, waypoints,
                                          raw_samples, SCALE)
            if wp_obj:
                if CENTER_ORIGIN:
                    # Shift waypoints by the same elevation-profile centre so
                    # they line up visually when both objects exist.
                    pass  # waypoints are in geo-space; leave them as-is
                print(f"Created  : '{wp_name}'  ({len(waypoints)} waypoints)")

    # Deselect all, then select newly created objects.
    bpy.ops.object.select_all(action='DESELECT')
    for obj in bpy.context.scene.objects:
        if any(obj.name.endswith(suffix)
               for suffix in ("_elevation", "_3d_path", "_geofps_3d",
                              "_geofps_3d_waypoints", "_waypoints")):
            obj.select_set(True)

    print(f"\n✓ Import complete.\n")


run()
