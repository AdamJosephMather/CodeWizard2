#include "myrect.h"
#include "application.h"

MyRect::MyRect(Widget *parent, App::PosFunction positioner) : Widget(parent) {
	POSITIONER = positioner;
	background_color = App::theme.main_background_color;
	border_color = App::theme.border;
	id = MST::toMonoString("MyRect");
}

void MyRect::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	POSITIONER(this);
}

void MyRect::render() {
	const float radius = App::themeCornerRadius(t_w, t_h);
	if (!is_visible) {
		return;
	}
	
	if (radius > 0.0f) {
		App::DrawRoundedRect(t_x, t_y, t_w, t_h, radius, background_color);
	}else{
		App::DrawRect(t_x, t_y, t_w, t_h, background_color);
	}
	
	
	if (border_color != nullptr) {
		if (radius > 0.0f) {
			App::DrawRoundBorder(t_x, t_y, t_w, t_h, border_color, 5, radius);
		}else{
			App::DrawBorder(t_x, t_y, t_w, t_h, border_color);
		}
	}
}