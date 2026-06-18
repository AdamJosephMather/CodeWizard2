#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include "helper_types.h"
#include "widget.h"

class CheckBox : public Widget {
public:	
	using CheckPositioner = std::function<void(CheckBox*,int,int,int,int)>;
	using CheckOnClick = std::function<void(CheckBox*)>;
	
	CheckBox(Widget* parent, CheckPositioner positioner, CheckOnClick onclick);
	
	bool rounded = false;
	Color* bgcolor;

	void render();
	void position(int x, int y, int width, int height);
	
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	
	bool is_checked = false;
	Color* border_color;
	Color* border_color_hover;

private:
	CheckPositioner POSITIONER;
	CheckOnClick ONCLICK;
	
	bool hovered;
};