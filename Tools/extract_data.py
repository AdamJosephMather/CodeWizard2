import geopandas as gpd
import numpy as np
import topojson as tp
import struct
from shapely.geometry import LineString
import pickle

print("imported")

load = True

if load:
	with open("topology.pkl", "rb") as f:
		topo = pickle.load(f)
else:
	countries = gpd.read_file(
		"C:\\Users\\adamj\\Downloads\\gadm_410-levels.gpkg",
		layer="ADM_0"
	)
	
	print("got data")
	
	# Construct topology FIRST.
	# Keep coordinates unquantized so arcs remain ordinary lon/lat coordinates.
	topo = tp.Topology(
		countries,
		prequantize=False
	)
	
	with open("topology.pkl", "wb") as f:
		pickle.dump(topo, f)

print("got topology")

topo_dict = topo.to_dict()
arcs = topo_dict["arcs"]

print(f"Topology contains {len(arcs)} unique arcs")

SIMPLIFY_TOLERANCE = 2
drawable_boundaries = []

for i, arc in enumerate(arcs):
	if len(arc) < 2:
		continue

	# Simplify THIS UNIQUE ARC, rather than simplifying whole countries.
	line = LineString(arc)

	line = line.simplify(
		SIMPLIFY_TOLERANCE,
		preserve_topology=False
	)

	coords = np.asarray(line.coords, dtype=np.float64)

	if len(coords) < 2:
		continue

	lon_rad = np.radians(coords[:, 0])
	lat_rad = np.radians(coords[:, 1])

	theta = lon_rad

	# Inverted to match your renderer.
	z = -np.sin(lat_rad)

	drawable_boundaries.append(
		np.column_stack((theta, z))
	)

print(f"Total unique line segments to draw: {len(drawable_boundaries)}")

output_bin_path = "globe_borders.bin"

with open(output_bin_path, "wb") as f:
	f.write(struct.pack("<I", len(drawable_boundaries)))

	for path in drawable_boundaries:
		path_f32 = path.astype("<f4")

		f.write(struct.pack("<I", len(path_f32)))
		f.write(path_f32.tobytes())

print(f"Exported successfully to {output_bin_path}")