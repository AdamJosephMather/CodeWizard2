#include "lspdebug.h"
#include "application.h"

LspDebug::LspDebug(Widget *parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("LspDebug");
	
	viewbox = new TextEdit(this, [](Widget*){});
}

void LspDebug::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
}

void LspDebug::lspmessage(std::string& from, std::string& message) {
	new std::thread([=]() {
		std::lock_guard<std::mutex> lock(App::canMakeChanges);
		Cursor c = Cursor();
		
		c.head_line = viewbox->lines.size()-1;
		c.head_char = viewbox->lines[c.head_line].line_text.length();
		
		c.anchor_line = c.head_line;
		c.anchor_char = c.head_char;
		
		std::string newstring = "\n\n["+from+"] --> "+trim(message);
		icu::UnicodeString log = icu::UnicodeString::fromUTF8(newstring);
		viewbox->insertTextAtCursor(c, log);
	});
}