#include "linenumbers.h"
#include "text_renderer.h"
#include "application.h"

LineNumbers::LineNumbers(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("LineNumbers");
}

void LineNumbers::render() {
	// we do this here because we have to do this after the textedit has updated their scroll
	
	t_h = toFollow->t_h; // there's a reason I promise. Prevents 1 frame lagging of size.
	
	int number_of_lines = toFollow->lines.size();
	int max_len = std::to_string(number_of_lines).length();
	
	double text_scroll = toFollow->scrolled_to_vert; // messured in lines
	int line_start = floor(text_scroll);
	start_y = -fmod(text_scroll, 1) * TextRenderer::get_text_height();
	
	lines_to_draw.clear();
	lines_to_color.clear();
	
	int num_lines = ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	
	int cursor_line = toFollow->cursors[0].head_line+1;
	
	for (int cur_line = line_start+1; cur_line < cur_line+num_lines+1; cur_line++) {
		if (cur_line > number_of_lines) {
			break;
		}
		
		if (App::settings->getValue("use_rel_line_num", false)) {
			int dst_to_crsr = abs(cursor_line-cur_line);
			
			Color* fillcolor = App::theme.lesser_text_color;
			
			std::string line_text = "";
			
			if (dst_to_crsr == 0) {
				dst_to_crsr = cursor_line;
				line_text = std::to_string(dst_to_crsr);
				fillcolor = App::theme.main_text_color;
			}else{
				line_text = std::to_string(dst_to_crsr);
				int len = line_text.length();
				int spaces = max_len-len;
				for (int s = 0; s < spaces; s++) {
					line_text = " " + line_text;
				}
			}
			
			std::vector<Color*> colors = AllOneColor(fillcolor, line_text.length());
			icu::UnicodeString text = icu::UnicodeString::fromUTF8(line_text);
			
			lines_to_draw.push_back(text);
			lines_to_color.push_back(colors);
		}else{
			Color* fillcolor = App::theme.lesser_text_color;
			
			std::string line_text = std::to_string(cur_line);
			
			if (cur_line == cursor_line) {
				fillcolor = App::theme.main_text_color;
			}
			
			int len = line_text.length();
			int spaces = max_len-len;
			for (int s = 0; s < spaces; s++) {
				line_text = " " + line_text;
			}
			
			std::vector<Color*> colors = AllOneColor(fillcolor, line_text.length());
			icu::UnicodeString text = icu::UnicodeString::fromUTF8(line_text);
			
			lines_to_draw.push_back(text);
			lines_to_color.push_back(colors);
		}
	}
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	
	int curx = t_x+App::text_padding;
	int cury = start_y+t_y+App::text_padding;
	
	for (int ln_ren = 0; ln_ren < lines_to_draw.size(); ln_ren++) {
		TextRenderer::draw_text(curx, cury, lines_to_draw[ln_ren], lines_to_color[ln_ren]);
		cury += TextRenderer::get_text_height();
	}
	
	Widget::render();
}

void LineNumbers::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_h = h;
	
	int number_of_lines = toFollow->lines.size();
	int max_len = std::to_string(number_of_lines).length();
	
	if (!toFollow) {
		t_w = 0;
		Widget::position(t_x, t_y, t_w, t_h);
		return;
	}else{
		t_w = TextRenderer::get_text_width(max_len) + App::text_padding*2;
		t_h = toFollow->t_h;
	}
	
	Widget::position(t_x, t_y, t_w, t_h);
}

void LineNumbers::setTextedit(TextEdit* edit) {
	toFollow = edit;
}
