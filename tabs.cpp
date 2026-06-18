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

void Tabs::nextTab() {
	for (int i = 0; i < tabs_list.size(); i++) {
		if (tabs_list[i].id == selected_id) {
			if (i+1 < tabs_list.size()) {
				selected_id = tabs_list[i+1].id;
				tab_clicked_callback(tabs_list[i+1]);
			}else{
				selected_id = tabs_list[0].id;
				tab_clicked_callback(tabs_list[0]);
			}
			return;
		}
	}
}

void Tabs::removeTab(int id, int nexttake) {
	if (id == selected_id) {
		int nx = 0;
		if (nexttake != -1) {
			nx = nexttake;
		}else{
			for (int j = 0; j < tabs_list.size(); j++) {
				if (tabs_list[j].id == id) {
					nx = j+1;
					break;
				}
			}
		}
		
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
	
	
	for (int i = 0; i < tabs_list.size(); i++) {
		if (tabs_list[i].id == id) {
			if (erasing_tab){
				erasing_tab(tabs_list[i]);
			}
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
	int close_size = TextRenderer::get_text_height()*.6;
	int tabheight = t_h-1;
	int close_offset_y = tabheight/2-close_size/2;
	int close_offset_x = end_len/2-close_size/2-1;
	
	if (end_len - 2*close_offset_x >= close_size) {
		close_offset_x -= 1;
		close_size += 1;
		close_offset_y -= 1;
	}
	
	for (int indx = 0; indx < tabs_list.size(); indx ++) {
		auto info = tabs_list[indx];
		int tab_width = TextRenderer::get_text_width(info.title.length()) + App::text_padding*2;
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
			
			int addextra = (int)has_close_button * end_len;
			
			Color* c;
			Color* tc;
			Color* bc;
			if (selected_id == info.id) {
				c = App::theme.main_text_color;
				tc = App::theme.darker_background_color;
				bc = App::theme.main_text_color;
			}else if (hovering.indx == indx && hovering.body) {
				c = App::theme.hover_background_color;
				tc = App::theme.main_text_color;
				bc = App::theme.main_text_color;
			}else {
				c = App::theme.extras_background_color;
				
				tc = App::theme.main_text_color;
				bc = App::theme.border;
			}
			
			if (rounded) {
				App::DrawRoundedRect(lc.start, t_y, tab_width+addextra, tabheight, App::text_padding, c);
			}else{
				App::DrawRect(lc.start, t_y, tab_width, tabheight, c);
			}
			
			if (has_close_button){
				if (hovering.indx == indx && !hovering.body) {
					c = App::theme.del_diff;
				}else{
					c = App::theme.extras_background_color;
				}
				
				if (rounded) {
					App::DrawRoundedRect(lc.end, t_y+2, end_len-2, tabheight-4, App::text_padding-App::SQRT_2, c);
				}else{
					App::DrawRect(lc.end, t_y, end_len, tabheight, c);
				}
				
				App::DrawX(lc.end+close_offset_x, t_y+close_offset_y, close_size, close_size, 2, App::theme.main_text_color);
			}
			
			if (rounded) {
				App::DrawRoundBorder(lc.start, t_y, tab_width+addextra, tabheight, bc, 5, App::text_padding);
			}else{
				App::DrawBorder(lc.start, t_y, tab_width+addextra, tabheight, bc);
			}
			
			TextRenderer::draw_text(lc.start+App::text_padding, t_y+text_height, info.title, tc);
		}
		
		tab_screen_loc.push_back(lc);
		
		curx = newx+App::text_padding;
		
		if (has_close_button) {
			curx += end_len;
		}
	}
	
	int add_loc = t_x+curx-scrolled_to;
	
	
	bool hvrngtab = hoveringNewTab();
	screen_add_x = -1;
	screen_add_endx = -1;
	
	if (can_add_new) {
		if (add_loc < t_w+t_x){
			Color* c;
			Color* bc;
			
			if (hvrngtab) {
				c = App::theme.hover_background_color;
				bc = App::theme.main_text_color;
			}else{
				c = App::theme.extras_background_color;
				bc = App::theme.border;
			}
			
			if (rounded) {
				App::DrawRoundedRect(add_loc, t_y, end_len-2, tabheight, App::text_padding, c);
				App::DrawRoundBorder(add_loc, t_y, end_len-2, tabheight, bc, 5, App::text_padding);
			}else {
				App::DrawRect(add_loc, t_y, end_len-2, tabheight, c);
				App::DrawBorder(add_loc, t_y, end_len-2, tabheight, bc);
			}
			
			App::DrawPlus(add_loc+close_offset_x, t_y+close_offset_y, close_size, close_size, 2, App::theme.main_text_color);
		}
		
		screen_add_x = add_loc;
		screen_add_endx = add_loc+end_len-2;
		
		curx += end_len+App::text_padding;
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
	
	if (!cursor_in_this) {
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
	if (screen_add_x == -1 || screen_add_endx == -1) {
		return false;
	}
	
	int mx = App::mouseX;
	
	if (!cursor_in_this) {
		return false;
	}
	
	if (mx > screen_add_x && mx < screen_add_endx && mx <= t_w+t_x) {
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
	
	if (!cursor_in_this) {
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
			removeTab(tabs_list[ht.indx].id, ht.indx+1);
		}
		return true;
	}
	
	return false;
}

bool Tabs::on_scroll_event(double xc, double yc) {
	if (!is_visible){
		return false;
	}
	
	if (!cursor_in_this) {
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
		t_h = TextRenderer::get_text_height()*1.5+1;
	}
	
	t_x = x;
	t_y = y;
	
	if (POSITIONER) {
		POSITIONER(this);
	}
	
	Widget::position(t_x, t_y, t_w, t_h);
	
	if (hovering.indx != -1 || hoveringNewTab()) {
		App::expectedCursorType = 3;
	}
}

void Tabs::updateTab(TabInfo info) {
	for (int i = 0; i < tabs_list.size(); i++) {
		if (tabs_list[i].id == info.id) {
			tabs_list[i] = info;
			return;
		}
	}
}
