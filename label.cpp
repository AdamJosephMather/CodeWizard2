#include "label.h"
#include "text_renderer.h"
#include "application.h"
#include "helper_types.h"
#include <unicode/uchar.h>

Label::Label(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Label");
	
	fulltext = icu::UnicodeString::fromUTF8("");
	drawlines = {};
}

void Label::setFullText(icu::UnicodeString text) {
	auto lns = splitByChar(text, U'\n');
	
	std::lock_guard<std::mutex> lock(positioning);
	
	fulltext = text;
	old_width = -1;
}

icu::UnicodeString Label::getFullText() {
	return fulltext;
}

bool Label::on_mouse_button_event(int button, int action, int mods) {
	if (action != GLFW_PRESS || button != GLFW_MOUSE_BUTTON_LEFT) {
		return Widget::on_mouse_button_event(button, action, mods);
	}
	// App::mouseX, App::mouseY inside t_x, t_y, t_w, t_h
	if (t_x < App::mouseX && App::mouseX < t_x+t_w && t_y < App::mouseY && App::mouseY < t_y+t_h) {
		if (fulltext.length() != 0) {
			std::string text;
			fulltext.toUTF8String(text);
			SetClipboardText(text);
			App::displayToast(icu::UnicodeString::fromUTF8("Coppied to clipboard."));
		}else{
			App::displayToast(icu::UnicodeString::fromUTF8("Nothing to copy."));
		}
		
		return true;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}

void Label::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, background_color);
	
	int ypos = t_y+App::text_padding;
	
	for (auto l : drawlines) {
		TextRenderer::draw_text(t_x+App::text_padding, ypos, l, App::theme.main_text_color);
		ypos += TextRenderer::get_text_height();
	}
	
	Widget::render();
}

void Label::position(int x, int y, int w, int h) {
	std::lock_guard<std::mutex> lock(positioning);
	
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	if (POSITIONER) {
		POSITIONER(this);
	}
	
	if (old_width == t_w) { return; }
	old_width = t_w;
	
	drawlines = {};
	
	icu::UnicodeString curline = icu::UnicodeString();
	
	should_be_h = App::text_padding*2;
	int linewidth = 0;
	
	int most_allowed = t_w-App::text_padding*2;
	
	for (auto ci = 0; ci < fulltext.length(); ci++) {
		UChar32 c = fulltext.char32At(ci);
		
		int num_chr = 1;
		if (c == U'\t') {
			num_chr = 4;
			c = ' ';
		}
		
		int newlen = linewidth + TextRenderer::get_text_width(num_chr);
		
		if (c == U'\n' || newlen > most_allowed) {
			drawlines.push_back(curline);
			curline = "";
			should_be_h += TextRenderer::get_text_height();
			
			linewidth = 0;
			newlen = linewidth + TextRenderer::get_text_width(num_chr);
		}
		
		if (c != U'\n') {
			for (int rep = 0; rep < num_chr; rep++) {
				curline += c;
			}
		}
		
		linewidth = newlen;
	}
	
	drawlines.push_back(curline);
	should_be_h += TextRenderer::get_text_height();
}