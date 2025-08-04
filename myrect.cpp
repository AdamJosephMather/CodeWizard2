#include "myrect.h"
#include "application.h"

MyRect::MyRect(Widget *parent, App::PosFunction positioner) : Widget(parent) {
	POSITIONER = positioner;
	background_color = App::theme.main_background_color;
	id = icu::UnicodeString::fromUTF8("MyRect");
}

void MyRect::position(int x, int y, int width, int height) {
	t_w = width;
	t_h = height;
	t_x = x;
	t_y = y;
	
	POSITIONER(this);
}

void MyRect::render() {
	if (!is_visible) {
		return;
	}
	
	App::DrawRect(t_x, t_y, t_w, t_h, background_color);
}
