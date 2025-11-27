#include "compare.h"
#include "application.h"
#include "textedit.h"
#include "linenumbers.h"
#include "tinyfiledialogs.h"

#include <fstream>
#include <unicode/regex.h>
#include <unicode/stringoptions.h>

Compare::Compare(Widget* parent, App::PosFunction positioner) : Widget(parent) {
	POS_FUNC = positioner;
	
	id = icu::UnicodeString::fromUTF8("Compare");
	
	line_numbers = new LineNumbers(this);
	
	f1Button = new Button(this, icu::UnicodeString::fromUTF8("Select File 1"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2-w-App::text_padding/2;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
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
		
		btn->BUTTON_LABEL = icu::UnicodeString::fromUTF8(filename);
		
		reload();
	});
	
	f2Button = new Button(this, icu::UnicodeString::fromUTF8("Select File 2"), [&](Button* btn, int x, int y, int av_width, int av_height, int w, int h){
		// position
		btn->t_x = t_x+t_w/2+App::text_padding/2;
		btn->t_y = t_y+App::text_padding;
	},  [&](Button* btn){
		// onclick
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
		
		btn->BUTTON_LABEL = icu::UnicodeString::fromUTF8(filename);
		
		reload();
	});
	
	f1Button->border = true;
	f2Button->border = true;
	
	textedit = new TextEdit(this, [&](Widget* t){
		textedit->t_x = t_x+line_numbers->t_w;
		textedit->t_y = f1Button->t_h+f1Button->t_y+App::text_padding;
		textedit->t_w = t_w-line_numbers->t_w;
		textedit->t_h = (t_y+t_h) - (f1Button->t_h+f1Button->t_y+App::text_padding);
	});
	
	textedit->highlighter = nullptr;
	textedit->scrollbar_vertical = true;
	textedit->getblankhighlighting = nullptr;
	textedit->highlighterNotEqual = nullptr;
	textedit->alreadyHighlighted = true;
	
	line_numbers->setTextedit(textedit);
}

void Compare::setOnlyTo(FileInfo* f) {
	bool worked = true;
	auto txt = App::readFileToUnicodeString(f->filepath, worked);
	if (!worked) { textedit->setFullText(icu::UnicodeString::fromUTF8("Failed to open file: " + f->filepath)); return; }
	
	auto lines = splitByChar(txt, U'\n');
	
	icu::UnicodeString text;
	UChar32 newlinechar = U'\n';
	
	for (int i = 0; i < lines.size(); i++) {
		text += icu::UnicodeString::fromUTF8("++ ")+lines[i];
		
		if (i < lines.size()-1) {
			text.append(newlinechar);
		}
	}
	
	textedit->setFullText(text);
	textedit->position(t_x, t_y, t_w, t_h);
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		ColoredTokens col;
		col.start = 0;
		col.end = textedit->lines[i].line_text.length()+1;
		col.color = 1;
		
		textedit->lines[i].tokens = {col};
		textedit->lines[i].changed = false;
		textedit->lines[i].highlightinguptodate = true;
	}
}

void Compare::reload() {
	if (!f1 && !f2) {
		textedit->setFullText(icu::UnicodeString());
		return;
	}else if (!f1) {
		setOnlyTo(f2);
		return;
	}else if (!f2) {
		setOnlyTo(f1);
		return;
	}
	
	bool worked = true;
	auto txt = App::readFileToUnicodeString(f1->filepath, worked);
	if (!worked) { textedit->setFullText(icu::UnicodeString::fromUTF8("Failed to open file: " + f1->filepath)); return; }
	auto txt2 = App::readFileToUnicodeString(f2->filepath, worked);
	if (!worked) { textedit->setFullText(icu::UnicodeString::fromUTF8("Failed to open file: " + f1->filepath)); return; }
	
	auto l1 = splitByChar(txt, U'\n');
	auto l2 = splitByChar(txt2, U'\n');
	
	auto calcDiff = calculateDifferences(l1, l2);
	
	icu::UnicodeString text;
	UChar32 newlinechar = U'\n';
	
	for (int i = 0; i < calcDiff.size(); i++) {
		auto c = calcDiff[i];
		
		if (c.first == 2) {
			text += icu::UnicodeString::fromUTF8("== ")+c.second;
		}
		if (c.first == 1) {
			text += icu::UnicodeString::fromUTF8("-- ")+c.second;
		}
		if (c.first == 0) {
			text += icu::UnicodeString::fromUTF8("++ ")+c.second;
		}
		
		if (i < calcDiff.size()-1) {
			text.append(newlinechar);
		}
	}
	
	textedit->setFullText(text);
	textedit->position(t_x, t_y, t_w, t_h);
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		auto c = calcDiff[i];
		ColoredTokens col;
		col.start = 0;
		col.end = textedit->lines[i].line_text.length()+1;
		
		col.color = (-c.first)-1;
		
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
	App::runWithSKIZ(line_numbers->t_x, line_numbers->t_y, line_numbers->t_w, textedit->t_h, [&](){
		line_numbers->render();
	});
	App::runWithSKIZ(textedit->t_x, textedit->t_y, textedit->t_w, textedit->t_h, [&](){
		textedit->render();
	});
}

void Compare::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	POS_FUNC(this);
	
	f1Button->position(t_x, t_y, t_w, t_h);
	f2Button->position(t_x, t_y, t_w, t_h);
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
	return textedit->scrollbar_v->on_mouse_button_event(button, action, mods);
}

std::vector<std::pair<int,icu::UnicodeString>> Compare::calculateDifferences(const std::vector<icu::UnicodeString>& t1, const std::vector<icu::UnicodeString>& t2) {
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
	
	std::vector<std::pair<int,icu::UnicodeString>> changes;
	
	int x, y;
	x = t1.size();
	y = t2.size();
	
	while (true){
		if (x == 0 && y == 0) { break; }
		
		int d = direction[x][y];
		
		std::string l1, l2;
		t1[x-1].toUTF8String(l1);
		t2[y-1].toUTF8String(l2);
		
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