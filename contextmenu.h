#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "button.h"

class ContextMenu : public Widget {
public:
	ContextMenu(Widget* parent);
	
	void render() override;
	void position(int x, int y, int width, int height) override;
	
	void addToMenu(icu::UnicodeString name, Button::OnClick onclick);
	void clearMenu();
	
//	bool on_mouse_button_event(int button, int action, int mods) override;
//	bool on_mouse_move_event() override;
private:
	std::vector<Button*> buttons = {};
	int maxwidth = 0;
};