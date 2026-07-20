#include "label.h"
#include "text_renderer.h"
#include "application.h"
#include "helper_types.h"

Label::Label(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("Label");
	
	fulltext = MST::toMonoString("");
	drawlines = {};
}

void Label::setFullText(MST::MonoString text, std::vector<MarkdownSpan> spans) {
	std::lock_guard<std::mutex> lock(positioning);
	
	fulltext = text;
	old_width = -1;
	handlingColor = (spans.size() != 0);
	colorSpans = spans;
}

MST::MonoString Label::getFullText() {
	return fulltext;
}

bool Label::on_mouse_button_event(int button, int action, int mods) {
	if (action != GLFW_PRESS || button != GLFW_MOUSE_BUTTON_LEFT) {
		return Widget::on_mouse_button_event(button, action, mods);
	}
	
	if (t_x < App::mouseX && App::mouseX < t_x+t_w && t_y < App::mouseY && App::mouseY < t_y+t_h) {
		if (fulltext.length != 0) {
			std::string text = MST::toString(fulltext);
			SetClipboardText(text);
			App::displayToast(MST::toMonoString("Coppied to clipboard."));
		}else{
			App::displayToast(MST::toMonoString("Nothing to copy."));
		}
		
		return true;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}

void Label::render() {
	if (rect) {
		if (rounded) {
			App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, background_color);
		}else{
			App::DrawRect(t_x, t_y, t_w, t_h, background_color);
		}
	}
	if (border && rounded) {
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else if (border) {
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
	
	int ypos = t_y+App::text_padding;
	
	for (int i = 0; i < drawlines.size(); i++) {
		if (handlingColor) {
			TextRenderer::draw_text(t_x+App::text_padding, ypos, drawlines[i], drawColors[i]);
		}else{
			TextRenderer::draw_text(t_x+App::text_padding, ypos, drawlines[i], App::theme.main_text_color);
		}
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
	
	const int mx = App::mouseX;
	const int my = App::mouseY;
	if (cursor_in_this) {
		App::expectedCursorType = 3;
	}
	
	if (old_width == t_w) { return; }
	old_width = t_w;
	
	drawlines.clear();
	drawColors.clear();
	
	MST::MonoString curline = MST::MonoString();
	
	should_be_h = App::text_padding*2;
	int linewidth = 0;
	
	const int most_allowed = t_w-App::text_padding*2;
	
	int curspan = 0;
	std::vector<Color*> curlineColor = {};
	Color* thisColor;
	
	for (auto ci = 0; ci < fulltext.length; ci++) {
		auto c = MST::char32At(fulltext, ci);
		
		if (handlingColor) {
			thisColor = App::theme.main_text_color;
			if (curspan < colorSpans.size()) { // spans are always ordered so that we can only look at one at a time (mucho faster)
				if (ci >= colorSpans[curspan].start && ci < colorSpans[curspan].end) {
					auto t = colorSpans[curspan].type;
					if (t == MarkdownElem::Header) {
						thisColor = App::theme.equal_diff;
					}else if (t == MarkdownElem::Bold) {
						thisColor = App::theme.add_diff;
					}else if (t == MarkdownElem::Italic) {
						thisColor = App::theme.warning_color;
					}else if (t == MarkdownElem::Link) {
						thisColor = App::theme.equal_diff;
					}else if (t == MarkdownElem::Code) {
						thisColor = App::theme.warning_color;
					}
				}
				
				if (ci == colorSpans[curspan].end) {
					curspan ++;
				}
			}
		}
		
		int CHAR_WIDTH = MST::isEmoji(fulltext, ci) ? 2 : 1;
		int newlen = linewidth + TextRenderer::get_text_width(CHAR_WIDTH);
		
		if (c == U'\n' || newlen > most_allowed) {
			drawlines.push_back(curline);
			curline = MST::MonoString{};
			
			if (handlingColor) {
				drawColors.push_back(curlineColor);
				curlineColor.clear();
			}
			
			should_be_h += TextRenderer::get_text_height();
			
			linewidth = 0;
			newlen = TextRenderer::get_text_width(CHAR_WIDTH);
		}
		
		if (c != U'\n') {
			if (c == U'\t'){
				curline += MST::toMonoString(MST::CONT);
			}else{
				curline += MST::toMonoString(c);
			}
			if (handlingColor) {
				curlineColor.push_back(thisColor);
			}
		}
		
		linewidth = newlen;
	}
	
	drawlines.push_back(curline);
	if (handlingColor) {
		drawColors.push_back(curlineColor);
	}
	should_be_h += TextRenderer::get_text_height();
	
	if (t_x < App::mouseX && App::mouseX < t_x+t_w && t_y < App::mouseY && App::mouseY < t_y+t_h) {
		if (App::expectedCursorType == -1 && cursor_in_this) { // only set cursor if expected to be arrow right now
			App::expectedCursorType = 3; // hand cursor
		}
	}
}