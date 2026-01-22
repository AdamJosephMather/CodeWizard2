#include "mathwindow.h"
#include "application.h"
#include "text_renderer.h"

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
	
	mathInput->ontextchange = [&](Widget* w){
		std::cout << "OTC\n";
		
		results.clear();
		results.reserve(mathInput->lines.size());
		icu::UnicodeString lastCalc = icu::UnicodeString::fromUTF8("_");
		
		std::unordered_map<std::string, icu::UnicodeString> vars;
		icu::UnicodeString alphabet = icu::UnicodeString::fromUTF8("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
		
		for (auto line : mathInput->lines) {
			std::string varname = "";
			icu::UnicodeString expression = "";
			
			auto splt = splitByChar(line.line_text, U'=');
			
			if (splt.size() > 2) {
				results.push_back(icu::UnicodeString());
				continue;
			}else if(splt.size() == 2){
				stripOfChar(splt.at(0), U' ').toUTF8String(varname);
				expression = splt.at(1);
			}else{
				expression = line.line_text;
			}
			
			icu::UnicodeString done_str;
			icu::UnicodeString current;
			
			for (int i = 0; i < expression.length(); i++) {
				UChar32 c = expression.char32At(i);
				if (alphabet.indexOf(c) != -1) {
					current += c;
				}else{
					if (current.length() != 0) {
						std::string cur;
						current.toUTF8String(cur);
						auto it = vars.find(cur);
						if (it != vars.end()) {
							current = it->second;
						}
						done_str += current;
						current = "";
					}
					done_str += c;
				}
			}
			
			if (current.length() != 0) {
				std::string cur;
				current.toUTF8String(cur);
				auto it = vars.find(cur);
				if (it != vars.end()) {
					current = it->second;
				}
				done_str += current;
				current = "";
			}
			
			auto modded = replaceWith(done_str, icu::UnicodeString::fromUTF8("_"), lastCalc);
			
			auto res = calcExpression(modded);
			if (res.first){
				auto out = doubleToUnicodeString(res.second);
				results.push_back(out);
				lastCalc = out;
				if (varname != "") {
					vars[varname] = out;
				}
			}else{
				results.push_back(icu::UnicodeString());
			}
		}
	};
	
	results = {icu::UnicodeString::fromUTF8("")};
}

void MathWindow::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	Widget::position(x, y, w, h);
}

void MathWindow::render() {
	Widget::render();
	
	int x = t_x+mathInput->t_w+1;
	int w = t_w-mathInput->t_w-1;
	
	App::DrawRoundedRect(x, t_y, w, t_h, App::text_padding, App::theme.overlay_background_color);
	App::DrawRoundBorder(x, t_y, w, t_h, App::theme.border, 5, App::text_padding);
	
	int cury = mathInput->start_y+t_y+App::text_padding;
	x += App::text_padding;
	
	int line_start = floor(mathInput->scrolled_to_vert);
	int end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	
	for (int i = line_start; i <= end_line; i++) {
		if (i >= results.size()) {
			break;
		}
		
		TextRenderer::draw_text(x, cury, results[i], App::theme.main_text_color);
		cury += TextRenderer::get_text_height();
	}
}