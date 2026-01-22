#pragma once

#include <GLFW/glfw3.h>
#include "textedit.h"
#include "widget.h"

class MathWindow : public Widget {
public:
	MathWindow(Widget* parent);
	
	void position(int x, int y, int w, int h);
	void render();
private:
	TextEdit* mathInput = nullptr;
	std::vector<icu::UnicodeString> results = {};
};