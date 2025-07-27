#pragma once

#include "textedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class LineNumbers : public Widget {
public:
	LineNumbers(Widget* parent);
	
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	
	double start_y;
	std::vector<icu::UnicodeString> lines_to_draw;
	std::vector<Color*> lines_to_color;
	
	void position(int x, int y, int w, int h);
	void render();
	
	void setTextedit(TextEdit* edit);
	
	TextEdit* toFollow;
private:
};