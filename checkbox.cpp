#include "checkbox.h"
#include "application.h"

CheckBox::CheckBox(Widget *parent, CheckPositioner positioner, CheckOnClick onclick) : Widget(parent) {
	POSITIONER = positioner;
	ONCLICK = onclick;
	
	hovered = false;
	bgcolor = App::theme.darker_background_color;
	
	border_color = App::theme.border;
	border_color_hover = App::theme.active_color;
	
	id = MST::toMonoString("CheckBox");
}

void CheckBox::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	POSITIONER(this, t_x, t_y, t_w, t_h);
	
	const int mx = App::mouseX;
	const int my = App::mouseY;
	
	if (t_x <= mx && t_x+t_w >= mx && t_y <= my && t_y+t_h >= my && (!parent || parent->cursor_in_this)) {
		App::expectedCursorType = 3;
	}
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
	if (!is_visible) return;
	const float radius = App::themeCornerRadius(t_w, t_h);
	App::runWithRoundedClip(t_x, t_y, t_w, t_h, radius, [&]() {

		const int p = std::min(4, std::max(0, std::min(t_w, t_h)/4));

		Color* borderC = border_color;
		Color* backC = bgcolor;
		if (cursor_in_this) {
			borderC = border_color_hover;
			backC = App::theme.hover_background_color;
		}

		if (radius > 0.0f) {
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, radius, backC);
		}else{
			App::DrawRect(t_x, t_y, t_w, t_h, backC);
		}

		if (is_checked && t_w > 2*p && t_h > 2*p) {
			const float innerRadius = std::clamp(radius-p, 0.0f, std::min(t_w-2*p, t_h-2*p)*0.5f);
			if (innerRadius > 0.0f) App::DrawRoundedRect(t_x+p, t_y+p, t_w-2*p, t_h-2*p, innerRadius, App::theme.main_text_color);
			else App::DrawRect(t_x+p, t_y+p, t_w-2*p, t_h-2*p, App::theme.main_text_color);
		}

		Widget::render();

		if (borderC) {
			if (radius > 0.0f) {
				App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, radius);
			}else{
				App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
			}
		}
	});
}