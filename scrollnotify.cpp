#include "scrollnotify.h"
#include "application.h"
#include "text_renderer.h"

ScrollNotify::ScrollNotify(Widget *parent, Positioner positioner) : Widget(parent) {
	POSITIONER = positioner;
	
	clickthrough = true;
	id = icu::UnicodeString::fromUTF8("ScrollNotify - ") + text;
}

void ScrollNotify::position(int x, int y, int width, int height) {
	POSITIONER(this, x, y, width, height);
}

void ScrollNotify::render() {
	if (!is_visible || text.length() == 0) {
		return;
	}
	
	TextRenderer::draw_text(t_x+offset, t_y+App::text_padding, text, App::theme.main_text_color);
	
	offset -= App::text_padding*0.6*App::settings->getValue("anim_speed", 1.0f);
	if (offset < -TextRenderer::get_text_width(text.length())) {
		text = icu::UnicodeString();
	}
	
	App::time_till_regular = 2;
	Widget::render();
}

void ScrollNotify::displayMessage(icu::UnicodeString txt) {
	offset = t_w;
	text = txt;
}