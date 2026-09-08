#include "globe.h"
#include "application.h"

#ifndef M_PI
const float M_PI = 3.141592653589793238f; // nice
#endif

float Mouse_Speed_Constant = 3.0;

const int resolution_long = 50;
const int resolution_lat = 100;

const int line_count_long = 20;
const int line_count_lat = 10;

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

Globe::Globe(Widget *parent) : Widget(parent) {
	id = MST::toMonoString("Globe");
	
	// longitudinal lines (top to bottom)
	
	for (int i = 0; i < line_count_long; i++) {
		Line3d l;
		
		float theta_1 = ((float)i/(float)line_count_long) * 2 * M_PI;
		
		for (int j = 0; j < resolution_long; j++) {
			float theta_2 = ((float)j/(float)(resolution_long-1)) * M_PI; // we only want half the thing, from 0 to pi/2 not to 2 pi
			
			
			// get xy and z
			
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
	
	borders = loadGlobeBorders(getExecutableDir()+"/globe_borders.bin");
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

void renderLine(int x, int y, Line3d line, Matrix matrix, Color* color) {
	if (line.points.size() < 2) {
		return;
	}
	
	Vec3 last = rotate(line.points[0], matrix);
	
	for (int i = 1; i < line.points.size(); i++) {
		Vec3 next = rotate(line.points[i], matrix);
		
		if (next.y <= 0 && last.y <= 0) {
			App::DrawLine(last.x + x, last.z + y, next.x + x, next.z + y, 1, color);
		}
		
		last = next;
	}
}

void Globe::render() {
	if (rerender) {
		App::reclear = 3;
		rerender = false;
	}
	
	if (App::reclear == 0) {
		return;
	}
	
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.darker_background_color, false);
	
	if (this == App::activeLeafNode) {
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.active_color, 5, App::text_padding);
	}else{
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}
	
	int centerx = t_x + t_w/2;
	int centery = t_y + t_h/2;
	
	int scale = t_w<t_h ? t_w*.4 : t_h*.4;
	
	// first let's draw the longitudinal and lateral lines
	
	Matrix matrix = makeRotMatrix(rotation_z, rotation_x, scale);
	
	for (const auto& line : longitudinal_lines) {
		renderLine(centerx, centery, line, matrix, App::theme.lesser_text_color);
	}
	
	for (const auto& line : lateral_lines) {
		renderLine(centerx, centery, line, matrix, App::theme.lesser_text_color);
	}
	
	for (const auto& line : borders) {
		renderLine(centerx, centery, line, matrix, App::theme.main_text_color);
	}
	
	App::DrawRoundBorder(centerx-scale, centery-scale, scale*2, scale*2, App::theme.main_text_color, resolution_long/2, scale);
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