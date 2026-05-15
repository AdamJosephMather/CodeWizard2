#pragma once

#include "button.h"
#include "textedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class BrokenStateMenu : public Widget {
public:
	BrokenStateMenu(Widget* parent, std::string firsttext, std::string secondtext, std::string query);
	
	Button* firstbutton;
	Button* secondbutton;
	
	char char_1 = ' ';
	char char_2 = ' ';
	
	App::VoidFunction first_callback = nullptr;
	App::VoidFunction second_callback = nullptr;
	
	std::string Q = "";
	
	bool on_key_event(int key, int scancode, int action, int mods) override;
	bool on_char_event(unsigned int keycode) override;
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
//	void position(int x, int y, int w, int h) override;
	void render() override;
private:
};
