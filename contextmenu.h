#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "button.h"

class ContextMenu : public Widget {
public:
	ContextMenu(Widget* parent);
	
	void render() override;
	void position(int x, int y, int width, int height) override;
	
	void addToMenu(MST::MonoString name, Button::OnClick onclick);
	void addSeparaterToMenu();
	void clearMenu();
	void recalcButtonTexts();
	
	int x_loc = 0;
	int y_loc = 0;
	bool is_visible_2 = false; // currently visible
	bool is_visible_3 = false; // visible ever (ie, use context menu)
	
	bool on_mouse_button_event(int button, int action, int mods) override;
private:
	std::vector<Button*> buttons = {};
	std::vector<MST::MonoString> buttonTexts = {};
	int maxwidth = 0;
	int runningypos = 0;
};