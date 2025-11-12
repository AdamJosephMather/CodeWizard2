#include "helpmenu.h"
#include "text_renderer.h"

HelpMenu::HelpMenu(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("HelpMenu");
	closebutton = new Button(this, icu::UnicodeString::fromUTF8("X"), [&](Widget* b, int x, int y, int w, int h, int width, int height){
		b->t_x = t_x+t_w-width;
		b->t_y = t_y;
		b->t_w = width;
		b->t_h = height;
	}, [&](Widget*){
		App::RemoveWidgetFromParent(this);
	});
	closebutton->window_button = true;
	
	label = new Label(this);
	label->setFullText(icu::UnicodeString::fromUTF8("Widgets"));
	label->POSITIONER = [&](Widget* l) {
		l->t_x = t_x;
		l->t_y = t_y+closebutton->t_h;
		l->t_w = t_w;
		l->t_h = t_h-closebutton->t_h;
	};
	label->border = false;
}

void HelpMenu::render() {
	App::DrawRect(t_x, t_y, t_w, closebutton->t_h, App::theme.main_background_color);
	App::DrawRect(t_x, t_y+closebutton->t_h, t_w, t_h-closebutton->t_h, App::theme.darker_background_color);
	
	int vert_padding = (closebutton->t_h-TextRenderer::get_text_height())/2;
	TextRenderer::draw_text(t_x+vert_padding, t_y+vert_padding, icu::UnicodeString::fromUTF8("CodeWizard2 Help Menu"), App::theme.main_text_color);
	Widget::render();
	App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
}

void HelpMenu::position(int x, int y, int w, int h) {
	t_x = x+w*.1;
	t_y = y+h*.1;
	t_w = w*.8;
	t_h = h*.8;
	Widget::position(t_x, t_y, t_w, t_h);
}

bool HelpMenu::on_key_event(int key, int scancode, int action, int mods) {
	Widget::on_key_event(key, scancode, action, mods);
	return true;
}

bool HelpMenu::on_char_event(unsigned int codepoint) {
	Widget::on_char_event(codepoint);
	return true;
}

bool HelpMenu::on_mouse_button_event(int button, int action, int mods) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (my <= App::tb->t_h) {
		return false;
	}
	
	if (action == GLFW_RELEASE && (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h)) {
		App::RemoveWidgetFromParent(this);
		return true;
	}
	
	Widget::on_mouse_button_event(button, action, mods);
	return true;
}

bool HelpMenu::on_mouse_move_event() {
	Widget::on_mouse_move_event();
	return true;
}

bool HelpMenu::on_scroll_event(double xchange, double ychange) {
	Widget::on_scroll_event(xchange, ychange);
	return true;
}