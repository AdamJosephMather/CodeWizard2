#include "statusbar.h"
#include "codeedit.h"
#include "editor.h"
#include "graphwindow.h"
#include "hexeditor.h"
#include "imageview.h"
#include "terminalwidget.h"
#include "application.h"
#include "text_renderer.h"

struct Item {
	double importance;
	MST::MonoString text;
	double side; // [0-1) is left side, [1,2) is center [2,3) is right. Ummm, we're going to make lower values of 2-3 be closer to the right. sorry
};

StatusBar::StatusBar(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("StatusBar");
}

void StatusBar::save() {
	App::time_till_regular = 2;
}

void StatusBar::render() {
//	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.extras_background_color);
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.main_background_color);
	
	// okay, we need to determine what content needs to be rendered and how important it is
	// for example "INSERT  |  1929, 4  |  application.cpp*  |  12.2 kb  |  Widgets: 192"
	// or "1929, 4" when space is limited
	// or in a graph widget: "(44.1, 12.9) -> (88.3, 98.1)  |  Widgets: 192"
	// or in a terminal "cmd.exe  |  Widgets: 192"
	// in an image "1280x1920  |  Widgets: 160"
	
	std::vector<Item> items; // importance, text, preffered side (left/center/middle)
	
	items.push_back({
		1, // not important
		MST::toMonoString("Widgets: "+std::to_string(App::all_widgets.size())),
		2 // far right side, I know 2<3, but this way it all sorts in one pass, i know it's terrible. 
	});
	
	if (auto te = dynamic_cast<TextEdit*>(App::activeLeafNode)) {
		if (App::settings->getValue("use_vim", true)) {
			if (te->mode == 'n') {
				items.push_back({
					2, // not super important
					MST::toMonoString("[NORMAL]"),
					0 // far left side
				});
			}else if (te->mode == 'i') {
				items.push_back({
					2, // not super important
					MST::toMonoString("[INSERT]"),
					0 // far left side
				});
			}
		}
		
		MST::MonoString cursorInfo;
		
		auto c = te->cursors[0];
		if (c.head_line != c.anchor_line || c.head_char != c.anchor_char) {
			cursorInfo = MST::toMonoString(std::to_string(c.anchor_line)+", "+std::to_string(c.anchor_char)+" -> "+std::to_string(c.head_line)+", "+std::to_string(c.head_char));
		}else{
			cursorInfo = MST::toMonoString(std::to_string(c.head_line)+", "+std::to_string(c.head_char));
		}
		
		items.push_back({
			3, // more important than the mode
			cursorInfo,
			0.2 // left side, ideally farther left
		});
	}else if (auto tw = dynamic_cast<TerminalWidget*>(App::activeLeafNode)) {
		items.push_back({
			3, // higher importance
			MST::toMonoString(tw->term->shellStr),
			0 // far left side
		});
	}else if (auto gw = dynamic_cast<GraphWindow*>(App::activeLeafNode)) {
		items.push_back({
			3, // higher importance
			MST::toMonoString("("+std::to_string(gw->xmin)+", "+std::to_string(gw->ymin)+") -> ("+std::to_string(gw->xmax)+", "+std::to_string(gw->ymax)+")"),
			0 // far left side
		});
	}
	
	if (auto e = dynamic_cast<Editor*>(App::activeEditor)) {
		auto w = e->editors.find(e->tab_bar->selected_id);
		
		double side = 0; // far left (for hexeditor and imageview which have no other items on the left
		
		std::string filepath;
		MST::MonoString filename;
		if (w != e->editors.end()) {
			if (auto ce = dynamic_cast<CodeEdit*>(w->second)) {
				if (ce->file && ce->file->filename != "") {
					filename = MST::toMonoString(ce->file->filename);
					if (ce->madeChangeBetweenSaves) {
						filename += MST::toMonoString("*");
					}
					filepath = ce->file->filepath;
				}else{
					filename = MST::toMonoString("Untitled*");
				}
				side = 1.2; // center side, ideally farther left
			}else if (auto iv = dynamic_cast<ImageView*>(w->second)) {
				if (iv->file && iv->file->filename != "") {
					filename = MST::toMonoString(iv->file->filename);
					filepath = iv->file->filepath;
				}else{
					filename = MST::toMonoString("Untitled"); // not sure how you'd get here...
				}
				items.push_back({
					2.6, // more important than the filename
					MST::toMonoString(std::to_string(iv->imgW)+"x"+std::to_string(iv->imgH)),
					1.5 // in the center
				});
			}else if (auto he = dynamic_cast<HexEditor*>(w->second)) {
				if (he->file && he->file->filename != "") {
					filename = MST::toMonoString(he->file->filename);
					filepath = he->file->filepath;
				}else{
					filename = MST::toMonoString("Untitled"); // not sure how you'd get here...
				}
			}
		}
		
		items.push_back({
			2.5, // more important than the mode, less important than position in file
			filename,
			side
		});
		
		if (!filepath.empty()) {
			std::filesystem::path p(filepath);
			if (std::filesystem::exists(p)) {
				try {
					auto size = std::filesystem::file_size(p);
					std::string sizeStr;
					if (size < 1024) sizeStr = std::to_string(size) + " B";
					else if (size < 1024 * 1024) sizeStr = std::to_string(size / 1024) + " KB";
					else if (size < 1024 * 1024 * 1024) sizeStr = std::to_string(size / (1024 * 1024)) + " MB";
					else sizeStr = std::to_string(size / (1024 * 1024 * 1024)) + " GB";
					items.push_back({
						2.4,
						MST::toMonoString(sizeStr),
						side + 0.1
					});
				} catch (...) {}
			}
		}
	}
	
	std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
		return a.importance > b.importance;
	});
	
	
	int avail = t_w-App::text_padding*2;
	int sep = TextRenderer::get_text_width(3);
	
	std::vector<Item> todraw;
	
	for (Item i : items) {
		int width = TextRenderer::get_text_width(i.text.length);
		
		if (TextRenderer::get_text_width(i.text.length) < avail) {
			todraw.push_back(i);
			avail -= (width + sep);
		}
	}
	
	std::sort(todraw.begin(), todraw.end(), [](const Item& a, const Item& b) {
		return a.side < b.side;
	});
	
	std::vector<MST::MonoString> center;
	int centerwidth = -sep;
	
	int left = t_x+App::text_padding;
	int right = t_x+t_w-App::text_padding;
	int top = t_y + t_h/2 - TextRenderer::get_text_height()/2;
	
	for (Item i : todraw) {
		int width = TextRenderer::get_text_width(i.text.length);
		
		if (i.side >= 0 && i.side < 1) { // left
			TextRenderer::draw_text(left, top, i.text, App::theme.main_text_color);
			left += width + sep;
		}else if (i.side >= 2) { // right
			right -= width;
			TextRenderer::draw_text(right, top, i.text, App::theme.main_text_color);
			right -= sep;
		}else {
			center.push_back(i.text);
			centerwidth += width + sep;
		}
	}
	
	int centerpos = std::max(left, (right+left - centerwidth)/2);
	int possible_left = t_x+t_w/2-centerwidth/2;
	int possible_right = possible_left+centerwidth;
	if (possible_left > left && possible_right < right) { // if we can put it in the middle safely, just do that.
		centerpos = possible_left;
	}
	
	
	for (const auto& text : center) {
		TextRenderer::draw_text(centerpos, top, text, App::theme.main_text_color);
		centerpos += TextRenderer::get_text_width(text.length) + sep;
	}
	
	Widget::render();
	
//	if (rounded) {
//		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
//		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
//	}else{
//		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
//	}
}
