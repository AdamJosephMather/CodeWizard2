#pragma once

#include <GLFW/glfw3.h>
#include "textedit.h"
#include "widget.h"

class MathWindow : public Widget {
public:
	MathWindow(Widget* parent);
	
	void render() override;
	
	bool on_mouse_button_event(int button, int action, int mods) override;
private:
	TextEdit* mathInput = nullptr;
	std::vector<icu::UnicodeString> results = {};
	icu::UnicodeString onMouseClick = icu::UnicodeString();
};