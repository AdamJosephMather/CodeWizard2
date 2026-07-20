#pragma once

#include "markdown_utils.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class Label : public Widget {
public:
	Label(Widget* parent);
	
	bool border = true;
	bool rect = true;
	
	MST::MonoString fulltext;
	std::vector<MarkdownSpan>        colorSpans = {};
	std::vector<MST::MonoString>  drawlines;
	std::vector<std::vector<Color*>> drawColors;
	bool handlingColor = false;
	
	App::PosFunction POSITIONER = nullptr;
	
	void position(int x, int y, int w, int h);
	void render();
	
	void setFullText(MST::MonoString text, std::vector<MarkdownSpan> spans = {});
	MST::MonoString getFullText();
	
	bool on_mouse_button_event(int button, int action, int mods);
	
	int should_be_h = 0;
	int old_width = -1;
	
	Color* background_color = App::theme.darker_background_color;
	
	bool rounded = false;
private:
	std::mutex positioning;
};