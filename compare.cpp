#include "compare.h"
#include "application.h"
#include "textedit.h"
#include "linenumbers.h"
#include "tinyfiledialogs.h"
#include "helper_types.h"

#include <unicode/regex.h>
#include <unicode/stringoptions.h>

Compare::Compare(Widget* parent, App::PosFunction positioner) : Widget(parent) {
	POS_FUNC = positioner;
	
	id = MST::toMonoString("Compare");
	
	line_numbers = new LineNumbers(this);
	
	f1Button = new Button(this, MST::toMonoString("Select File 1"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2-w-App::text_padding/2;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
		if (FileBackends::isRemote()) {
			App::requestString("First remote file?", App::settings->getValue("current_folder", FileBackends::current()->homeDirectory()),
				[this, btn](MST::MonoString selected) {
					const std::string filePath = MST::toString(selected);
					if (filePath.empty()) return;
					const std::string filename = FileBackends::current()->filename(filePath);
					f1 = new FileInfo();
					f1->filepath = filePath;
					f1->filename = filename;
					btn->BUTTON_LABEL = MST::toMonoString(filename);
					cb1Button->BUTTON_LABEL = MST::toMonoString("Use Clipboard");
					reload();
				});
			return;
		}
		const char * fp1 = tinyfd_openFileDialog(
			"Select first file", // dialog title
			"",                  // default path and filename
			0, NULL, NULL,       // filter count and filters
			0                    // allow multiple selections (0 = no)
		);
		
		if (!fp1) { return; }
		
		std::string filePath(fp1);
		
		std::filesystem::path fullPath = filePath;
		std::string filename = fullPath.filename().string();
		
		f1 = new FileInfo();
		f1->filepath = filePath;
		f1->filename = filename;
		
		btn->BUTTON_LABEL = MST::toMonoString(filename);
		cb1Button->BUTTON_LABEL = MST::toMonoString("Use Clipboard");
		
		reload();
	});
	
	cb1Button = new Button(this, MST::toMonoString("Use Clipboard"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = f1Button->t_x-w-App::text_padding;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
		clipBoardText1 = MST::replaceAll(MST::toMonoString(GetClipboardText()), MST::toMonoString("\r\n"), MST::toMonoString("\n"));
		
		f1 = new FileInfo();
		f1->filepath = "";
		f1->filename = "";
		
		btn->BUTTON_LABEL = MST::toMonoString("Coppied!");
		f1Button->BUTTON_LABEL = MST::toMonoString("Select File 1");
		
		reload();
	});
	
	f2Button = new Button(this, MST::toMonoString("Select File 2"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2+App::text_padding/2;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
		if (FileBackends::isRemote()) {
			App::requestString("Second remote file?", App::settings->getValue("current_folder", FileBackends::current()->homeDirectory()),
				[this, btn](MST::MonoString selected) {
					const std::string filePath = MST::toString(selected);
					if (filePath.empty()) return;
					const std::string filename = FileBackends::current()->filename(filePath);
					f2 = new FileInfo();
					f2->filepath = filePath;
					f2->filename = filename;
					btn->BUTTON_LABEL = MST::toMonoString(filename);
					cb2Button->BUTTON_LABEL = MST::toMonoString("Use Clipboard");
					reload();
				});
			return;
		}
		const char * fp2 = tinyfd_openFileDialog(
			"Select second file", // dialog title
			"",                   // default path and filename
			0, NULL, NULL,        // filter count and filters
			0                     // allow multiple selections (0 = no)
		);
		
		if (!fp2) { return; }
		
		std::string filePath(fp2);
		
		std::filesystem::path fullPath = filePath;
		std::string filename = fullPath.filename().string();
		
		f2 = new FileInfo();
		f2->filepath = filePath;
		f2->filename = filename;
		
		btn->BUTTON_LABEL = MST::toMonoString(filename);
		cb2Button->BUTTON_LABEL = MST::toMonoString("Use Clipboard");
		
		reload();
	});
	
	cb2Button = new Button(this, MST::toMonoString("Use Clipboard"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = f2Button->t_x+f2Button->t_w+App::text_padding;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
		clipBoardText2 = MST::replaceAll(MST::toMonoString(GetClipboardText()), MST::toMonoString("\r\n"), MST::toMonoString("\n"));
		
		f2 = new FileInfo();
		f2->filepath = "";
		f2->filename = "";
		
		btn->BUTTON_LABEL = MST::toMonoString("Coppied!");
		f2Button->BUTTON_LABEL = MST::toMonoString("Select File 2");
		
		reload();
	});
	
	f1Button->rounded = true;
	f2Button->rounded = true;
	cb1Button->rounded = true;
	cb2Button->rounded = true;
	
	textedit = new TextEdit(this, [&](Widget* t){
		textedit->t_x = t_x+line_numbers->t_w;
		textedit->t_y = f1Button->t_h+f1Button->t_y+App::text_padding;
		textedit->t_w = t_w-line_numbers->t_w;
		textedit->t_h = (t_y+t_h) - (f1Button->t_h+f1Button->t_y+App::text_padding);
	});
	
	textedit->highlighter = nullptr;
	textedit->scrollbar_vertical = true;
	textedit->scrollbar_horizontal = true;
	textedit->alreadyHighlighted = true;
	
	line_numbers->setTextedit(textedit);
}

void Compare::setOnlyTo(FileInfo* f, int num) {
	bool worked = true;
	MST::MonoString txt;
	
	if (f->filepath == "") {
		if (num == 1) {
			txt = clipBoardText1;
		}else{
			txt = clipBoardText2;
		}
	}else{
		txt = App::readFileToMonoString(f->filepath, worked);
		if (!worked) { textedit->setFullText(MST::toMonoString("Failed to open file: " + f->filepath)); return; }
	}
	
	auto lines = MST::split(txt, U'\n');
	
	MST::MonoString text;
	const MST::MonoString newlinechar = MST::toMonoString(U'\n');
	
	for (int i = 0; i < lines.size(); i++) {
		text += MST::toMonoString("++ ")+lines[i];
		
		if (i < lines.size()-1) {
			text += newlinechar;
		}
	}
	
	textedit->setFullText(text);
	textedit->position(t_x, t_y, t_w, t_h);
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		CW_HighlightToken col;
		col.start_byte = 0;
		col.end_byte = textedit->lines[i].line_text.length+1;
		col.role = -1;
		
		textedit->lines[i].visual_length = textedit->getVisLen(textedit->lines[i].line_text);
		textedit->lines[i].tokens = {col};
		textedit->lines[i].changed = false;
		textedit->lines[i].highlightinguptodate = true;
	}
}

void Compare::reload() {
	App::time_till_regular = 2;
	textedit->DO_POSITION = true;
	App::rerender = true;
	
	if (!f1 && !f2) {
		textedit->setFullText(MST::MonoString());
		return;
	}else if (!f1) {
		setOnlyTo(f2, 2);
		return;
	}else if (!f2) {
		setOnlyTo(f1, 1);
		return;
	}
	
	MST::MonoString txt;
	MST::MonoString txt2;
	
	bool worked = true;
	if (f1->filepath != "") {
		txt = App::readFileToMonoString(f1->filepath, worked);
		if (!worked) { textedit->setFullText(MST::toMonoString("Failed to open file: " + f1->filepath)); return; }
	}else{
		txt = clipBoardText1;
	}
	
	if (f2->filepath != "") {
		txt2 = App::readFileToMonoString(f2->filepath, worked);
		if (!worked) { textedit->setFullText(MST::toMonoString("Failed to open file: " + f1->filepath)); return; }
	}else{
		txt2 = clipBoardText2;
	}
	
	auto l1 = MST::split(txt, U'\n');
	auto l2 = MST::split(txt2, U'\n');
	
	auto calcDiff = calculateDifferences(l1, l2);
	
	MST::MonoString text;
	const MST::MonoString newlinechar = MST::toMonoString(U'\n');
	
	for (int i = 0; i < calcDiff.size(); i++) {
		auto c = calcDiff[i];
		
		if (c.first == 2) {
			text += MST::toMonoString("== ")+c.second;
		}
		if (c.first == 1) {
			text += MST::toMonoString("-- ")+c.second;
		}
		if (c.first == 0) {
			text += MST::toMonoString("++ ")+c.second;
		}
		
		if (i < calcDiff.size()-1) {
			text += newlinechar;
		}
	}
	
	textedit->setFullText(text);
	textedit->position(t_x, t_y, t_w, t_h);
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		auto c = calcDiff[i];
		CW_HighlightToken col;
		col.start_byte = 0;
		col.end_byte = textedit->lines[i].line_text.length+1;
		col.role = (-c.first)-1;
		
		textedit->lines[i].visual_length = textedit->getVisLen(textedit->lines[i].line_text);
		textedit->lines[i].tokens = {col};
		textedit->lines[i].changed = false;
		textedit->lines[i].highlightinguptodate = true;
	}
}

void Compare::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.main_background_color);
	
	App::runWithSKIZ(f1Button->t_x, f1Button->t_y, f1Button->t_w, textedit->t_h, [&](){
		f1Button->render();
	});
	App::runWithSKIZ(f2Button->t_x, f2Button->t_y, f2Button->t_w, f2Button->t_h, [&](){
		f2Button->render();
	});
	App::runWithSKIZ(cb1Button->t_x, cb1Button->t_y, cb1Button->t_w, cb1Button->t_h, [&](){
		cb1Button->render();
	});
	App::runWithSKIZ(cb2Button->t_x, cb2Button->t_y, cb2Button->t_w, cb2Button->t_h, [&](){
		cb2Button->render();
	});
	App::runWithSKIZ(line_numbers->t_x, line_numbers->t_y, line_numbers->t_w, textedit->t_h, [&](){
		line_numbers->render();
	});
	App::runWithSKIZ(textedit->t_x, textedit->t_y, textedit->t_w, textedit->t_h, [&](){
		textedit->render();
	});
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, textedit->t_y, t_w, textedit->t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, textedit->t_y, t_w, textedit->t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, textedit->t_y, t_w, textedit->t_h, App::theme.border);
	}
}

void Compare::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	POS_FUNC(this);
	
	f1Button->position(t_x, t_y, t_w, t_h);
	f2Button->position(t_x, t_y, t_w, t_h);
	cb1Button->position(t_x, t_y, t_w, t_h);
	cb2Button->position(t_x, t_y, t_w, t_h);
	line_numbers->position(t_x, t_y, t_w, t_h);
	textedit->position(t_x, t_y, t_w, t_h);
}

bool Compare::on_char_event(unsigned int keycode) {
	return false;
}

bool Compare::on_key_event(int key, int scancode, int action, int mods) {
	return false;
}

bool Compare::on_mouse_button_event(int button, int action, int mods) {
	if (f1Button->on_mouse_button_event(button, action, mods)) { return true; }
	if (f2Button->on_mouse_button_event(button, action, mods)) { return true; }
	if (cb1Button->on_mouse_button_event(button, action, mods)) { return true; }
	if (cb2Button->on_mouse_button_event(button, action, mods)) { return true; }
	if (textedit->scrollbar_v->on_mouse_button_event(button, action, mods)) { return true; }
	if (textedit->scrollbar_h->on_mouse_button_event(button, action, mods)) { return true; }
	return false;
}

std::vector<std::pair<int,MST::MonoString>> Compare::calculateDifferences(const std::vector<MST::MonoString>& t1, const std::vector<MST::MonoString>& t2) {
	std::vector<std::vector<int>> cost(t1.size()+1, std::vector<int>(t2.size()+1));
	std::vector<std::vector<int>> direction(t1.size()+1, std::vector<int>(t2.size()+1));
	
	for (int i = 0; i <= t1.size(); i++) { cost[i][0] = i; direction[i][0] = 1; }
	for (int i = 0; i <= t2.size(); i++) { cost[0][i] = i; direction[0][i] = 0; }
	
	for (int i = 1; i <= t1.size(); i++) {
		for (int j = 1; j <= t2.size(); j++) {
			if (t1[i-1] == t2[j-1]) {
				cost[i][j] = cost[i-1][j-1];
				direction[i][j] = 2;
			}else{
				cost[i][j] = cost[i][j-1]+1;
				direction[i][j] = 0;
				if (cost[i-1][j]+1 < cost[i][j]) {
					cost[i][j] = cost[i-1][j]+1;
					direction[i][j] = 1;
				}
			}
		}
	}
	
	std::vector<std::pair<int,MST::MonoString>> changes;
	
	int x, y;
	x = t1.size();
	y = t2.size();
	
	while (true){
		if (x == 0 && y == 0) { break; }
		
		int d = direction[x][y];
		
		if (d == 2) {
			x -= 1;
			y -= 1;
			changes.push_back({d, t1[x]});
		}else if (d == 1) {
			x -= 1;
			changes.push_back({d, t1[x]});
		}else {
			y -= 1;
			changes.push_back({d, t2[y]});
		}
	}
	
	std::reverse(changes.begin(), changes.end());
	
	return changes;
}
