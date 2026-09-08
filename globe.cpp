#include "globe.h"
#include "application.h"
#include "text_renderer.h"
#include <cmath>
#include <limits>

#ifndef M_PI
const float M_PI = 3.141592653589793238f; // nice
#endif

float Mouse_Speed_Constant = 3.0;

const int resolution_long = 50;
const int resolution_lat = 100;

const int line_count_long = 20;
const int line_count_lat = 10;

const int line_count_pyramid = 5;

enum PyramidFace : uint32_t {
	PYRAMID_BASE  = 1 << 0,
	PYRAMID_POS_X = 1 << 1,
	PYRAMID_NEG_X = 1 << 2,
	PYRAMID_POS_Y = 1 << 3,
	PYRAMID_NEG_Y = 1 << 4,
};

uint32_t getPyramidFaces(const Vec3& p) {
	const float eps = 0.001f;

	uint32_t faces = 0;

	// Base: z = 1
	if (std::abs(p.z - 1.0f) < eps)
		faces |= PYRAMID_BASE;

	// +X side: 2x - z = 1
	if (std::abs(2.0f * p.x - p.z - 1.0f) < eps)
		faces |= PYRAMID_POS_X;

	// -X side: -2x - z = 1
	if (std::abs(-2.0f * p.x - p.z - 1.0f) < eps)
		faces |= PYRAMID_NEG_X;

	// +Y side: 2y - z = 1
	if (std::abs(2.0f * p.y - p.z - 1.0f) < eps)
		faces |= PYRAMID_POS_Y;

	// -Y side: -2y - z = 1
	if (std::abs(-2.0f * p.y - p.z - 1.0f) < eps)
		faces |= PYRAMID_NEG_Y;

	return faces;
}

Vec3 pyramidFaceNormal(uint32_t face) {
	switch (face) {
		case PYRAMID_BASE:
			return {0, 0, 1};

		case PYRAMID_POS_X:
			return {2, 0, -1};

		case PYRAMID_NEG_X:
			return {-2, 0, -1};

		case PYRAMID_POS_Y:
			return {0, 2, -1};

		case PYRAMID_NEG_Y:
			return {0, -2, -1};
	}

	return {0, 0, 0};
}

std::vector<Line3d> loadGlobeBorders(const std::string& filename) {
	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open binary border file.");
	}
	
	uint32_t num_lines = 0;
	file.read(reinterpret_cast<char*>(&num_lines), sizeof(num_lines));

	std::vector<Line3d> lines(num_lines);
	
	for (uint32_t i = 0; i < num_lines; ++i) {
		uint32_t num_points = 0;
		file.read(reinterpret_cast<char*>(&num_points), sizeof(num_points));
		
		lines[i].points.resize(num_points);
		
		file.read(
			reinterpret_cast<char*>(lines[i].points.data()),
			num_points * sizeof(Vec3)
		);
	}

	return lines;
}

Vec3 findPyramid(const Vec3& point) {
	float t = std::numeric_limits<float>::infinity();

	// Check intersection with the base plane z = 1
	if (point.z > 0.0f) {
		float t_base = 1.0f / point.z;
		if (t_base < t) {
			t = t_base;
		}
	}

	// Check intersection with X slant planes (2|x| - z = 1)
	float denom_x = 2.0f * std::abs(point.x) - point.z;
	if (denom_x > 0.0f) {
		float t_x = 1.0f / denom_x;
		if (t_x < t) {
			t = t_x;
		}
	}

	// Check intersection with Y slant planes (2|y| - z = 1)
	float denom_y = 2.0f * std::abs(point.y) - point.z;
	if (denom_y > 0.0f) {
		float t_y = 1.0f / denom_y;
		if (t_y < t) {
			t = t_y;
		}
	}

	// If zero vector or invalid direction
	if (std::isinf(t)) {
		return Vec3{0.0f, 0.0f, 0.0f};
	}

	return Vec3{point.x * t, point.y * t, point.z * t};
}

void updateToPyramid(std::vector<Line3d>* globular) {
	for (int i = 0; i < globular->size(); i++) {
		for (int j = 0; j < globular->at(i).points.size(); j++) {
			globular->at(i).points[j] = findPyramid(globular->at(i).points[j]);
		}
	}
}

void Globe::updateGlobeData() {
	borders.clear();
	longitudinal_lines.clear();
	lateral_lines.clear();
	bright_edges.clear();
	
	if (!currently_pyramid) {
		for (int i = 0; i < line_count_long; i++) {
			Line3d l;
			
			float theta_1 = ((float)i/(float)line_count_long) * 2 * M_PI;
			
			for (int j = 0; j < resolution_long; j++) {
				float theta_2 = ((float)j/(float)(resolution_long-1)) * M_PI; // we only want half the thing, from 0 to pi/2 not to 2 pi
				
				float x = std::cos(theta_1) * std::cos(M_PI/2.0 - theta_2);
				float y = std::sin(theta_1) * std::cos(M_PI/2.0 - theta_2);
				float z = std::sin(M_PI/2.0 - theta_2);
				
				Vec3 p = {
					x,
					y,
					z
				};
				
				l.points.push_back(p);
			}
			
			longitudinal_lines.push_back(l);
		}
		
		for (int i = 0; i < line_count_lat; i++) {
			Line3d l;
			
			float theta_2 = ((float)i/(float)line_count_lat) * M_PI;
			
			float z = std::sin(M_PI/2.0 - theta_2);
			
			for (int j = 0; j < resolution_lat; j++) {
				float theta_1 = ((float)j/(float)(resolution_lat-1)) * 2 * M_PI; // we only want half the thing, from 0 to pi/2 not to 2 pi
				
				float x = std::cos(theta_1) * std::cos(M_PI/2.0 - theta_2);
				float y = std::sin(theta_1) * std::cos(M_PI/2.0 - theta_2);
				
				Vec3 p = {
					x,
					y,
					z
				};
				
				l.points.push_back(p);
			}
			
			lateral_lines.push_back(l);
		}
	}else{
		for (int i = 0; i < line_count_pyramid; i++) {
			float moved = 1.0 - ((float)(i+1)/(float)(line_count_pyramid+1))*2.0;
			
			Line3d l;
			l.points.push_back({0, 0, -1}); // top
			l.points.push_back({moved, -1, 1});
			l.points.push_back({moved, 1, 1});
			l.points.push_back({0, 0, -1}); // top
			
			Line3d l2;
			l2.points.push_back({0, 0, -1}); // top
			l2.points.push_back({-1, moved, 1});
			l2.points.push_back({1, moved, 1});
			l2.points.push_back({0, 0, -1}); // top
			
			longitudinal_lines.push_back(l);
			longitudinal_lines.push_back(l2);
		}
		
		for (int i = 0; i < line_count_pyramid; i++) {
			Line3d l;
			
			float z = 1.0 - ((float)(i+1)/(float)(line_count_pyramid+1))*2.0;
			float x = 0.5 + z/2;
			
			l.points.push_back({x, x, z});
			l.points.push_back({x, -x, z});
			l.points.push_back({-x, -x, z});
			l.points.push_back({-x, x, z});
			l.points.push_back({x, x, z});
			
			lateral_lines.push_back(l);
		}
	}
	
	borders = loadGlobeBorders(getExecutableDir()+"/globe_borders.bin");
	
	if (currently_pyramid) {
		updateToPyramid(&borders);
		
		Line3d square;
		square.points.push_back({-1, -1, 1});
		square.points.push_back({-1, 1, 1});
		square.points.push_back({1, 1, 1});
		square.points.push_back({1, -1, 1});
		square.points.push_back({-1, -1, 1});
		bright_edges.push_back(square);
		
		Line3d first;
		first.points.push_back({-1, -1, 1});
		first.points.push_back({0, 0, -1});
		first.points.push_back({-1, 1, 1});
		bright_edges.push_back(first);
		
		Line3d second;
		second.points.push_back({1, -1, 1});
		second.points.push_back({0, 0, -1});
		second.points.push_back({1, 1, 1});
		bright_edges.push_back(second);
	}
	
	rerender = true;
	App::time_till_regular = 3;
}

Globe::Globe(Widget *parent) : Widget(parent) {
	id = MST::toMonoString("Globe");
	
	pyramid = new Button(this, MST::toMonoString("Globe"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+TextRenderer::get_text_height();
		btn->t_y = t_y+TextRenderer::get_text_height();
	},  [&](Button* btn){
		// onclick
		currently_pyramid = !currently_pyramid;
		
		if (currently_pyramid) {
			pyramid->BUTTON_LABEL = MST::toMonoString("Pyramid");
		}else {
			pyramid->BUTTON_LABEL = MST::toMonoString("Globe");
		}
		
		updateGlobeData();
	});
	pyramid->rounded = true;
	
	updateGlobeData();
}

Matrix makeRotMatrix(float theta_z, float theta_x, float s) {
	return {
		{s*std::cos(theta_z), s*-std::sin(theta_z), s*0},
		{s*std::cos(theta_x)*std::sin(theta_z), s*std::cos(theta_x)*std::cos(theta_z), s*-std::sin(theta_x)},
		{s*std::sin(theta_x)*std::sin(theta_z), s*std::sin(theta_x)*std::cos(theta_z), s*std::cos(theta_x)}
	};
}

Vec3 rotate(const Vec3& p, Matrix matrix) {
	const float x = p.x*matrix.r1.x + p.y*matrix.r1.y + p.z*matrix.r1.z;
	const float y = p.x*matrix.r2.x + p.y*matrix.r2.y + p.z*matrix.r2.z;
	const float z = p.x*matrix.r3.x + p.y*matrix.r3.y + p.z*matrix.r3.z;
	
	return {
		x,
		y,
		z
	};
}

bool pyramidFaceVisible(uint32_t face, Matrix matrix) {
	Vec3 normal = rotate(pyramidFaceNormal(face), matrix);
	
	return normal.y <= 0.0f;
}

bool pyramidPointVisible(const Vec3& p, Matrix matrix) {
	uint32_t faces = getPyramidFaces(p);

	const uint32_t allFaces[] = {
		PYRAMID_BASE,
		PYRAMID_POS_X,
		PYRAMID_NEG_X,
		PYRAMID_POS_Y,
		PYRAMID_NEG_Y
	};

	for (uint32_t face : allFaces) {
		if ((faces & face) && pyramidFaceVisible(face, matrix))
			return true;
	}

	return false;
}

bool Globe::on_key_event(int key, int scancode, int action, int mods) {
	return Widget::on_key_event(key, scancode, action, mods);
}

void Globe::position(int x, int y, int w, int h) {
	OldGlobeRenderState NEWSTATE = {x, y, w, h};
	if (OLDSTATE.t_x != NEWSTATE.t_x || OLDSTATE.t_y != NEWSTATE.t_y || OLDSTATE.t_w != NEWSTATE.t_w || OLDSTATE.t_h != NEWSTATE.t_h) {
		rerender = true;
		OLDSTATE = NEWSTATE;
	}
	
	Widget::position(x, y, w, h);
}

bool Globe::on_scroll_event(double xchange, double ychange) {
	return false;
}

void renderLine(int x, int y, Line3d line, Matrix matrix, Color* color, bool pyramid) {
	if (line.points.size() < 2) {
		return;
	}

	Vec3 lastOriginal = line.points[0];
	Vec3 last = rotate(lastOriginal, matrix);

	for (int i = 1; i < line.points.size(); i++) {
		Vec3 nextOriginal = line.points[i];
		Vec3 next = rotate(nextOriginal, matrix);

		bool draw = false;

		if (!pyramid) {
			// Existing sphere behaviour
			draw = (next.y <= 0 && last.y <= 0);
		}
		else {
			uint32_t lastFaces = getPyramidFaces(lastOriginal);
			uint32_t nextFaces = getPyramidFaces(nextOriginal);

			// A segment normally belongs to the face(s) shared
			// by both of its endpoints.
			uint32_t sharedFaces = lastFaces & nextFaces;

			if (sharedFaces != 0) {
				const uint32_t allFaces[] = {
					PYRAMID_BASE,
					PYRAMID_POS_X,
					PYRAMID_NEG_X,
					PYRAMID_POS_Y,
					PYRAMID_NEG_Y
				};

				for (uint32_t face : allFaces) {
					if ((sharedFaces & face) &&
						pyramidFaceVisible(face, matrix)) {
						draw = true;
						break;
					}
				}
			}
			else {
				/*
				 * This mainly happens to globe-border segments that
				 * happen to cross a pyramid edge between two sampled
				 * points.
				 *
				 * Requiring both ends to be on visible surfaces avoids
				 * drawing a short segment around onto the back.
				 */
				draw =
					pyramidPointVisible(lastOriginal, matrix) &&
					pyramidPointVisible(nextOriginal, matrix);
			}
		}

		if (draw) {
			App::DrawLine(
				last.x + x,
				last.z + y,
				next.x + x,
				next.z + y,
				1,
				color
			);
		}

		lastOriginal = nextOriginal;
		last = next;
	}
}

void Globe::render() {
	if (rerender) {
		App::reclear = 3;
		rerender = false;
	}
	
	if (App::reclear == 0) {
		Widget::render();
		return;
	}
	
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.darker_background_color, false);
	
	Widget::render();
	
	int centerx = t_x + t_w/2;
	int centery = t_y + t_h/2;
	
	int scale = t_w<t_h ? t_w*.4 : t_h*.4;
	
	// first let's draw the longitudinal and lateral lines
	
	Matrix matrix = makeRotMatrix(rotation_z, rotation_x, scale);
	
	for (const auto& line : longitudinal_lines) {
		renderLine(centerx, centery, line, matrix, App::theme.lesser_text_color, currently_pyramid);
	}
	
	for (const auto& line : lateral_lines) {
		renderLine(centerx, centery, line, matrix, App::theme.lesser_text_color, currently_pyramid);
	}
	
	for (const auto& line : borders) {
		renderLine(centerx, centery, line, matrix, App::theme.main_text_color, currently_pyramid);
	}
	
	if (!currently_pyramid) {
		App::DrawRoundBorder(centerx-scale, centery-scale, scale*2, scale*2, App::theme.main_text_color, resolution_long/2, scale);
	}else{
		for (const auto& line : bright_edges) {
			renderLine(centerx, centery, line, matrix, App::theme.main_text_color, currently_pyramid);
		}
	}
	
	if (this == App::activeLeafNode) {
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.active_color, 5, App::text_padding);
	}else{
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}
}

bool Globe::on_mouse_button_event(int button, int action, int mods) {
	if (cursor_in_this && this != App::activeLeafNode && action == GLFW_PRESS) {
		App::setActiveLeafNode(this);
	}
	
	if (cursor_in_this && action == GLFW_PRESS) {
		dragging = true;
		was_at_x = App::mouseX;
		was_at_y = App::mouseY;
	}else if (action == GLFW_RELEASE) {
		dragging = false;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}

bool Globe::on_mouse_move_event() {
	bool clicking = glfwGetMouseButton(App::window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	
	if (!clicking) {
		dragging = false;
	}
	
	if (dragging) {
		int diff_z = App::mouseX - was_at_x;
		rotation_z += ((float)(diff_z) / (float)(t_w)) * Mouse_Speed_Constant;
		
		int diff_y = was_at_y - App::mouseY;
		rotation_x += ((float)(diff_y) / (float)(t_w)) * Mouse_Speed_Constant;
		
		if (rotation_x < -M_PI/2) {
			rotation_x = -M_PI/2;
		}else if (rotation_x > M_PI/2) {
			rotation_x = M_PI/2;
		}
		
		
		rerender = true;
		App::time_till_regular = 2;
		
		was_at_x = App::mouseX;
		was_at_y = App::mouseY;
	}
	
	return Widget::on_mouse_move_event();
}

void Globe::executeAction(WidgetActionType t) {
	if (t == WidgetActionType::THEME_CALCULATED) {
		rerender = true;
	}
}