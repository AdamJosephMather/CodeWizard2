#include "listbox.h"
#include "application.h"
#include "text_renderer.h"

ListBox::ListBox(Widget* parent, App::PosFunction pf) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("ListBox");
	pFunc = pf;
}

void ListBox::render() {
	if (!is_visible || !is_visible_layered) {
		return;
	}
	
	elementalPositions.clear();
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	int th = TextRenderer::get_text_height()+10;
	
	int maxlen = floor(t_w/TextRenderer::get_text_width(1));
	int y = t_y;
	
	for (int indx = scrolled_to; indx < fmin(elements.size(), scrolled_to+toshow); indx++) {
		if (selected_id == indx) {
			App::DrawRect(t_x, y, t_w, th, App::theme.hover_background_color);
		}
		
		elementalPositions.push_back({t_x, y, t_w, th, indx});
		
		TextRenderer::draw_text(t_x+5, y+5, elements[indx].tempSubStringBetween(0, maxlen), App::theme.main_text_color);
		y += th;
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
	t_h = (TextRenderer::get_text_height()+10)*toshow;
	
	pFunc(this);
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
