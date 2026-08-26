import mapbox_earcut as earcut
import matplotlib.pyplot as plt
import numpy as np
from svgpathtools import Line, svg2paths


def convert_svg_to_triangles(
	svg_file, output_txt, curve_resolution=20, canvas_size=2500.0
):
	"""Extracts paths from SVG, samples curves, triangulates concave paths using

	ear-clipping, and exports flattened triangle vertices.
	"""
	paths, _ = svg2paths(svg_file)
	all_triangles = []

	for path in paths:
		pts = []
		for segment in path:
			num_samples = 2 if isinstance(segment, Line) else curve_resolution
			t_values = np.linspace(0, 1, num_samples)

			for i, t in enumerate(t_values):
				if i == 0 and len(pts) > 0:
					continue
				p = segment.point(t)
				norm_x = p.real / canvas_size
				norm_y = p.imag / canvas_size
				pts.append((norm_x, norm_y))

		# Ear clipping requires at least 3 vertices
		if len(pts) < 3:
			continue

		verts = np.array(pts, dtype=np.float32)
		rings = np.array([len(pts)], dtype=np.int32)

		# Generate indices for triangles: returns a 1D array of vertex indices
		indices = earcut.triangulate_float32(verts, rings)

		# Append actual triangle vertices in order
		for idx in indices:
			all_triangles.append(pts[idx])

	# Save total triangle count and vertex coordinates
	num_triangles = len(all_triangles) // 3
	with open(output_txt, "w") as f:
		f.write(f"{num_triangles}\n")
		for x, y in all_triangles:
			f.write(f"{x:.6f} {y:.6f}\n")

	return all_triangles


def render_preview(triangles):
	"""Previews triangulated mesh in Matplotlib."""
	plt.figure(figsize=(7, 7))

	# Reshape flat array into N triangles of 3 vertices each
	tri_array = np.array(triangles).reshape(-1, 3, 2)

	for tri in tri_array:
		t_closed = np.vstack([tri, tri[0]])
		plt.plot(t_closed[:, 0], t_closed[:, 1], "b-", linewidth=0.5)
		plt.fill(tri[:, 0], tri[:, 1], color="skyblue", alpha=0.5)

	plt.title("Triangulated SVG Mesh Preview")
	plt.xlim(0, 1)
	plt.ylim(0, 1)
	plt.gca().invert_yaxis()
	plt.gca().set_aspect("equal", adjustable="box")
	plt.grid(True, linestyle="--", alpha=0.5)
	plt.show()


if __name__ == "__main__":
	svg_input = "main_only_plain.svg"
	txt_output = "normalized_triangles.txt"

	triangles_data = convert_svg_to_triangles(
		svg_file=svg_input,
		output_txt=txt_output,
		curve_resolution=25,
		canvas_size=2048.0,
	)

	render_preview(triangles_data)