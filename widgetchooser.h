#pragma once

#include "button.h"
#include "helper_types.h"
#include "widget.h"
#include <GLFW/glfw3.h>

class WidgetChooser : public Widget {
public:
	WidgetChooser(Widget* parent);
	
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void render();
private:
	Button *b1;
	Button *b2;
	Button *b3;
};