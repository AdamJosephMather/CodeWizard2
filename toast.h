#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include <vector>
#include "helper_types.h"
#include "widget.h"
#include "unicode/unistr.h"


class Toast : public Widget {
public:	
//	using Positioner = std::function<void(Widget*,int,int,int,int)>;
//	using OnClick = std::function<void(Widget*)>;
	
	Toast(Widget* parent); //, Positioner positioner);

	void render();
	void position(int x, int y, int width, int height);
	
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	
	icu::UnicodeString text;
	double time = -1;
	
	void displayMessage(icu::UnicodeString text);

private:
//	Positioner POSITIONER;
};