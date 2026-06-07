#include "contextmenu.h"
#include "application.h"

ContextMenu::ContextMenu(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("contextmenu");
}

void ContextMenu::render() {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return;
	}
	
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding+sqrt(App::text_padding), App::theme.extras_background_color, true);
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
	
	t_x = x_loc;
	t_y = y_loc;
	
	maxwidth = 0;
	Widget::position(t_x, t_y, t_w, t_h);
	
	int bx = t_x+App::text_padding+App::border_width;
	int by = t_y+App::text_padding+App::border_width;
	
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
		
		// hate to do this a second time but we need to because button isn't sized until it is. So.
		if (App::mouseX >= b->t_x && App::mouseX <= b->t_x+b->t_w && App::mouseY >= b->t_y && App::mouseY <= b->t_y+b->t_h) {
			App::expectedCursorType = 3;
		}
	}
	
	t_w = maxwidth+App::text_padding*2+App::border_width*2;
	t_h = (App::border_width+by)-t_y;
	
	if (App::expectedCursorType == -1 && cursor_in_this) {
		App::expectedCursorType = 0;
	}
}

void ContextMenu::addToMenu(icu::UnicodeString name, Button::OnClick onclick) {
	Button* b = new Button(this, name, [&](Widget *btn, int x, int y, int av_width, int av_height, int w, int h) {
		maxwidth = std::max(maxwidth, w);
	}, onclick);
	b->border = true;
	b->rounded = true;
	b->alignLeft = true;
	b->isContext = true;
	b->background_color = App::theme.main_text_color;
	
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
		
		App::deleteWidget(b);
	}
	buttons.clear();
}

bool ContextMenu::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return false;
	}
	
	if (!cursor_in_this) {
		return false;
	}
	
	Widget::on_mouse_button_event(button, action, mods);
	return true;
}