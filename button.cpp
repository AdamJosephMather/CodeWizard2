#include "button.h"
#include "application.h"
#include "text_renderer.h"

int radius = 6;

Button::Button(Widget *parent, icu::UnicodeString text, Positioner positioner, OnClick onclick) : Widget(parent) {
	BUTTON_LABEL = text;
	POSITIONER = positioner;
	ONCLICK = onclick;
	
	transparent = false;
	window_button = false;
	hovered = false;
	
	id = icu::UnicodeString::fromUTF8("Button - ") + text;
}

void Button::position(int x, int y, int width, int height) {
	if (window_button) {
		t_w = TextRenderer::get_text_height() * 2.5;
		t_h = TextRenderer::get_text_height() + 10;
	}else{
		t_w = TextRenderer::get_text_width(BUTTON_LABEL.length()) + padding_button*2;
		t_h = TextRenderer::get_text_height() + padding_button*2;
	}
	
	POSITIONER(this, x, y, width, height, t_w, t_h);
}

bool Button::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible) {
		return false;
	}
	
	if (!hovered) {
		return false;
	}
	
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		ONCLICK(this);
		return true;
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
			App::DrawRect(t_x, t_y, t_w, t_h, App::theme.hover_background_color);
		}
	}else{
		if (hovered) {
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, radius, App::theme.hover_background_color);
		}else if (!transparent){
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, radius, App::theme.darker_background_color);
		}
	}
	
	auto HIGHLIGHT = AllOneColor(App::theme.main_text_color, BUTTON_LABEL.length());
	
	if (!window_button){
		TextRenderer::draw_text(t_x+padding_button, t_y+padding_button, BUTTON_LABEL, HIGHLIGHT);
	}else{
		int x = t_x+t_w/2-TextRenderer::get_text_width(BUTTON_LABEL.length())/2;
		int y = t_y+t_h/2-TextRenderer::get_text_height()/2;
		TextRenderer::draw_text(x, y, BUTTON_LABEL, HIGHLIGHT);
	}
	
	Widget::render();
}
