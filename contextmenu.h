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
	void addSeparaterToMenu();
	void clearMenu();
	
	int x_loc = 0;
	int y_loc = 0;
	bool is_visible_2 = false;
	bool is_visible_3 = false;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
private:
	std::vector<Button*> buttons = {};
	int maxwidth = 0;
};