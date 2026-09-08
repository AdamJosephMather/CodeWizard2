#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"

struct OldGlobeRenderState {
	int t_x = -1;
	int t_y = -1;
	int t_w = -1;
	int t_h = -1;
};

struct FilePoint {
	float theta = 0;
	float z = 0;
};

struct Point {
	float theta = 0;
	float z = 0;
	float length = 0;
};

struct Vec3 {
	float x = 0;
	float y = 0;
	float z = 0;
};

struct Line3d {
	std::vector<Point> points = {};
};

class Globe : public Widget {
public:
	Globe(Widget* parent);
	
	void render() override;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
	bool on_mouse_move_event() override;
	bool on_scroll_event(double xchange, double ychange) override;
	bool on_key_event(int key, int scancode, int action, int mods) override;
	void position(int x, int y, int w, int h) override;
	
	void executeAction(WidgetActionType typ) override;
	
	bool dragging = false;
	int was_at = 0;
	
	OldGlobeRenderState OLDSTATE = {};
	bool rerender = true;
	
	float rotation = 0;
	
	std::vector<Line3d> longitudinal_lines = {};
	std::vector<Line3d> lateral_lines = {};
	
	std::vector<Line3d> borders = {};
	
private:
	
};