#include "../graphfunction.h"
#include <cassert>
#include <iostream>

int main() {
	auto square = GraphFunction::sample("x^2", -2, 2, -1, 5, 400);
	assert(square.hasRealValues && square.segments.size() == 400);
	for (const auto& s : square.segments) {
		assert(std::abs(s.y1 - s.x1*s.x1) < 1e-10);
		assert(std::abs(s.y2 - s.x2*s.x2) < 1e-10);
	}
	auto root = GraphFunction::sample("sqrt(x)", -2, 2, -1, 5, 400);
	assert(root.hasRealValues && !root.segments.empty());
	for (const auto& s : root.segments) assert(s.x1 >= 0 && s.x2 >= 0);
	for (int width : {399, 400}) {
		auto pole = GraphFunction::sample("1/x", -2, 2, -10, 10, width);
		assert(pole.hasRealValues && !pole.segments.empty());
		for (const auto& s : pole.segments) {
			assert(!(s.x1 < 0 && s.x2 > 0));
			assert(s.y1 >= -10 && s.y1 <= 10 && s.y2 >= -10 && s.y2 <= 10);
		}
	}
	auto steep = GraphFunction::sample("10000*x", -2, 2, -1, 1, 399);
	assert(!steep.segments.empty());
	for (const auto& s : steep.segments) {
		assert(s.y1 >= -1 && s.y1 <= 1 && s.y2 >= -1 && s.y2 <= 1);
	}
	auto constant = GraphFunction::sample("pi", 10, 20, 0, 5, 200);
	assert(constant.hasRealValues && constant.segments.size() == 200);
	assert(constant.segments.front().x1 == 10 && constant.segments.back().x2 == 20);
	for (const auto& expression : {"", "sin(", "unknown(x)", "x=2", "sqrt(-1)", "1/0", "x\n+1"}) {
		auto invalid = GraphFunction::sample(expression, -2, 2, -10, 10, 200);
		assert(!invalid.hasRealValues && invalid.segments.empty());
	}
	assert(GraphFunction::sample("x", 2, 2, -10, 10, 200).segments.empty());
	std::cout << "Graph function sampling checks passed\n";
}
