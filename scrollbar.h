#pragma once

#include <GLFW/glfw3.h>
#include <functional>
#include "widget.h"

class Scrollbar : public Widget {
public:
	// scroll start, scroll end
	using ScrollInfoFunc = std::function<std::vector<double>()>;
	using ScrollFunc = std::function<void(double val)>;
	
	Scrollbar(Widget* parent);
	
	bool horizontal = false;
	ScrollInfoFunc getScrollInfo = nullptr;
	ScrollFunc scrollTo = nullptr;
	
	void render() override;
	void position(int x, int y, int width, int height) override;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
	bool on_mouse_move_event() override;
	
	void setErrors(std::vector<double> r, std::vector<double> o, std::vector<double> b);
private:
	bool hovering = false;
	double start = 0;
	double end = 0;
	bool holding = false;
	double addToTheEnd = 0.0;
	int offset = 0;
	
	int bar_width = 0;
	int r_x = 0;
	int r_y = 0;
	int r_w = 0;
	int r_h = 0;
	
	std::vector<double> red = {};
	std::vector<double> orange = {};
	std::vector<double> blue = {};
};