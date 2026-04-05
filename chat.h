#pragma once

#include <GLFW/glfw3.h>
#include <vector>
#include "listbox.h"
#include "textedit.h"
#include "widget.h"

struct Segment {
	bool isCode;          // true ⇒ this segment is a code block
	std::string content;  // the raw text of that segment
};

class Chat : public Widget {
public:
	Chat(Widget* parent);
	
	bool rounded = true;
	
	void render();
	void position(int x, int y, int width, int height);
	
	bool on_mouse_button_event(int button, int action, int mods);
	bool on_scroll_event(double xchange, double ychange);
	bool on_key_event(int key, int scancode, int action, int mods);
	
	void request_close(close_callback_type callback);
	
	double scrolled_to = 0;
	int min_scroll = 0;
	
private:
	TextEdit* querybox = nullptr;
	std::vector<Widget*> message_te = {};
	std::vector<bool> from_user = {};
	Button* filesButton;
	std::vector<std::string> textstoadd = {};
	ListBox* filesAddList;
	Button* newChat;
	bool running = false;
	
	std::vector<Segment> splitMarkdown(const std::string& input);
};