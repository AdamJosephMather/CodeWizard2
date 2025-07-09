#pragma once

#include "helper_types.h"
#include "codeedit.h"
#include "widget.h"
#include <GLFW/glfw3.h>
#include "tabs.h"

class Editor : public Widget {
public:
	Editor(Widget* parent);
	
	Tabs* tab_bar;
	int tabid;
	
	std::unordered_map<int,Widget*> editors;
	
	void fileOpenRequested(FileInfo* f, int lns = -1, int chrs = -1, int ln = -1, int chr = -1);
	void moveto(int lns, int chrs, int ln, int chr);
	
	bool on_key_event(int key, int scancode, int action, int mods);
//	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
//	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void createNew(FileInfo* fn);
	void closeFile(int file_id);
	
	bool is_image(std::string path);
	
	void tabinfoclicked(TabInfo info);
	
	void save();
	Widget* fileOpen(std::string fname);
	
	void render();
	
	icu::UnicodeString getPaletteName();
private:
};