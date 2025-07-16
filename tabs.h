#pragma once

#include "widget.h"
#include <GLFW/glfw3.h>

struct TabInfo {
	int xloc;
	icu::UnicodeString title;
	int id;
};

struct tagloc {
	int start;
	int end;
	int end_end;
};

struct hit {
	bool body = true;
	int indx = -1;
};

class Tabs : public Widget {
public:
	Tabs(Widget* parent);
	
	using TCC = std::function<void(TabInfo info)>;
	using ATCC = std::function<void()>;
	using ANT = std::function<void()>;
	
	std::vector<TabInfo> tabs_list;
	double scrolled_to = 0;
	int selected_id = 0;
	int max_scroll = 0;
	std::vector<tagloc> tab_screen_loc;
	TCC tab_clicked_callback = nullptr;
	ATCC all_tabs_closed_callback = nullptr;
	ATCC add_new_tab_callback = nullptr;
	TCC erasing_tab = nullptr;
	hit hovering;
	bool has_close_button = true;
	bool can_add_new = true;
	int screen_add_x = 0;
	int screen_add_y = 0;
	
	void addTab(TabInfo info);
	void removeTab(int id);
	hit hoveringTab();
	bool hoveringNewTab();
	
//	bool on_key_event(int key, int scancode, int action, int mods);
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_mouse_move_event();
	bool on_scroll_event(double xchange, double ychange);
	void updateTab(TabInfo info);
	
	void position(int x, int y, int w, int h);
	void render();
	
	
private:
};