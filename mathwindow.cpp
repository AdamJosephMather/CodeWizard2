#include "mathwindow.h"
#include "application.h"
#include "text_renderer.h"

MathWindow::MathWindow(Widget *parent) : Widget(parent) {
	id = MST::toMonoString("MathWindow");
	
	mathInput = new TextEdit(this, [&](Widget*){
		mathInput->t_x = t_x;
		mathInput->t_y = t_y;
		mathInput->t_w = (t_w*2)/3-1;
		mathInput->t_h = t_h;
	});
	mathInput->rounded = true;
	mathInput->scrollbar_horizontal = true;
	mathInput->scrollbar_vertical = true;
	mathInput->contextmenu->is_visible_3 = true;
	
	mathInput->ontextchange = [&](Widget* w){
		results.clear();
		results.reserve(mathInput->lines.size());
		MST::MonoString lastCalc = MST::toMonoString("_");
		
		std::unordered_map<std::string, MST::MonoString> vars;
		MST::MonoString alphabet = MST::toMonoString("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
		
		for (auto& line : mathInput->lines) {
			std::string varname = "";
			MST::MonoString expression{};
			
			auto splt = MST::split(line.line_text, U'=');
			
			if (splt.size() > 2) {
				results.push_back(MST::MonoString());
				continue;
			}else if(splt.size() == 2){
				varname = MST::toString(MST::stripOfChar(splt.at(0), U' '));
				expression = splt.at(1);
			}else{
				expression = line.line_text;
			}
			
			MST::MonoString done_str;
			MST::MonoString current;
			
			for (int i = 0; i < expression.length; i++) {
				MST::u32 c = MST::char32At(expression, i);
				if (MST::index(alphabet, 0, c) != -1) {
					current += MST::substring(expression, i, i+1);
				}else{
					if (current.length != 0) {
						std::string cur = MST::toString(current);
						auto it = vars.find(cur);
						if (it != vars.end()) {
							current = MST::toMonoString(U'(')+it->second+MST::toMonoString(U')');
						}
						done_str += current;
						current = {};
					}
					done_str += MST::substring(expression, i, i+1);
				}
			}
			
			if (current.length != 0) {
				std::string cur = MST::toString(current);
				auto it = vars.find(cur);
				if (it != vars.end()) {
					current = MST::toMonoString(U'(')+it->second+MST::toMonoString(U')');
				}
				done_str += current;
				current = {};
			}
			
			auto modded = MST::replaceAll(done_str, MST::toMonoString("_"), lastCalc);
			
			auto res = calcExpression(modded);
			if (res.first){
				results.push_back(doubleToMonoString_pretty(res.second));
				
				auto out = doubleToMonoString(res.second);
				lastCalc = out;
				if (varname != "") {
					vars[varname] = out;
				}
			}else{
				results.push_back(MST::MonoString());
			}
		}
	};
	
	results = {MST::toMonoString("")};
}

void MathWindow::render() {
	int x = t_x+mathInput->t_w+1;
	int w = t_w-mathInput->t_w-1;
	
	App::DrawRoundedRect(x-App::text_padding*2-1, t_y, w+App::text_padding*2+1, t_h, App::text_padding, App::theme.overlay_background_color);
	App::DrawRoundBorder(x-App::text_padding*2-1, t_y, w+App::text_padding*2+1, t_h, App::theme.border, 5, App::text_padding);
	
	Widget::render();
	
	int cury = mathInput->start_y+t_y+App::text_padding;
	x += App::text_padding;
	
	int line_start = floor(mathInput->scrolled_to_vert);
	int end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	int textH = TextRenderer::get_text_height();
	int iconS = textH*0.7;
	int iconOff = (textH-iconS)/2;
	
	if (textH - iconS * 2 != iconOff) {
		iconS += 1;
	}
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	onMouseClick = MST::MonoString();
	
	for (int i = line_start; i <= end_line; i++) {
		if (i >= results.size()) {
			break;
		}
		
		if (results[i].length != 0) {
			TextRenderer::draw_text(x, cury, results[i], App::theme.main_text_color);
			int ix = t_x+t_w-textH-App::text_padding;
			if (mx >= ix && mx <= ix+textH && my >= cury && my <= cury + textH) {
				App::DrawRoundedRect(ix, cury, textH, textH, App::text_padding, App::theme.hover_background_color);
				App::DrawRoundBorder(ix, cury, textH, textH, App::theme.active_color, 5, App::text_padding);
				onMouseClick = results[i];
			}
			App::DrawPlus(ix+iconOff, cury+iconOff, iconS, iconS, 2, App::theme.main_text_color);
		}
		
		cury += textH;
	}
	
	App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
}

bool MathWindow::on_mouse_button_event(int button, int action, int mods) {
	if (onMouseClick.length != 0 && action == GLFW_PRESS) {
		std::string txt = MST::toString(onMouseClick);
		SetClipboardText(txt);
		App::displayToast(MST::toMonoString("Coppied to clipboard!"));
		return true;
	}
	
	return Widget::on_mouse_button_event(button, action, mods);
}