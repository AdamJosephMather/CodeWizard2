#pragma once

#include "button.h"
#include "widget.h"
#include <GLFW/glfw3.h>

class WidgetChooser : public Widget {
public:
	WidgetChooser(Widget* parent);
	
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	void render();
	
	bool rounded = true;
private:
	Button* b1;
	Button* b2;
	Button* b3;
	Button* b4;
	Button* b5;
	Button* b6;
	Button* b7;
	Button* b8;
	Button* b9;
	Button* b10;
};