#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>

struct TreeStructure {
	icu::UnicodeString name;
	std::string path;
	bool is_folder = false;
	
	std::vector<TreeStructure*> childrenFolders = {};
	std::vector<TreeStructure*> childrenFiles = {};
};

struct Visual {
	int x;
	int y;
	int w;
	int h;
	
	icu::UnicodeString name;
	TreeStructure* ts = nullptr;
};

class FileTree : public Widget {
public:
	FileTree(Widget* parent);
	
	TreeStructure* root = nullptr;
	std::vector<Visual> toRender = {};
	
	std::vector<std::string> openpaths = {};
	
	double scrolled_to_vert = 0; // measured in lines
	double scrolled_to_horz = 0;
	double max_scroll_vert = 0;
	double max_scroll_horz = 0;
	
	int elHeighto = 0;
	
	
//	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
//	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	
	void position(int x, int y, int w, int h);
	void render();
	void save();
	
	void fillOutTree(TreeStructure* el);
	void deleteTree(TreeStructure* el);
	
	double createVisuals(double pos, double depth, TreeStructure* el);
	void request_close(Widget::close_callback_type cllbck);
private:
};