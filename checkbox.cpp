#include "checkbox.h"
#include "application.h"
#include "text_renderer.h"

CheckBox::CheckBox(Widget *parent, CheckPositioner positioner, CheckOnClick onclick) : Widget(parent) {
	POSITIONER = positioner;
	ONCLICK = onclick;
	
	hovered = false;
	
	id = icu::UnicodeString::fromUTF8("CheckBox");
}

void CheckBox::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	POSITIONER(this, t_x, t_y, t_w, t_h);
}

bool CheckBox::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible) {
		return false;
	}
	
	if (!hovered) {
		return false;
	}
	
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		is_checked = !is_checked;
		if (ONCLICK) {
			ONCLICK(this);
		}
		return true;
	}
	
	return false;
}

bool CheckBox::on_mouse_move_event() {
	if (!is_visible) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (t_x <= mx && t_x+t_w >= mx && t_y <= my && t_y+t_h >= my) {
		hovered = true;
	}else{
		hovered = false;
	}
	
	Widget::on_mouse_move_event();
	
	return false;
}

void CheckBox::render() {
	if (!is_visible) {
		return;
	}
	
	int p = 4;
	int radius = 5;
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.darker_background_color);
	
	if (is_checked) {
		App::DrawRoundedRect(t_x+p, t_y+p, t_w-2*p, t_h-2*p, radius, App::theme.main_text_color);
	}
	
	Widget::render();
}
