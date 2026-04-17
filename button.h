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
	
	bool transparent;
	bool window_button;
	bool execute_on_down = true;
	
	bool border = false;
	bool rounded = false;
	bool alignLeft = false;
	bool isContext = false;
	int text_special = 0;
	Color* background_color;
	icu::UnicodeString BUTTON_LABEL;
	
private:
	Positioner POSITIONER;
	OnClick ONCLICK;
};