#pragma once

#include <GLFW/glfw3.h>
#include "application.h"

class MyRect : public Widget {
public:	
	MyRect(Widget* parent, App::PosFunction positioner);

	void render();
	void position(int x, int y, int width, int height);
	
	Color* background_color = nullptr;
	App::PosFunction POSITIONER;
};