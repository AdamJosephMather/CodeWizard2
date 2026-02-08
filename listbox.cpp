#include "listbox.h"
#include "application.h"
#include "text_renderer.h"

ListBox::ListBox(Widget* parent, App::PosFunction pf) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("ListBox");
	pFunc = pf;
}

void ListBox::fillElementalPositions() {	
	elementalPositions.clear();
	
	int th = TextRenderer::get_text_height()+App::text_padding*2;
	
	int y = t_y;
	for (int indx = scrolled_to; indx < fmin(elements.size(), scrolled_to+toshow); indx++) {
		elementalPositions.push_back({t_x, y, t_w, th, indx});
		y += th;
	}
}

void ListBox::render() {
	if (!is_visible || !is_visible_layered) {
		return;
	}
	
	elementalPositions.clear();
	
	if (rounded) {
		App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.extras_background_color);
	}else{
		App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	}
	
	int th = TextRenderer::get_text_height()+App::text_padding*2;
	
	int maxlen = floor(t_w/TextRenderer::get_text_width(1));
	int y = t_y;
	Color* textCol;
	
	for (int indx = scrolled_to; indx < fmin(elements.size(), scrolled_to+toshow); indx++) {
		if (selected_id == indx) {
			textCol = App::theme.darker_background_color;
			if (rounded) {
				App::DrawRoundedRect(t_x, y, t_w, th, App::text_padding, App::theme.main_text_color);
				App::DrawRoundBorder(t_x, y, t_w, th, App::theme.border, 5, App::text_padding);
			}else{
				App::DrawRect(t_x, y, t_w, th, App::theme.main_text_color);
				App::DrawBorder(t_x, y, t_w, th, App::theme.border);
			}
		}else{
			textCol = App::theme.main_text_color;
		}
		
		elementalPositions.push_back({t_x, y, t_w, th, indx});
		
		TextRenderer::draw_text(t_x+App::text_padding+1, y+App::text_padding, elements[indx].tempSubStringBetween(0, maxlen), textCol);
		y += th;
	}
	
	if (!rounded) {
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}else{
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}
}

void ListBox::setElements(std::vector<icu::UnicodeString> el) {
	elements = el;
	selected_id = 0;
	scrolled_to = 0;
}

void ListBox::moveDown() {
	selected_id ++;
	if (selected_id >= elements.size()) {
		selected_id = 0;
	}
	if (selected_id-toshow >= scrolled_to) {
		scrolled_to = selected_id-toshow+1;
	}else if (selected_id < scrolled_to) {
		scrolled_to = selected_id;
	}
}

void ListBox::moveUp() {
	selected_id --;
	if (selected_id < 0) {
		selected_id = elements.size()-1;
		if (selected_id < 0) {
			selected_id = 0;
		}
	}
	if (selected_id-toshow >= scrolled_to) {
		scrolled_to = selected_id-toshow+1;
	}else if (selected_id < scrolled_to) {
		scrolled_to = selected_id;
	}
}

void ListBox::position(int x, int y, int w, int h) {
	if (!is_visible || !is_visible_layered) {
		return;
	}
	
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = (TextRenderer::get_text_height()+App::text_padding*2)*toshow;
	
	pFunc(this);
	
	const int mx = App::mouseX;
	const int my = App::mouseY;
	if (mx >= t_x && mx <= t_x+t_w && my >= t_y && my <= t_y+t_h) {
		App::expectedCursorType = 3;
	}
}

bool ListBox::on_scroll_event(double xchange, double ychange) {
	if (!is_visible || !is_visible_layered) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) {
		return false;
	}
	
	tryingtoscrollby += ychange*2;
	
	if (abs(tryingtoscrollby) > 1) {
		int scrollby = floor(tryingtoscrollby);
		scrolled_to += scrollby;
		tryingtoscrollby -= scrollby;
	}
	
	int el_sz = elements.size();
	
	if (scrolled_to > el_sz-toshow) {
		scrolled_to = elements.size()-toshow;
	}
	if (scrolled_to < 0) {
		scrolled_to = 0;
	}
	
	fillElementalPositions();
	
	for (int thisone = 0; thisone < elementalPositions.size(); thisone++) {
		auto ep = elementalPositions[thisone];
		
		if (mx >= ep[0] && mx <= ep[0]+ep[2] && my >= ep[1] && my <= ep[1]+ep[3]) {
			selected_id = ep[4];
		}
	}
	
	return true;
}

bool ListBox::on_mouse_button_event(int button, int action, int mods) {
	if (!ONCLICK || !is_visible || !is_visible_layered || action != GLFW_PRESS) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) {
		return false;
	}
	
	for (auto ep : elementalPositions) {
		if (mx >= ep[0] && mx <= ep[0]+ep[2] && my >= ep[1] && my <= ep[1]+ep[3]) {
			ONCLICK(this, ep[4]);
			return true;
		}
	}
	
	return false;
}

bool ListBox::on_mouse_move_event() {
	if (!is_visible || !is_visible_layered) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) {
		return false;
	}
	
	for (int thisone = 0; thisone < elementalPositions.size(); thisone++) {
		auto ep = elementalPositions[thisone];
		
		if (mx >= ep[0] && mx <= ep[0]+ep[2] && my >= ep[1] && my <= ep[1]+ep[3]) {
			selected_id = ep[4];
		}
	}
	
	return true;
}