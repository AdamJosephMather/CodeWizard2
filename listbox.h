#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class ListBox : public Widget {
public:
	ListBox(Widget* parent, App::PosFunction pf);
	
	App::PosFunction pFunc;
	int scrolled_to = 0;
	int toshow = 7;
	bool is_visible_layered = false;
	
	double tryingtoscrollby = 0;
	
	using onclick = std::function<void(Widget*, int)>;
	
	std::vector<std::vector<int>> elementalPositions;
	
	onclick ONCLICK = nullptr;
	
//	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void render();
	
	void setElements(std::vector<icu::UnicodeString> el);
	void moveUp();
	void moveDown();
	
	int selected_id = 0;
	std::vector<icu::UnicodeString> elements;
private:
};
