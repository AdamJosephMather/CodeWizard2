#pragma once

#include <GLFW/glfw3.h>
#include <functional>
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
	
	bool window_button = false;
	bool execute_on_down = true;
	
	bool rounded = true;
	bool alignLeft = false;
	int text_special = 0;
	
	Color* background_color;
	Color* background_color_hover;
	Color* text_color;
	Color* text_color_hover;
	Color* border_color;
	Color* border_color_hover;
	icu::UnicodeString BUTTON_LABEL;
	
	Positioner POSITIONER;
	OnClick ONCLICK;
private:
};