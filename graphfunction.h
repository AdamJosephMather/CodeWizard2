#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include "MathParser.hpp"

// Graph-specific sampling stays outside the reusable parser library.
namespace GraphFunction {
struct Segment { double x1, y1, x2, y2; };
struct Samples {
	std::vector<Segment> segments;
	bool hasRealValues = false;
};

inline Samples sample(const std::string& expression, double xmin, double xmax,
		double ymin, double ymax, int width) {
	Samples samples;
	const double xrange = xmax - xmin, yrange = ymax - ymin;
	if (!(xrange > 0) || !(yrange > 0) || !std::isfinite(xrange) || !std::isfinite(yrange)) return samples;
	MathParser parser;
	auto state = parser.setup();
	// Expressions cannot assign state that leaks into the next sample.
	if (expression.find('=') != std::string::npos) return samples;
	auto evaluate = [&](double x) {
		state.vars["x"] = x;
		auto result = parser.RunLine(expression, state);
		if (!result.worked || !std::isfinite(result.value)) return std::numeric_limits<double>::quiet_NaN();
		samples.hasRealValues = true;
		return result.value;
	};
	const int count = std::clamp(width, 64, 4096);
	double x1 = xmin, y1 = evaluate(x1);
	for (int i = 1; i <= count; ++i) {
		double x2 = xmin + xrange * (double(i) / count);
		double y2 = evaluate(x2);
		double middle = evaluate(x1 + (x2 - x1) * 0.5);
		// Test the midpoint as well: poles can lie between two finite samples.
		if (std::isfinite(y1) && std::isfinite(y2) && std::isfinite(middle) &&
			std::abs(middle / yrange - (y1 / yrange * 0.5 + y2 / yrange * 0.5)) < 0.25) {
			// Clip before converting to integer pixels (large finite values are common near poles).
			double a = y1, b = y2, left = x1, right = x2;
			if (!((a < ymin && b < ymin) || (a > ymax && b > ymax))) {
				if (a < ymin || a > ymax) {
					double clipped = std::clamp(a, ymin, ymax);
					left = x1 + (x2 - x1) * ((clipped - y1) / (y2 - y1));
					a = clipped;
				}
				if (b < ymin || b > ymax) {
					double clipped = std::clamp(b, ymin, ymax);
					right = x1 + (x2 - x1) * ((clipped - y1) / (y2 - y1));
					b = clipped;
				}
				if (std::isfinite(left) && std::isfinite(right)) samples.segments.push_back({left, a, right, b});
			}
		}
		x1 = x2;
		y1 = y2;
	}
	return samples;
}
}
