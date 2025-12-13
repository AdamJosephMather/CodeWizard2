#include "contextmenu.h"
#include "application.h"

ContextMenu::ContextMenu(Widget* parent) : Widget(parent) { 
	id = icu::UnicodeString::fromUTF8("contextmenu");
}

void ContextMenu::render() {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return;
	}
	
//	App::DrawRoundedRect(t_x, t_y, t_w, t_h, 6.2, App::theme.border);
//	App::DrawRoundedRect(t_x+App::border_width, t_y+App::border_width, t_w-App::border_width*2, t_h-App::border_width*2, 6.1, App::theme.extras_background_color);
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, 6.2, App::theme.extras_background_color, true);
	Widget::render();
	
	int yc = t_y+App::text_padding+App::border_width;
	
	for (int bi = 0; bi < buttons.size(); bi++) {
		Button* b = buttons[bi];
		
		if (b) {
			App::runWithSKIZ(b->t_x, b->t_y, b->t_w, b->t_h, [&](){
				b->render();
			});
			yc = b->t_h+b->t_y+App::text_padding;
		}else{
			App::DrawRect(t_x+App::text_padding+App::border_width, yc, maxwidth, App::border_width*2, App::theme.main_text_color);
			yc += App::text_padding+App::border_width*2;
		}
	}
}

void ContextMenu::position(int x, int y, int width, int height) {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return;
	}
	
	x = x_loc;
	y = y_loc;
	
	maxwidth = 0;
	Widget::position(x, y, width, height);
	
	int bx = x+App::text_padding+App::border_width;
	int by = y+App::text_padding+App::border_width;
	
	for (int bi = 0; bi < buttons.size(); bi++) {
		Button* b = buttons[bi];
		
		if (!b) {
			by += App::border_width*2+App::text_padding;
			continue;
		}
		
		b->t_x = bx;
		b->t_y = by;
		b->t_w = maxwidth;
		
		by += b->t_h+App::text_padding;
	}
	
	t_x = x;
	t_y = y;
	t_w = maxwidth+App::text_padding*2+App::border_width*2;
	t_h = (App::border_width+by)-y;
	
	const int mx = App::mouseX;
	const int my = App::mouseY;
	
	if (App::expectedCursorType == -1 && mx >= t_x && mx <= t_x+t_w && my >= t_y && my <= t_y+t_h) {
		App::expectedCursorType = 0;
	}
}

void ContextMenu::addToMenu(icu::UnicodeString name, Button::OnClick onclick) {
	Button* b = new Button(this, name, [&](Widget *btn, int x, int y, int av_width, int av_height, int w, int h){
		maxwidth = max(maxwidth, w);
	}, onclick);
	b->border = true;
	b->rounded = true;
	b->alignLeft = true;
	b->background_color = App::theme.extras_background_color;
	
	buttons.push_back(b);
}

void ContextMenu::addSeparaterToMenu() {
	buttons.push_back(nullptr);
}

void ContextMenu::clearMenu() {
	for (Button* b : buttons) {
		if (!b) {
			continue;
		}
		
		b->request_close([](Widget* w){
			delete w;
		});
	}
	buttons.clear();
}

bool ContextMenu::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) {
		return false;
	}
	
	Widget::on_mouse_button_event(button, action, mods);
	return true;
}