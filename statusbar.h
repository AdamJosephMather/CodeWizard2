#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>

class StatusBar : public Widget {
public:
	StatusBar(Widget* parent);
	
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	void render();
	void save();
	
	bool rounded = true;
private:
};