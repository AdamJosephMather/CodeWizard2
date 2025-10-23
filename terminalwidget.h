#pragma once

#include <GLFW/glfw3.h>
#include "terminal.h"
#include "widget.h"

class TerminalWidget : public Widget {
public:
	Terminal* term = nullptr;
	
	TerminalWidget(Widget* parent);
	
	Widget* findTerminal();
	
	bool settingup = true;
	
	int prev_w_cells = 0;
	int prev_h_cells = 0;
	
	void position(int x, int y, int w, int h);
	void render();
	void request_close(close_callback_type callback);
	
	void runCommand(std::string command);
	
	virtual bool on_key_event(int key, int scancode, int action, int mods);
	virtual bool on_char_event(unsigned int keycode);
	virtual bool on_mouse_button_event(int button, int action, int mods);
	virtual bool on_mouse_move_event();
	virtual bool on_scroll_event(double xchange, double ychange);
	
	void run();
private:
	void cell_from_cursor(int& row, int& col);
};