#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "json.hpp"

class PanelHolder : public Widget {
public:
	PanelHolder(Widget* parent);
	
	void render();
	void position(int x, int y, int width, int height);
	bool hoveringHandle();
	
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
//	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_scroll_event(double xchange, double ychange);
	
	void addRemoveNada(bool adding, bool removing, bool nada);
	
	void addWidget(bool horizontal, bool first, Widget* new_wid);
	
	nlohmann::json saveConfiguration();
	void setState(nlohmann::json state);
	
private:
	float ratio = 1.0f;
	bool hastwo = false;
	bool is_horizontal = false;
	
	int handle_short = 4;
	
	bool has_one_to_add = false;
	bool has_one_to_rem = false;
	bool adding_first_pos = false;
	
	int x_nb = 0;
	int y_nb = 0;
	int w_nb = 0;
	int h_nb = 0;
	
	int c1_x = 0;
	int c1_y = 0;
	int c1_w = 0;
	int c1_h = 0;
	
	int c2_x = 0;
	int c2_y = 0;
	int c2_w = 0;
	int c2_h = 0;
	
	int padding = 3;
	
	bool first_hovered = false;
	bool second_hovered = false;
	
	int xpos_h = 0;
	int ypos_h = 0;
	
	int width_h = 0;
	int height_h = 0;
	
	bool dragging_handle = false;
};
