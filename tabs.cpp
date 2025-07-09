#include "tabs.h"
#include "text_renderer.h"
#include "application.h"

Tabs::Tabs(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Tabs");
	
	tabs_list.clear();
	tab_screen_loc.clear();
	hovering = hit();
}

void Tabs::addTab(TabInfo info) {
	tabs_list.push_back(info);
}

void Tabs::removeTab(int id) {
	for (int i = 0; i < tabs_list.size(); i++) {
		if (tabs_list[i].id == id) {
			tabs_list.erase(tabs_list.begin() + i);
		}
	}
}

void Tabs::render() {
	tab_screen_loc.clear();
	
	if (tabs_list.empty()) {
		return;
	}
	
	if (scrolled_to > max_scroll) {
		scrolled_to = max_scroll;
	}
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.main_background_color);
	
	int curx = 0;
	
	int start_x = scrolled_to;
	int end_x = scrolled_to+t_w;
	int text_height = (t_h-TextRenderer::get_text_height())/2;
	int end_len = TextRenderer::get_text_width(2);
	int tsx = end_len*0.25;
	
	for (int indx = 0; indx < tabs_list.size(); indx ++) {
		auto info = tabs_list[indx];
		int tab_width = TextRenderer::get_text_width(info.title.length()) + 10;
		int newx = curx + tab_width;
		
		auto lc = tagloc();
		lc.start = -1;
		lc.end = -1;
		
		if (curx <= end_x && newx >= start_x) {
			lc.start = t_x+curx-scrolled_to;
			lc.end = lc.start+tab_width;
			if (has_close_button) {
				lc.end_end = lc.end + end_len;
			}else{
				lc.end_end = lc.end;
			}
			
			if (selected_id == info.id || (hovering.indx == indx && hovering.body)) {
				App::DrawRect(lc.start, t_y, tab_width, t_h, App::theme.hover_background_color);
			}else{
				App::DrawRect(lc.start, t_y, tab_width, t_h, App::theme.extras_background_color);
			}
			
			if (has_close_button){
				if (hovering.indx == indx && !hovering.body) {
					App::DrawRect(lc.end, t_y, end_len, t_h, App::theme.hover_background_color);
				}else{
					App::DrawRect(lc.end, t_y, end_len, t_h, App::theme.extras_background_color);
				}
				
				TextRenderer::draw_text(lc.end+tsx, t_y+text_height, "X", AllOneColor(App::theme.main_text_color, 1));
			}
			
			TextRenderer::draw_text(lc.start+5, t_y+text_height, info.title, AllOneColor(App::theme.main_text_color, info.title.length()));
		}
		
		tab_screen_loc.push_back(lc);
		
		curx = newx+5;
		
		if (has_close_button) {
			curx += end_len;
		}
	}
	
	int add_loc = t_x+curx-scrolled_to;
	
	
	bool hvrngtab = hoveringNewTab();
	screen_add_x = -1;
	screen_add_y = -1;
	
	if (can_add_new) {
		if (add_loc < t_w+t_x){
			if (hvrngtab) {
				App::DrawRect(add_loc, t_y, end_len, t_h, App::theme.hover_background_color);
			}else{
				App::DrawRect(add_loc, t_y, end_len, t_h, App::theme.extras_background_color);
			}
			
			TextRenderer::draw_text(add_loc+tsx, t_y+text_height, "+", AllOneColor(App::theme.main_text_color, 1));
		}
		
		screen_add_x = add_loc;
		screen_add_y = add_loc+end_len;
		
		curx += end_len+5;
	}
	
	int full_width = curx;
	max_scroll = full_width-t_w;
	if (max_scroll < 0) {
		max_scroll = 0;
	}
	
	Widget::render();
}

hit Tabs::hoveringTab(){
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (my < t_y || my > t_y+t_h || mx < t_x || mx > t_x+t_w) {
		return hit();
	}
	
	if (tab_screen_loc.size() != tabs_list.size()) {
		return hit();
	}
	
	for (int indx = 0; indx < tab_screen_loc.size(); indx ++) {
		auto lc = tab_screen_loc[indx];
		
		if (mx >= lc.start && mx <= lc.end) {
			auto ht = hit();
			ht.indx = indx;
			ht.body = true;
			return ht;
		}else if (mx >= lc.end && mx <= lc.end_end) {
			auto ht = hit();
			ht.indx = indx;
			ht.body = false;
			return ht;
			
		}
	}
	
	return hit();
}

bool Tabs::hoveringNewTab() {
	if (screen_add_x == -1 || screen_add_y == -1) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (my < t_y || my > t_y+t_h || mx < t_x || mx > t_x+t_w) {
		return false;
	}
	
	if (mx > screen_add_x && mx < screen_add_y && mx <= t_w+t_x) {
		return true;
	}
	
	return false;
}

bool Tabs::on_mouse_move_event() {
	if (!is_visible){
		return false;
	}
	
	hovering = hoveringTab();
	return false;
}

bool Tabs::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible){
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (my < t_y || my > t_y+t_h || mx < t_x || mx > t_x+t_w) {
		return false;
	}
	
	hit ht = hoveringTab();
	
	if (ht.indx == -1) {
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && can_add_new && hoveringNewTab() && add_new_tab_callback) {
			add_new_tab_callback();
			return true;
		}
		return false;
	}
	
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS){
		if (tab_clicked_callback && ht.body){
			selected_id = tabs_list[ht.indx].id;
			tab_clicked_callback(tabs_list[ht.indx]);
		}else{
			if (tabs_list[ht.indx].id == selected_id) {
				int nx = ht.indx + 1;
				int new_indx = nx;
				if (nx >= tabs_list.size()) {
					nx -= 2;
					if (nx < 0) {
						new_indx = -1;
					}else{
						new_indx = nx;
					}
				}
				
				if (new_indx == -1) {
					selected_id = -1;
				}else{
					selected_id = tabs_list[new_indx].id;
				}
				
				if (tab_clicked_callback && selected_id != -1) {
					tab_clicked_callback(tabs_list[new_indx]);
				}
				
				if (selected_id == -1 && all_tabs_closed_callback) {
					all_tabs_closed_callback();
				}
			}
			
			removeTab(tabs_list[ht.indx].id);
		}
		return true;
	}
	
	return false;
}

bool Tabs::on_scroll_event(double xc, double yc) {
	if (!is_visible){
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (my < t_y || my > t_y+t_h || mx < t_x || mx > t_x+t_w) {
		return false;
	}
	
	scrolled_to += xc*200 + yc*200;
	
	if (scrolled_to > max_scroll) {
		scrolled_to = max_scroll;
	}else if (scrolled_to < 0) {
		scrolled_to = 0;
	}
	return true;
}

void Tabs::position(int x, int y, int w, int h) {
	if (tabs_list.empty()) {
		t_w = 0;
		t_h = 0;
	}else {
		t_w = w;
		t_h = TextRenderer::get_text_height()*1.5;
	}
	
	t_x = x;
	t_y = y;
	
	Widget::position(x, y, w, h);
}

void Tabs::updateTab(TabInfo info) {
	for (int i = 0; i < tabs_list.size(); i++) {
		if (tabs_list[i].id == info.id) {
			tabs_list[i] = info;
			return;
		}
	}
}
