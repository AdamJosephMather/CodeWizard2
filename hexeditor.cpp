#include "hexeditor.h"
#include "application.h"
#include "text_renderer.h"

HexEditor::HexEditor(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("HexEditor");
	
	scrollbar = new Scrollbar(this);
	scrollbar->getScrollInfo = [&](){
		std::vector<double> out = {0.0, 0.0};
		double screenlines = (double)t_h/(double)TextRenderer::get_text_height();
		double end_line = scroll+screenlines;
		
		out[0] = scroll/((double)lineCount+screenlines-1);
		out[1] = end_line/((double)lineCount+screenlines-1);
		
		return out;
	};
	scrollbar->scrollTo = [&](double newval){
		double screenlines = (double)t_h/(double)TextRenderer::get_text_height();
		scroll = newval*((double)lineCount+screenlines-1);
		
		if (scroll > max_scroll_vert) {
			scroll = max_scroll_vert;
		}else if (scroll < 0.0) {
			scroll = 0.0;
		}
		
		scroll_change = 0;
		DO_POSITION = true;
	};
}

void HexEditor::position(int x, int y, int width, int height) {
	Widget::position(x, y, width, height);
	
	if (scroll_change != 0) {
		double value = (App::settings->getValue("smooth_scroll", 0.2f) * App::settings->getValue("anim_speed", 1.0f));
		double changeyby = scroll_change*value;
		
		if (scroll_change < 0.1 && scroll_change > -0.1) {
			changeyby = scroll_change;
		}
		
		scroll += changeyby;
		scroll_change -= changeyby;
		
		if (scroll > max_scroll_vert) {
			scroll = max_scroll_vert;
		}else if (scroll < 0.0) {
			scroll = 0.0;
		}
		
		App::time_till_regular = 2;
	}
}

void HexEditor::openFile(FileInfo* f) {
	file = f;
	
	App::setActiveLeafNode(this);
	
	if (!file) {
		return;
	}
	
	bool worked;
	bytes = loadFileBytes(f->filepath, worked);
	
	if (!worked) {
		file = nullptr;
	}
	
	scroll_change = 0;
	scroll = 0;
	
	lineCount = ceil((double)bytes.size() / 16);
	
	max_scroll_vert = bytes.size() / 16;
	if (bytes.size() % 16 == 0) {
		max_scroll_vert -= 1;
	}
	
	App::time_till_regular = 2;
}

bool HexEditor::on_scroll_event(double xchange, double ychange) {
	if (!cursor_in_this) {
		return false;
	}
	
	scroll_change += ychange*6;
	DO_POSITION = true;
	App::time_till_regular = 2;
	
	return false;
}

std::string HexEditor::toHex(int num) {
	std::stringstream ss;
	ss << std::hex << num;
	std::string hex_str = ss.str(); // "ff"
	std::transform(hex_str.begin(), hex_str.end(), hex_str.begin(), ::toupper); // "FF"
	return hex_str;
}


void HexEditor::render() {
	if (!is_visible) return;

	// draw the file
	
	long long line_start = floor(scroll);
	long long end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	
	int cur_y = t_y + App::text_padding - fmod(scroll, 1)*TextRenderer::get_text_height();
	
	long long maxAddress = lineCount*16;
	std::string hexMaxAddress = toHex(maxAddress);
	int req_len = hexMaxAddress.length();
	
	int mx = App::mouseX;
	int my = App::mouseY;
	
	int left = t_x+App::text_padding;
	int asciiX = left + TextRenderer::get_text_width(req_len+53);
	
	// background
	
	int columnWidth = TextRenderer::get_text_width(req_len+1)+App::text_padding;
	App::DrawRect(t_x, t_y, columnWidth, t_h, App::theme.extras_background_color);
	App::DrawRect(t_x+columnWidth, t_y, t_w-columnWidth, t_h, App::theme.darker_background_color);
	App::DrawRect(t_x+columnWidth, t_y, 1, t_h, App::theme.border); // border betwix the two
	
	// text
	
	for (long long l = line_start; l < fmin(end_line, lineCount); l++) {
		long long address = l*16; // idk if we really need long longs but... why not, eh?
		std::string addressString = toHex(address);
		int count = req_len - addressString.length();
		
		for (int i = 0; i < count; i++) {
			addressString = "0"+addressString;
		}
		
		std::string hexdata = "";
		std::string ascii = "";
		
		std::vector<Color*> asciiColors = {};
		
		for (long long offset = address; offset < address+16; offset++) {
			if (offset >= bytes.size()) {
				break;
			}
			
			if (offset % 8 == 0) {
				hexdata += " ";
			}
			
			std::uint8_t byte = bytes[offset];
			
			auto hexStr = toHex(byte);
			if (hexStr.length() == 0) {
				hexStr = "00";
			}else if (hexStr.length() == 1) {
				hexStr = "0" + hexStr;
			}
			
			if (cursor_in_this && my >= cur_y && my < cur_y+TextRenderer::get_text_height()) {
				int x_left = left + TextRenderer::get_text_width(req_len+2+hexdata.size()) - App::text_padding;
				int x_right = x_left+TextRenderer::get_text_width(2)+App::text_padding*2;
				
				int x_left_ascii = asciiX + TextRenderer::get_text_width(ascii.length());
				int x_right_ascii = x_left_ascii + TextRenderer::get_text_width(1);
				
				if ((x_left <= mx && x_right > mx) || (x_left_ascii <= mx && x_right_ascii > mx)) {
					App::DrawRect(x_left, cur_y, TextRenderer::get_text_width(2)+App::text_padding*2, TextRenderer::get_text_height(), App::theme.hover_background_color);
					App::DrawBorder(x_left, cur_y, TextRenderer::get_text_width(2)+App::text_padding*2, TextRenderer::get_text_height(), App::theme.active_color);
					
					App::DrawRect(x_left_ascii, cur_y, TextRenderer::get_text_width(1), TextRenderer::get_text_height(), App::theme.hover_background_color);
					App::DrawBorder(x_left_ascii, cur_y, TextRenderer::get_text_width(1), TextRenderer::get_text_height(), App::theme.active_color);
				}
			}
			
			hexdata += hexStr + " ";
			
			if (std::isprint(byte)) {
				ascii += static_cast<char>(byte);
				asciiColors.push_back(App::theme.main_text_color);
			}else{
				ascii += ".";
				asciiColors.push_back(App::theme.lesser_text_color);
			}
		}
		
		TextRenderer::draw_text(left, cur_y, icu::UnicodeString::fromUTF8(addressString+": "+hexdata), App::theme.main_text_color);
		TextRenderer::draw_text(asciiX, cur_y, icu::UnicodeString::fromUTF8(ascii), asciiColors); // 53 is the combined length of the spaces and other text there
		cur_y += TextRenderer::get_text_height();
	}
	
	// scrollbar
	
	Widget::render();
	
	// border
	
	Color* borderC = App::theme.border;
	if (this == App::activeLeafNode) {
		borderC = App::theme.active_color;
	}
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
	}
}

bool HexEditor::on_mouse_button_event(int button, int action, int mods) {
	Widget::on_mouse_button_event(button, action, mods);
	
	if (cursor_in_this && this != App::activeLeafNode && action == GLFW_PRESS) {
		App::setActiveLeafNode(this);
		return true;
	}
	return false;
}