#include "widget.h"
#include "application.h"

Widget::Widget(Widget* p) {
	parent = p;
	
	if (parent) {
		parent->children.push_back(this);
	}
	
	children = {};
	
}

bool Widget::on_char_event(unsigned int codepoint) {
	if (!is_visible) {
		return false;
	}
	
	for (auto w : children) {
		if (w->on_char_event(codepoint)) {
			return true;
		}
	}
	
	return false;
}

bool Widget::on_key_event(int key, int scancode, int action, int mods){
	if (!is_visible) {
		return false;
	}
	
	for (auto w : children) {
		if (w->on_key_event(key, scancode, action, mods)) {
			return true;
		}
	}
	
	return false;
}

bool Widget::on_mouse_button_event(int button, int action, int mods){
	if (!is_visible) {
		return false;
	}
	
	for (auto w : children) {
		if (w->on_mouse_button_event(button, action, mods)) {
			return true;
		}
	}
	
	return false;
}

bool Widget::on_mouse_move_event(){
	if (!is_visible) {
		return false;
	}
	
	for (auto w : children) {
		if (w->on_mouse_move_event()) {
			return true;
		}
	}
	
	return false;
}

bool Widget::on_scroll_event(double xchange, double ychange){
	if (!is_visible) {
		return false;
	}
	
	for (auto w : children) {
		if (w->on_scroll_event(xchange, ychange)) {
			return true;
		}
	}
	
	return false;
}

void Widget::position(int x, int y, int width, int height) { // the individual widgets will store the info they need. This here provides the space in which it **can** be placed
	if (!is_visible) {
		return;
	}
	
	for (auto w : children) {
		w->position(x, y, width, height); // by default we just give children the entire space which we are allowed to take - this will be overwritten in almost all cases
	}
}

void Widget::render() { // unlikely to need new one for each widget
	if (!is_visible) {
		return;
	}
	
	for (auto w : children) {
		App::runWithSKIZ(w->t_x, w->t_y, w->t_w, w->t_h, [&](){
			w->render();
		});
	}
}

void Widget::hide() {
	is_visible = false;
	
	for (auto w : children) {
		w->hide();
	}
}

void Widget::show() {
	is_visible = true;
	
	for (auto w : children) {
		w->show();
	}
}

void Widget::close_callback(Widget* ready_to_remove) {
	for (int i = 0; i < children.size(); i++) {
		if (children[i] == ready_to_remove) {
			children.erase(children.begin()+i);
			break;
		}
	}
	
	delete ready_to_remove;
	
	if (closing && children.size() == 0) {
		if (before_self_close) {
			before_self_close(this);
		}
		if (on_close){
			on_close(this);
		}
	}
}

void Widget::request_close(Widget::close_callback_type cllbck) {
	if (App::activeLeafNode == this || App::activeEditor == this || App::beforeCommandLeafNode == this) {
		App::setActiveLeafNode(nullptr);
	}
	
	if (closing) {
		return;
	}
	
	on_close = cllbck;
	closing = true;
	
	if (children.size() == 0) {
		if (before_self_close) {
			before_self_close(this);
		}
		if (on_close){
			on_close(this);
		}
		return;
	}
	
	auto copy = children;
	
	for (auto c : copy) {
		if (c){
			c->request_close([&](Widget* to_remove){
				close_callback(to_remove);
			});
		}
	}
}

bool Widget::widgetexists(Widget* w) {
	if (w == nullptr) {
		return false;
	}else if (this == w) {
		return true;
	}
	
	for (auto c : children) {
		if (c->widgetexists(w)) {
			return true;
		}
	}
	return false;
}

void Widget::save() {
	for (auto w : children) {
		w->save();
	}
}

Widget* Widget::fileOpen(std::string fname) {
	for (auto w : children) {
		if (auto ret = w->fileOpen(fname)) { return ret; }
	}
	return nullptr;
}

Widget* Widget::getFirstEditor() {
	for (auto c : children) {
		if (auto b = c->getFirstEditor()) { return b; }
	}
	return nullptr;
}

void Widget::lspmessage(std::string& from, std::string& message) {
	for (auto c : children) {
		c->lspmessage(from, message);
	}
}