#include "contextmenu.h"
#include "application.h"

ContextMenu::ContextMenu(Widget* parent) : Widget(parent) { 
	id = icu::UnicodeString::fromUTF8("contextmenu");
}

void ContextMenu::render() {
	if (!is_visible) {
		return;
	}
	
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, 5.0, App::theme.darker_background_color);
	App::DrawRoundedRect(t_x+1, t_y+1, t_w-2, t_h-2, 4.9, App::theme.darker_background_color);
	
	Widget::render();
}

void ContextMenu::position(int x, int y, int width, int height) {
	if (!is_visible) {
		return;
	}
	
	maxwidth = 0;
	Widget::position(x, y, width, height);
	
	int bx = x+App::text_padding;
	int by = y+App::text_padding;
	
	for (int bi = 0; bi < buttons.size(); bi++) {
		Button* b = buttons[bi];
		
		b->t_x = bx;
		b->t_y = by;
		b->t_w = maxwidth;
		
		by += b->t_h+App::text_padding;
	}
	
	t_x = x;
	t_y = y;
	t_w = maxwidth+App::text_padding*2;
	t_h = height+App::text_padding*(buttons.size()+1);
}

void ContextMenu::addToMenu(icu::UnicodeString name, Button::OnClick onclick) {
	Button* b = new Button(this, name, [&](Widget *btn, int x, int y, int av_width, int av_height, int w, int h){
		maxwidth = max(maxwidth, w);
	}, [&](Widget* w){
		onclick(b);
	});
	b->border = true;
	
	buttons.push_back(b);
}

void ContextMenu::clearMenu() {
	for (Button* b : buttons) {
		b->request_close([](Widget* w){
			delete w;
		});
	}
	buttons.clear();
}