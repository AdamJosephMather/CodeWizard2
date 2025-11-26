#include "scrollbar.h"
#include "application.h"
#include "text_renderer.h"

Scrollbar::Scrollbar(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("scrollbar");
}

void Scrollbar::render() {
	if (!is_visible || !getScrollInfo || start >= end || (start == 0 && end == 1)) {
		return;
	}
	
	App::DrawRoundedRect(r_x+1, r_y+1, r_w-2, r_h-2, (bar_width-2)/2.0, App::theme.main_text_color);
}

void Scrollbar::position(int x, int y, int width, int height) {
	if (!is_visible || getScrollInfo == nullptr) {
		return;
	}
	
	std::vector<double> scrollInfo = getScrollInfo();
	start = scrollInfo[0];
	end = scrollInfo[1];
	
	if (end > 1) {
		end = 1;
	}if (start < 0) {
		start = 0;
	}
	
	if (start >= end || (start == 0 && end == 1)) {
		return;
	}
	
	bar_width = TextRenderer::get_text_width(1);
	int far_right = parent->t_x+parent->t_w;
	int far_bottom = parent->t_y+parent->t_h;
	
	if (horizontal) {
		int width = (double)parent->t_w * (end-start);
		int start_x = parent->t_x + (double)parent->t_w*start;
		
		r_x = start_x;
		r_y = far_bottom-bar_width;
		r_w = width;
		r_h = bar_width;
		
		t_x = parent->t_x;
		t_w = parent->t_w;
		
		t_y = r_y;
		t_h = r_h;
	}else{
		int height = (double)parent->t_h * (end-start);
		int start_y = parent->t_y + (double)parent->t_h*start;
		
		r_x = far_right-bar_width;
		r_y = start_y;
		r_w = bar_width;
		r_h = height;
		
		t_x = r_x;
		t_w = r_x;
		
		t_y = parent->t_y;
		t_h = parent->t_h;
	}
}

bool Scrollbar::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible || !getScrollInfo || start >= end || end >= 1) {
		return false;
	}
	
	if (button != GLFW_MOUSE_BUTTON_LEFT) {
		return false;
	}
	
	if (scrollTo == nullptr) {
		return false;
	}
	
	if (action == GLFW_RELEASE && holding) {
		holding = false;
		return true;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) { return false; }
	
	if (action == GLFW_PRESS) {
		if (mx < r_x || mx > r_x+r_w || my < r_y || my > r_y+r_h) {
			offset = 0;
			if (horizontal) {
				scrollTo((double)(mx-t_x)/(double)t_w);
			}else{
				scrollTo((double)(my-t_y)/(double)t_h);
			}
		}else{
			if (horizontal) {
				offset = r_x-mx;
			}else{
				offset = r_y-my;
			}
		}
		
		holding = true;
		return true;
	}
	
	return false;
}

bool Scrollbar::on_mouse_move_event() {
	if (!is_visible || !getScrollInfo || start >= end || end >= 1) {
		return false;
	}
	
	if (scrollTo == nullptr || !holding) {
		return false;
	}
	
	int state = glfwGetMouseButton(App::window, GLFW_MOUSE_BUTTON_LEFT);
	if (state != GLFW_PRESS) { holding = false; return false; }
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (horizontal) {
		double newvalue = mx+offset;
		if (newvalue < t_x) {
			newvalue = t_x;
		}else if (newvalue > t_x+t_w-r_w) {
			newvalue = t_x+t_w-r_w;
		}
		
		scrollTo((double)(newvalue-t_x)/(double)t_w);
	}else{
		double newvalue = my+offset;
		
		if (newvalue < t_y) {
			newvalue = t_y;
		}else if (newvalue > t_y+t_h-r_h) {
			newvalue = t_y+t_h-r_h;
		}
		
		scrollTo((double)(newvalue-t_y)/(double)t_h);
	}
	
	return true;
}