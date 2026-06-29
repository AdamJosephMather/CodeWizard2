#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include "widget.h"

class ScrollNotify : public Widget {
public:
	using Positioner = std::function<void(ScrollNotify*,int,int,int,int)>;
	
	ScrollNotify(Widget* parent, Positioner positioner);
	
	void render();
	void position(int x, int y, int width, int height);
	
	void displayMessage(MST::MonoString text);
	
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	
	MST::MonoString text;
	
private:
	Positioner POSITIONER;
	int offset = 0;
};