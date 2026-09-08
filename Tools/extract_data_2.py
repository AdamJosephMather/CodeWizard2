import geopandas as gpd
import numpy as np
import struct

from shapely.ops import unary_union
from shapely import set_precision
from shapely.ops import unary_union

import pickle


load = True

if not load:
	countries = gpd.read_file(
		"C:\\Users\\adamj\\Downloads\\gadm_410-levels.gpkg",
		layer="ADM_0"
	)
	
	print("got data")
	
	countries["geometry"] = countries.geometry.apply(
		lambda g: set_precision(g, grid_size=0.03)
	)
	
	try:
		with open("countries.pkl", "wb") as f:
			pickle.dump(countries, f)
	except Exception as e:
		print(e)
else:
	with open("countries.pkl", "rb") as f:
		countries = pickle.load(f)

countries["geometry"] = countries.geometry.simplify_coverage(
   tolerance=0.4
)

print("applied precision")

merged = unary_union(countries.geometry.boundary)


if merged.geom_type == "LineString":
	lines = [merged]
elif merged.geom_type == "MultiLineString":
	lines = list(merged.geoms)
elif merged.geom_type == "GeometryCollection":
	lines = [
		g for g in merged.geoms
		if g.geom_type == "LineString"
	]
else:
	raise RuntimeError(f"Unexpected geometry: {merged.geom_type}")


drawable_boundaries = []

total_points = 0

for line in lines:
	coords = np.asarray(line.coords, dtype=np.float64)

	if len(coords) < 2:
		continue

	total_points += len(coords)

	lon_rad = np.radians(coords[:, 0])
	lat_rad = np.radians(coords[:, 1])
	
	x = np.cos(lat_rad) * np.cos(lon_rad)
	y = np.cos(lat_rad) * np.sin(lon_rad)
	z = -np.sin(lat_rad)

	drawable_boundaries.append(
		np.column_stack((x, y, z))
	)

print("Lines:", len(drawable_boundaries))
print("Points:", total_points)


output_bin_path = "globe_borders.bin"

with open(output_bin_path, "wb") as f:
	f.write(struct.pack("<I", len(drawable_boundaries)))

	for path in drawable_boundaries:
		path_f32 = path.astype("<f4")

		f.write(struct.pack("<I", len(path_f32)))
		f.write(path_f32.tobytes())

print("Exported:", output_bin_path)