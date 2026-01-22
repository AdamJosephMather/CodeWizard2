#include "mathwindow.h"
#include "application.h"

MathWindow::MathWindow(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("MathWindow");
	
	mathInput = new TextEdit(this, [&](Widget*){
		mathInput->t_x = t_x;
		mathInput->t_y = t_y;
		mathInput->t_w = (t_w*2)/3-1;
		mathInput->t_h = t_h;
	});
	mathInput->rounded = true;
	mathInput->border = true;
	
	mathOutput = new TextEdit(this, [&](Widget*){
		mathOutput->t_x = t_x+mathInput->t_w+1;
		mathOutput->t_y = t_y;
		mathOutput->t_w = t_w/3;
		mathOutput->t_h = t_h;
	});
	mathOutput->rounded = true;
	mathOutput->border = true;
	mathOutput->background_color = App::theme.overlay_background_color;
	
	mathInput->ontextchange = [&](Widget* w){
		
	};
}

void MathWindow::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	if (mathInput->scrolled_to_vert != mathOutput->scrolled_to_vert) {
		mathOutput->scrolled_to_vert = mathInput->scrolled_to_vert;
	}
	
	Widget::position(x, y, w, h);
}

void MathWindow::render() {
	Widget::render();
}