#pragma once

#include <GLFW/glfw3.h>
#include "helper_types.h"
#include "widget.h"

class TitleBar : public Widget {
public:	
	TitleBar(Widget* parent);

	void render();
	void position(int x, int y, int width, int height);
	
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	
	bool is_out_of_child(int x);

private:
	bool hovered;
};