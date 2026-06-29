#include "button.h"
#include "application.h"
#include "text_renderer.h"

Button::Button(Widget *parent, MST::MonoString text, Positioner positioner, OnClick onclick) : Widget(parent) {
	BUTTON_LABEL = text;
	POSITIONER = positioner;
	ONCLICK = onclick;
	
	window_button = false;
	rounded = false;
	
	background_color = App::theme.darker_background_color;
	background_color_hover = App::theme.hover_background_color;
	text_color = App::theme.main_text_color;
	text_color_hover = App::theme.main_text_color;
	border_color = App::theme.border;
	border_color_hover = App::theme.active_color;
	
	id = MST::toMonoString("Button - ") + text;
}

void Button::position(int x, int y, int width, int height) {
	if (window_button) {
		t_w = TextRenderer::get_text_height() * 2.5;
		t_h = TextRenderer::get_text_height() + App::text_padding*2;
	}else{
		t_w = TextRenderer::get_text_width(BUTTON_LABEL.length) + App::text_padding*2;
		t_h = TextRenderer::get_text_height() + App::text_padding*2;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	POSITIONER(this, x, y, width, height, t_w, t_h);
	
	if (t_x <= mx && t_x+t_w >= mx && t_y <= my && t_y+t_h >= my && (!parent || parent->cursor_in_this)) {
		App::expectedCursorType = 3;
	}
}

bool Button::on_mouse_button_event(int button, int action, int mods) {
	if (!parent || !cursor_in_this || !is_visible) {
		return false;
	}
	
	if (execute_on_down) {
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
			ONCLICK(this);
			return true;
		}
	}else {
		if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
			ONCLICK(this);
			return true;
		}
	}
	
	return false;
}

void Button::render() {
	if (!is_visible) {
		return;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	bool hovered = t_x <= mx && t_x+t_w >= mx && t_y <= my && t_y+t_h >= my;
	
	Color* textColor = text_color;
	Color* backColor = background_color;
	Color* borderColor = border_color;
	
	if (hovered) {
		textColor = text_color_hover;
		backColor = background_color_hover;
		borderColor = border_color_hover;
	}
	
	if (backColor != nullptr) {
		if (rounded) {
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, backColor, borderColor != nullptr);
		}else{
			App::DrawRect(t_x, t_y, t_w, t_h, backColor);
		}
	}
	
	if (borderColor) {
		if (rounded) {
			App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderColor, 5, App::text_padding);
		}else{
			App::DrawBorder(t_x, t_y, t_w, t_h, borderColor);
		}
	}
	
	if (textColor != nullptr) {
		if (text_special == 0) {
			int x;
			if (alignLeft) {
				x = t_x+App::text_padding;
			}else{
				x = t_x+t_w/2-TextRenderer::get_text_width(BUTTON_LABEL.length)/2;
			}
			
			int y = t_y+t_h/2-TextRenderer::get_text_height()/2;
			TextRenderer::draw_text(x, y, BUTTON_LABEL, textColor);
		}else {
			int wdth = TextRenderer::get_text_height()*.6;
			
			int x;
			if (alignLeft) {
				x = t_x+App::text_padding;
			}else{
				x = t_x+t_w/2-wdth/2;
			}
			
			int y = t_y+t_h/2-wdth/2;
			
			if (text_special == 1) {
				App::DrawX(x, y, wdth, wdth, 2, textColor);
			}else if (text_special == 2) {
				App::DrawPlus(x, y, wdth, wdth, 2, textColor);
			}else if (text_special == 3) {
				App::DrawMinus(x, y, wdth, wdth, 2, textColor);
			}else if (text_special == 4) {
				App::DrawSquare(x, y, wdth, wdth, 2, textColor);
			}
		}
	}
	
	Widget::render();
}
