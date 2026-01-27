#include "button.h"
#include "application.h"
#include "text_renderer.h"

Button::Button(Widget *parent, icu::UnicodeString text, Positioner positioner, OnClick onclick) : Widget(parent) {
	BUTTON_LABEL = text;
	POSITIONER = positioner;
	ONCLICK = onclick;
	
	transparent = false;
	window_button = false;
	hovered = false;
	rounded = false;
	
	background_color = App::theme.darker_background_color;
	
	id = icu::UnicodeString::fromUTF8("Button - ") + text;
}

void Button::position(int x, int y, int width, int height) {
	if (window_button) {
		t_w = TextRenderer::get_text_height() * 2.5;
		t_h = TextRenderer::get_text_height() + App::text_padding*2;
	}else{
		t_w = TextRenderer::get_text_width(BUTTON_LABEL.length()) + App::text_padding*2;
		t_h = TextRenderer::get_text_height() + App::text_padding*2;
	}
	
	if (hovered) {
		App::expectedCursorType = 3;
	}
	
	POSITIONER(this, x, y, width, height, t_w, t_h);
}

bool Button::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible) {
		return false;
	}
	
	if (!parent) {
		return false;
	}
	
	if (!hovered) {
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

bool Button::on_mouse_move_event() {
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

void Button::render() {
	if (!is_visible) {
		return;
	}
	
	if (window_button) {
		if (hovered) {
			if (rounded) {
				App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, background_color);
				
				if (border){
					App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
				}
			}else{
				App::DrawRect(t_x, t_y, t_w, t_h, background_color);
				
				if (border){
					App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
				}
			}
		}
	}else{
		Color* fillcolor = background_color;
		if (hovered) {
			fillcolor = App::theme.hover_background_color;
		}
		
		if (rounded) {
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, fillcolor, border);
			if (border) {
				App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
			}
		}else{
			App::DrawRect(t_x, t_y, t_w, t_h, fillcolor);
			if (border) {
				App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
			}
		}
	}
	
	int x;
	if (alignLeft) {
		x = t_x+App::text_padding;
	}else{
		x = t_x+t_w/2-TextRenderer::get_text_width(BUTTON_LABEL.length())/2;
	}
	
	int y = t_y+t_h/2-TextRenderer::get_text_height()/2;
	TextRenderer::draw_text(x, y, BUTTON_LABEL, App::theme.main_text_color);
	
	Widget::render();
}
