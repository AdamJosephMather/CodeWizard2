#include "toast.h"
#include "application.h"
#include "text_renderer.h"

Toast::Toast(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Toast");
}

void Toast::position(int x, int y, int width, int height) {
	const int pad = 10;
	t_w = width/4;
	t_h = height/12;
	t_x = x+width-t_w-pad;
	t_y = y+pad;
	
	if (time != -1) {
		if (glfwGetTime()-time > 2) {
			time = -1;
			App::time_till_regular += 2;
		}
	}
}

void Toast::render() {
	if (!is_visible) {
		return;
	}
	
	const int radius = 10;
	
	if (time != -1) {
		App::DrawRoundedRect(t_x, t_y, t_w, t_h, radius, App::theme.hover_background_color);
		TextRenderer::draw_text(t_x+radius, t_y+t_h/2-TextRenderer::get_text_height()/2, text, App::theme.main_text_color);
	}
	
	Widget::render();
}

void Toast::displayMessage(icu::UnicodeString displ_text) {
	text = displ_text;
	time = glfwGetTime();
	App::time_till_regular += 2;
}