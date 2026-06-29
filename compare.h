#pragma once

#include "button.h"
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
	
	std::vector<std::pair<int,MST::MonoString>> calculateDifferences(const std::vector<MST::MonoString>& t1, const std::vector<MST::MonoString>& t2);
	
	void position(int x, int y, int w, int h);
	void render();
	
	void reload();
	void setOnlyTo(FileInfo* f, int num);
	
	bool rounded = true;
private:
	FileInfo* f1 = nullptr;
	FileInfo* f2 = nullptr;
	
	MST::MonoString clipBoardText1;
	MST::MonoString clipBoardText2;
	
	Button* f1Button = nullptr;
	Button* f2Button = nullptr;
	Button* cb1Button = nullptr;
	Button* cb2Button = nullptr;
};
