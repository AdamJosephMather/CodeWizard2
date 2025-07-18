#pragma once

#include "linenumbers.h"
#include "textedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "application.h"

class Compare : public Widget {
public:
	Compare(Widget* parent, App::PosFunction positioner);
	
	App::PosFunction POS_FUNC = nullptr;
	
	TextEdit* textedit;
	LineNumbers* line_numbers;
	
	virtual bool on_key_event(int key, int scancode, int action, int mods);
	virtual bool on_char_event(unsigned int keycode);
	virtual bool on_mouse_button_event(int button, int action, int mods);
	
	FileInfo* file1 = nullptr;
	FileInfo* file2 = nullptr;
	
	std::vector<std::pair<int,icu::UnicodeString>> calculateDifferences(const std::vector<icu::UnicodeString>& t1, const std::vector<icu::UnicodeString>& t2);
	
	void position(int x, int y, int w, int h);
	void render();
private:
};
