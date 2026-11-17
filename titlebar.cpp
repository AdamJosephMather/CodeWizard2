#include "titlebar.h"

#include "application.h"

TitleBar::TitleBar(Widget *parent) : Widget(parent) {
	ext_b = new Button(this, MST::toMonoString("X"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw;
		button->t_y = 0;
	}, [&](Button* button) {
		App::ext_button();
	});
	ext_b->window_button = true;
	ext_b->execute_on_down = false;
	ext_b->background_color = nullptr;
	ext_b->background_color_hover = App::theme.del_diff;
	ext_b->border_color = nullptr;
	ext_b->border_color_hover = nullptr;
	ext_b->text_special = 1;
	
	win_b = new Button(this, MST::toMonoString("□"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*2;
		button->t_y = 0;
	}, [&](Button* button) {
		App::win_button();
	});
	win_b->window_button = true;
	win_b->execute_on_down = false;
	win_b->background_color = nullptr;
	win_b->background_color_hover = App::theme.hover_background_color;
	win_b->border_color = nullptr;
	win_b->border_color_hover = nullptr;
	win_b->text_special = 4;
	
	min_b = new Button(this, MST::toMonoString("-"), [&](Button* button, int x, int y, int w, int h, int tw, int th){
		button->t_x = t_w-tw*3;
		button->t_y = 0;
	}, [&](Button* button) {
		App::min_button();
	});
	min_b->window_button = true;
	min_b->execute_on_down = false;
	min_b->background_color = nullptr;
	min_b->background_color_hover = App::theme.hover_background_color;
	min_b->border_color = nullptr;
	min_b->border_color_hover = nullptr;
	min_b->text_special = 3;
	
	hovered = false;
	
	id = MST::toMonoString("Titlebar");
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
	if (!is_visible) return;

	// The titlebar sits above the root content area. Establish its own scissor
	// before creating a rounded clip, then restore the caller's clipping state.
	const int previousX = App::SKIZ_X, previousY = App::SKIZ_Y;
	const int previousW = App::SKIZ_W, previousH = App::SKIZ_H;
	glPushAttrib(GL_SCISSOR_BIT);
	glEnable(GL_SCISSOR_TEST);
	App::SKIZ_X = 0;
	App::SKIZ_Y = 0;
	App::SKIZ_W = std::max(t_w, 0);
	App::SKIZ_H = std::max(t_h, 0);
	glScissor(0, App::WINDOW_HEIGHT-t_h, App::SKIZ_W, App::SKIZ_H);

	const float radius = App::themeCornerRadius(t_w, t_h);
	App::runWithRoundedClip(0, 0, t_w, t_h, radius, [&]() {
		if (radius > 0.0f) App::DrawRoundedRect(0, 0, t_w, t_h, radius, App::theme.main_background_color);
		else App::DrawRect(0, 0, t_w, t_h, App::theme.main_background_color);
		for (auto child : children) {
			App::runWithSKIZ(child->t_x, child->t_y, child->t_w, child->t_h, [&]() {
				child->render();
			});
		}
	});

	App::SKIZ_X = previousX;
	App::SKIZ_Y = previousY;
	App::SKIZ_W = previousW;
	App::SKIZ_H = previousH;
	glPopAttrib();
}
