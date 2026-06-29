#pragma once

#include "button.h"
#include "label.h"
#include "tabs.h"
#include "widget.h"
#include <GLFW/glfw3.h>

class HelpMenu : public Widget {
public:
	HelpMenu(Widget* parent);
	
	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_char_event(unsigned int codepoint);
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void render();
	
	void setToIndex(int index);
	
	std::vector<std::vector<MST::MonoString>> helpInformation = {};
private:
	Label* label;
	Button* closebutton;
	Tabs* tb;
	std::vector<double> scrolled_to = {};
};