#include "contextmenu.h"
#include "application.h"

ContextMenu::ContextMenu(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("contextmenu");
}

void ContextMenu::render() {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return;
	}
	
	float rad = App::text_padding+sqrt(App::text_padding);
	App::DrawRoundedRect(t_x, t_y, t_w, t_h, rad, App::theme.extras_background_color, !cursor_in_this);
	if (cursor_in_this) { // draw our own border
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.active_color, 5, rad);
	}
	
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
			App::DrawRect(t_x+App::text_padding+App::border_width, yc-App::text_padding/2, maxwidth, App::border_width, App::theme.main_text_color);
		}
	}
}

void ContextMenu::position(int x, int y, int width, int height) {
	if (!is_visible || !is_visible_2 || !is_visible_3) {
		return;
	}
	
	t_x = x_loc;
	t_y = y_loc;
	
	if (cursor_in_this) { App::expectedCursorType = 0; }
	
	maxwidth = 0;
	runningypos = t_y+App::text_padding+App::border_width;
	Widget::position(t_x, t_y, t_w, t_h);
	
	if (!buttons.empty()) {
		t_h = runningypos + App::border_width - t_y;
		t_w = maxwidth+App::text_padding*2+App::border_width*2;
	}
}

void ContextMenu::addToMenu(icu::UnicodeString name, Button::OnClick onclick) {
	Button* b = new Button(this, name, [&](Widget *btn, int x, int y, int av_width, int av_height, int w, int h) {
		btn->t_x = t_x+App::text_padding+App::border_width;
		btn->t_y = runningypos;
		btn->t_w = w;
		btn->t_h = h - App::text_padding;
		maxwidth = fmax(maxwidth, w);
		runningypos += btn->t_h + App::text_padding;
	}, onclick);
	
	b->rounded = true;
	b->alignLeft = true;
	b->background_color = nullptr;
	b->background_color_hover = App::theme.hover_background_color;
	b->border_color = nullptr;
	b->border_color_hover = App::theme.active_color;
	
	buttons.push_back(b);
	buttonTexts.push_back(name);
}

void ContextMenu::recalcButtonTexts() {
	int neededTextLen = 0;
	for (auto l : buttonTexts) {
		neededTextLen = fmax(neededTextLen, l.length());
	}
	
	UChar32 spaceChar = U' ';
	
	int ti = 0;
	for (int i = 0; i < buttons.size(); i++) {
		if (buttons[i] == nullptr) {
			continue;
		}
		
		auto prts = splitByChar(buttonTexts[ti], U'\t');
		
		if (prts.size() != 2) {
			icu::UnicodeString newStr = buttonTexts[ti];
			for (int j = buttonTexts[ti].length(); j < neededTextLen; j++) {
				newStr.append(spaceChar);
			}
			buttons[i]->BUTTON_LABEL = newStr;
		}else{
			icu::UnicodeString newStr = prts[0];
			for (int j = buttonTexts[ti].length()-1; j < neededTextLen; j++) {
				newStr.append(spaceChar);
			}
			newStr.append(prts[1]);
			buttons[i]->BUTTON_LABEL = newStr;
		}
		
		ti += 1;
	}
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
	buttonTexts.clear();
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