#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include <vector>
#include "helper_types.h"
#include "widget.h"
#include "unicode/unistr.h"

class Button : public Widget {
public:	
	using Positioner = std::function<void(Button*,int,int,int,int,int,int)>;
	using OnClick = std::function<void(Button*)>;
	
	Button(Widget* parent, icu::UnicodeString text, Positioner positioner, OnClick onclick);

	void render();
	void position(int x, int y, int width, int height);
	
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	
	bool transparent;
	bool window_button;
	
	int padding_button = 5;
private:
	icu::UnicodeString BUTTON_LABEL;
	Positioner POSITIONER;
	OnClick ONCLICK;
	
	bool hovered;
};