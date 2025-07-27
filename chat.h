#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include "textedit.h"
#include "widget.h"
#include "label.h"

class Chat : public Widget {
public:
	Chat(Widget* parent);
	
	void render();
	void position(int x, int y, int width, int height);
	
//	bool on_mouse_button_event(int button, int action, int mods);
	bool on_scroll_event(double xchange, double ychange);
	bool on_key_event(int key, int scancode, int action, int mods);
	
	double scrolled_to = 0;
	int min_scroll = 0;
	
private:
	TextEdit* querybox = nullptr;
	std::vector<Label*> message_te = {};
	std::vector<bool> from_user = {};
};