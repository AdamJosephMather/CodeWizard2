#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class Label : public Widget {
public:
	Label(Widget* parent);
	
	icu::UnicodeString fulltext;
	std::vector<icu::UnicodeString> drawlines;
	
	void position(int x, int y, int w, int h);
	void render();
	
	void setFullText(icu::UnicodeString text);
	icu::UnicodeString getFullText();
	
	bool on_mouse_button_event(int button, int action, int mods);
	
	int should_be_h = 0;
	int old_width = -1;
	
	Color* background_color = App::theme.darker_background_color;
private:
	std::mutex positioning;
};