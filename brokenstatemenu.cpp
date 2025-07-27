#include "brokenstatemenu.h"
#include "text_renderer.h"

BrokenStateMenu::BrokenStateMenu(Widget* parent, std::string firsttext, std::string secondtext, std::string query) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("BrokenStateMenu");
	Q = query;
	
	firstbutton = new Button(this, icu::UnicodeString::fromUTF8(firsttext), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2-w/2;
		btn->t_y = t_y+50;
	},  [&](Button* btn){
		// onclick
		if (first_callback) {
			first_callback();
		}
	});
	
	secondbutton = new Button(this, icu::UnicodeString::fromUTF8(secondtext), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2-w/2;
		btn->t_y = firstbutton->t_y+firstbutton->t_h+20;
	},  [&](Button* btn){
		// onclick
		if (second_callback) {
			second_callback();
		}
	});
}

void BrokenStateMenu::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.hover_background_color);
	
	auto txt = icu::UnicodeString::fromUTF8(Q);
	TextRenderer::draw_text(t_x+t_w/2-TextRenderer::get_text_width(txt.length())/2, t_y+10, txt, App::theme.main_text_color);
	
	Widget::render();
}

void BrokenStateMenu::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(t_x, t_y, t_w, t_h);
}