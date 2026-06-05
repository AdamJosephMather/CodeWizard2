#include "titlebar.h"

#include "button.h"
#include "application.h"

TitleBar::TitleBar(Widget *parent) : Widget(parent) {
	Button* ext_b = new Button(this, icu::UnicodeString::fromUTF8("X"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw;
		button->t_y = 0;
	}, [&](Button* button) {
		App::ext_button();
	});
	ext_b->transparent = true;
	ext_b->window_button = true;
	ext_b->execute_on_down = false;
	ext_b->rounded = true;
	ext_b->background_color = App::theme.del_diff;
	ext_b->text_special = 1;
	
	Button* win_b = new Button(this, icu::UnicodeString::fromUTF8("□"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*2;
		button->t_y = 0;
	}, [&](Button* button) {
		App::win_button();
	});
	win_b->transparent = true;
	win_b->window_button = true;
	win_b->execute_on_down = false;
	win_b->rounded = true;
	win_b->background_color = App::theme.hover_background_color;
	win_b->text_special = 4;
	
	Button* min_b = new Button(this, icu::UnicodeString::fromUTF8("-"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*3;
		button->t_y = 0;
	}, [&](Button* button) {
		App::min_button();
	});
	min_b->transparent = true;
	min_b->window_button = true;
	min_b->execute_on_down = false;
	min_b->rounded = true;
	min_b->background_color = App::theme.hover_background_color;
	min_b->text_special = 3;
	
	hovered = false;
	
	id = icu::UnicodeString::fromUTF8("Titlebar");
}

void TitleBar::position(int x, int y, int width, int height) {
	t_w = width+App::text_padding*2; // we position root element inset slightly. Because it looks f****** amazing.
	t_h = children[0]->t_h+1;
	
	Widget::position(0, 0, t_w, t_h);
}

bool TitleBar::is_out_of_child(int x) {
	if (!is_visible) {
		return true;
	}
	
	for (auto c : children) {
		if (c->clickthrough) {
			continue;
		}
		
		if (x > c->t_x && x < c->t_x+c->t_w) {
			return false;
		}
	}
	
	return true;
}

bool TitleBar::on_mouse_button_event(int button, int action, int mods) {
	if (!is_visible) {
		return false;
	}
	
	if (!hovered) {
		return false;
	}
	
	for (auto child : children) {
		if (child->on_mouse_button_event(button, action, mods)) {
			return true;
		}
	}
	
	return true;
}

bool TitleBar::on_mouse_move_event() {
	if (!is_visible) {
		
		return false;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (0 <= mx && t_w >= mx && 0 <= my && t_h >= my) {
		hovered = true;
	}else{
		hovered = false;
	}
	
	Widget::on_mouse_move_event();
	return false;
}

void TitleBar::render() {
	if (!is_visible) {
		return;
	}
	
	App::DrawRect(0, 0, t_w, t_h, App::theme.main_background_color);
	
	App::SKIZ_X = 0;
	App::SKIZ_Y = 0;
	App::SKIZ_W = t_w;
	App::SKIZ_H = t_h;
	
	for (auto w : children) {
		// this is the one instance (as if there aren't more) where our archetecture screws us. The root widget takes up the space below the titlebar (so we have to handle this...)
		glScissor(w->t_x, App::WINDOW_HEIGHT-(w->t_y+w->t_h), w->t_w, w->t_h);
		w->render(); // we do this manually because otherwise our scissoring will cut this off
	}
	
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_SCISSOR_TEST);
}