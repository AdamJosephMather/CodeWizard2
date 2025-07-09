#include "titlebar.h"

#include "button.h"
#include "application.h"
#include "text_renderer.h"

TitleBar::TitleBar(Widget *parent) : Widget(parent) {
	Button* ext_b = new Button(this, icu::UnicodeString::fromUTF8("X"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw;
		button->t_y = 0;
	}, [&](Button* button) {
		App::ext_button();
	});
	ext_b->transparent = true;
	ext_b->window_button = true;
	
	Button* win_b = new Button(this, icu::UnicodeString::fromUTF8("□"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*2;
		button->t_y = 0;
	}, [&](Button* button) {
		App::win_button();
	});
	win_b->transparent = true;
	win_b->window_button = true;
	
	Button* min_b = new Button(this, icu::UnicodeString::fromUTF8("-"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*3;
		button->t_y = 0;
	}, [&](Button* button) {
		App::min_button();
	});
	min_b->transparent = true;
	min_b->window_button = true;
	
	hovered = false;
	
	id = icu::UnicodeString::fromUTF8("Titlebar");
}

void TitleBar::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = children[0]->t_h;
	
	Widget::position(0, 0, t_w, t_h);
}

bool TitleBar::is_out_of_child(int x) {
	if (!is_visible) {
		return true;
	}
	
	for (auto c : children) {
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
	
	App::SKIZ_X = 0;
	App::SKIZ_Y = 0;
	App::SKIZ_W = t_w;
	App::SKIZ_H = t_h;
	
	for (auto w : children) {
		// this is the one instance (as if there aren't more) where our archetecture screws us. The root widget takes up the space below the titlebar (so we have to handle this...)
		glScissor(w->t_x, App::WINDOW_HEIGHT-(w->t_y+w->t_h), w->t_w, w->t_h);
		w->render(); // we do this manually because otherwise our scissoring will cut this off
	}
}
