#include "textedit.h"
#include "text_renderer.h"
#include "application.h"
#include <set>
#include <chrono>
#include "helper_types.h"
#include <unicode/uchar.h>

#define CODEWIZARD_WORD_WRAP 10000
#define CODEWIZARD_MATCHING_BRACKET_LEFT 10001
#define CODEWIZARD_MATCHING_BRACKET_RIGHT 10002

std::set<UChar32> whitespace = {0x20, 0x09, 0x0A, 0x0D, 0x00A0, 0x2028, 0x2029};
std::set<UChar32> numeric = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
std::set<UChar32> allowed_in_var_names = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5F};
std::set<UChar32> punctuationset = {U'!', U'#', U'$', U'%', U'&', U'(', U')', U'*', U'+', U',', U'-', U'.', U'/', U':', U';', U'<', U'=', U'>', U'?', U'@', U'[', U'\\', U']', U'^', U'`', U'{', U'|', U'}', U'~'};

icu::UnicodeString openers = icu::UnicodeString("({[<");
icu::UnicodeString closers = icu::UnicodeString(")}]>");

std::vector<int> DIGITS_KEYS = {GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9};

TextEdit::TextEdit(Widget* parent, App::PosFunction fnct) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("TextEdit");
	
	POS_FUNC = fnct;
	
	Line ln = Line();
	ln.line_text = icu::UnicodeString::fromUTF8("");
	ln.tokens = {};
	changed_during_update = true;
	
	lines = {ln}; // one empty line.
	cursors = {Cursor()}; // we have to set these two before we init the undo history.
	historyThisUpdate = createHistory();
	
	draw_cursor = {};
	draw_diagnostics = {};
}

void TextEdit::setFullText(icu::UnicodeString text) {
	auto lns = splitByChar(text, U'\n');
	
	lines = {};
	
	for (auto l : lns) {
		Line line;
		line.line_text = l;
		line.tokens = {};
		line.changed = true;
		lines.push_back(line);
	}
	
	undo_stack.clear();
	redo_stack.clear();
	historyThisUpdate = createHistory();
	
	cursors = {Cursor()};
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

History TextEdit::createHistory() {
	History hs;
	hs.millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	hs.cursors_before = cursors;
	hs.cursors_after = {};
	hs.edits = {};
	return hs;
}

void TextEdit::Highlight(int first_visible_line, int last_visible_line) {
	max_line_len = 0;
	
	for (int i = first_visible_line; i < last_visible_line; i ++) {
		Line* line = &lines[i];
		
		if (line->changed) {
			int ln = 0;
			
			for (int c_indx = 0; c_indx < line->line_text.length(); c_indx++) {
				UChar32 c = line->line_text.char32At(c_indx);
				
				if (c == U'\t') {
					ln += 4;
				}else{
					ln ++;
				}
			}
			
			line->visual_length = ln;
		}
		
		if (line->visual_length > max_line_len) {
			max_line_len = line->visual_length;
		}
	}
	
	if (highlighter) {
		auto first_info = getblankhighlighting();
		
		int highlighted = 0;
		
		for (int i = 0; i < lines.size(); i ++) {
			Line* line = &lines[i];
			
			if (line->changed) { // I know - DRY and all that. (we have to do it here because the previous one doesn't loop over all of them, and this one sets changed = false)
				int ln = 0;
				line->changed = false;
				line->highlightinguptodate = false;
				
				for (int c_indx = 0; c_indx < line->line_text.length(); c_indx++) {
					UChar32 c = line->line_text.char32At(c_indx);
					
					if (c == U'\t') {
						ln += 4;
					}else{
						ln ++;
					}
				}
				
				line->visual_length = ln;
			}
			
			TextMateInfo* prev_line_info;
			
			if (i == 0) {
				prev_line_info = &first_info;
			}else{
				prev_line_info = &lines[i-1].after_line_colored;
			}
			
			if (!line->highlightinguptodate || prev_line_info->contextStack.back().hash != line->prev_hash) {
				auto ln = highlighter(line->line_text, *prev_line_info);
				
				if (i >= first_visible_line && i <= last_visible_line) {
					App::rerender = true; // this changed a viewport thing, rerender.
				}else{
					App::forceWaitTime = false; // we don't force a wait time when we need to highlight
				}
				
				line->after_line_colored = ln.lineInfo;
				line->prev_hash = prev_line_info->contextStack.back().hash;
				line->tokens = ln.tokens;
				line->highlightinguptodate = true;
				highlighted ++;
				
				if (i < first_visible_line && highlighted > 40) {
					break;
				}else if (i >= first_visible_line && highlighted > 10) {
					break; // we'll break faster if we're past the first visible line. (because it's slow.)
				}
			}
		}
	}else {
		for (int i = 0; i < last_visible_line; i ++) {
			auto line = lines[i];
			if (!line.highlightinguptodate){
				line.tokens = {};
				line.changed = false;
				line.highlightinguptodate = true;
				lines[i] = line;
			}
		}		
	}
}

void TextEdit::updateUndoHistory() {
	historyThisUpdate.cursors_after = cursors;
	
	if (historyThisUpdate.edits.size() == 0) {
		historyThisUpdate = createHistory();
		return;
	}
	
	if (undo_stack.empty()) {
		undo_stack.push_back(historyThisUpdate);
		historyThisUpdate = createHistory();
		return;
	}
	
	auto last_millis = undo_stack.back().millis;
	
	if (historyThisUpdate.millis-last_millis > 300) { // save every ___ millis
		undo_stack.push_back(historyThisUpdate);
	}else{
		for (auto i : historyThisUpdate.edits) {
			undo_stack.back().edits.push_back(i);
		}
		undo_stack.back().millis = historyThisUpdate.millis;
		undo_stack.back().cursors_after = historyThisUpdate.cursors_after;
	}
	
	redo_stack.clear();
	
	historyThisUpdate = createHistory();
}

void TextEdit::activateUndo() {
	if (undo_stack.empty()) {
		std::cout << "No undo stack\n";
		return;
	}
	
	auto hs = undo_stack.back();
	
	History redoHistory;
	redoHistory.millis = historyThisUpdate.millis;
	redoHistory.cursors_after = hs.cursors_after;
	redoHistory.cursors_before = hs.cursors_before;
	
	undo_stack.pop_back();
	
	for (int i = hs.edits.size()-1; i >= 0; i--) {
		auto e = hs.edits[i];
		
		if (e.type == EditType::ChangeLine) {
			redoHistory.edits.push_back( { EditType::ChangeLine, lines[e.index], e.index } );
			
			lines[e.index] = e.line;
		}else if (e.type == EditType::DeleteLine) { // we need to reinsert it
			redoHistory.edits.push_back( { EditType::InsertLine, e.line, e.index } );
			
			lines.insert(lines.begin()+e.index, e.line);
		}else if (e.type == EditType::InsertLine) { // let's go ahead and delete that sucker
			redoHistory.edits.push_back( { EditType::DeleteLine, lines[e.index], e.index } );
			
			lines.erase(lines.begin()+e.index);
		}
	}
	
	redo_stack.push_back(redoHistory);
	
	cursors = hs.cursors_before;
	
	tryingToEnsureCursorPos = true;
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

void TextEdit::activateRedo() {
	if (redo_stack.empty()) {
		std::cout << "No redo stack\n";
		return;
	}
	
	auto hs = redo_stack.back();
	
	History undoHistory;
	undoHistory.millis = historyThisUpdate.millis;
	undoHistory.cursors_before  = hs.cursors_before;
	undoHistory.cursors_after = hs.cursors_after;
	
	redo_stack.pop_back();
	
	for (int i = hs.edits.size()-1; i >= 0; i--) {
		auto e = hs.edits[i];
		
		if (e.type == EditType::ChangeLine) {
			undoHistory.edits.push_back( { EditType::ChangeLine, lines[e.index], e.index } );
			
			lines[e.index] = e.line;
		}else if (e.type == EditType::DeleteLine) { // we need to reinsert it
			undoHistory.edits.push_back( { EditType::InsertLine, e.line, e.index } );
			
			lines.insert(lines.begin()+e.index, e.line);
		}else if (e.type == EditType::InsertLine) { // let's go ahead and delete that sucker
			undoHistory.edits.push_back( { EditType::DeleteLine, lines[e.index], e.index } );
			
			lines.erase(lines.begin()+e.index);
		}
	}
	
	undo_stack.push_back(undoHistory);
	
	cursors = hs.cursors_after;
	
	tryingToEnsureCursorPos = true;
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

int TextEdit::charType(UChar32 c) {
	if (whitespace.count(c)) {return 0;}
	if (allowed_in_var_names.count(c)) {return 1;}
	return 2;
}

int TextEdit::_findNextWord(Cursor c, int dir){
	auto line = lines[c.head_line].line_text;
	
	int location = c.head_char;
	
	if (dir == -1) {
		location --;
		int toc = charType(line.char32At(location));
		bool notseenwhite = (toc != 0);
		
		while (true) {
			location --;
			
			if (location < 0) {
				location = 0;
				break;
			}
			
			auto ntoc = charType(line.char32At(location));
			
			if (!notseenwhite && ntoc != 0) {
				notseenwhite = true;
				toc = ntoc;
			}
			
			if (ntoc != toc) {
				location ++;
				break;
			}
		}
	}else{
		int toc = charType(line.char32At(location));
		bool seenwhite = (toc == 0);
		
		while (true) {
			location ++;
			
			if (location >= line.length()) {
				break;
			}
			
			auto ntoc = charType(line.char32At(location));
			
			if (!seenwhite && ntoc == 0) {
				seenwhite = true;
				toc = ntoc;
			}
			
			if (ntoc != toc) {
				break;
			}
		}
	}
	
	return location;
}

std::pair<std::pair<int,int>,std::pair<int,int>> TextEdit::_getCursSelec(Cursor c){
	int start_line = c.anchor_line;
	int end_line = c.head_line;
	
	int start_char = c.anchor_char;
	int end_char = c.head_char;
	
	if (start_line > end_line) {
		std::swap(start_line, end_line);
		std::swap(start_char, end_char);
	}else if (start_line == end_line && start_char > end_char) {
		std::swap(start_char, end_char);
	}
	
	return { {start_line, end_line}, {start_char, end_char} };
}

std::pair<int,int> TextEdit::findMatchingBracket(int type, int direction, int line, int col) {
	int count = 0;
	
	while (true) {
		UChar32 c = lines[line].line_text.char32At(col);
		
		if (direction == 1) {
			if (openers.indexOf(c) == type) {
				count ++;
			}else if (closers.indexOf(c) == type) {
				count --;
			}
		}else{
			if (openers.indexOf(c) == type) {
				count --;
			}else if (closers.indexOf(c) == type) {
				count ++;
			}
		}
		
		if (count == 0) {
			return {line, col};
		}
		
		col += direction;
		if (col < 0) {
			line --;
			if (line < 0) {
				break;
			}
			col = lines[line].line_text.length()-1;
		}else if (col >= lines[line].line_text.length()) {
			line ++;
			if (line >= lines.size()) {
				break;
			}
			col = 0;
		}
	}
	
	return {-1, -1};
}

Cursor TextEdit::applyMoveToCursor(Cursor c, int key, bool shift, bool control) {
	bool cursor_has_selection = (c.anchor_char != c.head_char || c.anchor_line != c.head_line);
	
	auto slelscec = _getCursSelec(c);
	
	int start_line = slelscec.first.first;
	int end_line = slelscec.first.second;
	
	int start_char = slelscec.second.first;
	int end_char = slelscec.second.second;
	
	auto c_line = lines[c.head_line].line_text;
	
	bool special_exceptions = false;
	
	if (key == GLFW_KEY_LEFT) {
		if (cursor_has_selection && !shift) {
			c.head_line = start_line;
			c.head_char = start_char;
			c.anchor_line = start_line;
			c.anchor_char = start_char;
		}else if (c.head_char == 0) {
			if (c.head_line == 0) {
				c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
				return c;
			}
			c.head_line -= 1;
			c.head_char = lines[c.head_line].line_text.length();
		}else{
			int new_x = c.head_char;
			
			if (!control) {
				new_x = c.head_char - 1;
			}else {
				new_x = _findNextWord(c, -1); // to be addressed
			}
			
			c.head_char = new_x;
		}
		
		c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
	}else if (key == GLFW_KEY_RIGHT) {
		if (cursor_has_selection && !shift) {
			c.head_line = end_line;
			c.head_char = end_char;
			c.anchor_line = end_line;
			c.anchor_char = end_char;
		}else if (c.head_char == lines[c.head_line].line_text.length()) {
			if (c.head_line == lines.size()-1) {
				c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
				return c;
			}
			c.head_line += 1;
			c.head_char = 0;
		}else{
			int new_x = c.head_char;
			
			if (!control) {
				new_x = c.head_char + 1;
			}else {
				new_x = _findNextWord(c, 1); // to be addressed
			}
			
			c.head_char = new_x;
		}
		
		c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
	}else if (key == GLFW_KEY_DOWN) {
		if (c.head_line == lines.size()-1) {
			c = applyMoveToCursor(c, GLFW_KEY_END, shift, control);
			return c;
		}
		c.head_line += 1;
		
		if (c.preffered_collumn != -1) {
			c.head_char = _mapFromVisualToReal(c.head_line, c.preffered_collumn);
		}
	}else if (key == GLFW_KEY_UP) {
		if (c.head_line == 0) {
			c = applyMoveToCursor(c, GLFW_KEY_HOME, shift, control);
			return c;
		}
		c.head_line -= 1;
		
		if (c.preffered_collumn != -1) {
			c.head_char = _mapFromVisualToReal(c.head_line, c.preffered_collumn);
		}
	}else if (key == GLFW_KEY_HOME) {
		c.head_char = 0;
		c.preffered_collumn = c.head_char;
	}else if (key == GLFW_KEY_END) {
		c.head_char = lines[c.head_line].line_text.length();
		c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
	}else if (key == CODEWIZARD_WORD_WRAP) {
		special_exceptions = true;
		
		UChar32 start_char;
		if (c.head_char == lines[c.head_line].line_text.length()) {
			start_char = U'\n';
		}else{
			start_char = lines[c.head_line].line_text.char32At(c.head_char);
		}
		
		int matching_type = charType(start_char);
		
		auto slelscec = _getCursSelec(c);
		
		int end_left_c = c.head_char;
		int end_left_l = c.head_line;
		while (true) {
			int eval_c = end_left_c;
			int eval_l = end_left_l;
			
			eval_c --;
			if (eval_c < 0) {
				eval_l --;
				
				if (eval_l < 0) {
					break;
				}
				
				eval_c = lines[eval_l].line_text.length();
			}
			
			UChar32 thischar;
			if (eval_c == lines[eval_l].line_text.length()) {
				thischar = U'\n';
			}else{
				thischar = lines[eval_l].line_text.char32At(eval_c);
			}
			
			int type = charType(thischar);
			
			if (type != matching_type) {
				break;
			}
			
			end_left_c = eval_c;
			end_left_l = eval_l;
		}
		
		int end_right_c = c.head_char;
		int end_right_l = c.head_line;
		while (true) {
			int eval_c = end_right_c;
			int eval_l = end_right_l;
			
			eval_c ++;
			if (eval_c > lines[eval_l].line_text.length()) {
				eval_l ++;
				
				if (eval_l >= lines.size()) {
					break;
				}
				
				eval_c = 0;
			}
			
			UChar32 thischar;
			if (eval_c == lines[eval_l].line_text.length()) {
				thischar = U'\n';
			}else{
				thischar = lines[eval_l].line_text.char32At(eval_c);
			}
			
			int type = charType(thischar);
			
			end_right_c = eval_c;
			end_right_l = eval_l;
			
			if (type != matching_type) {
				break;
			}
		}
		
		if (end_right_l >= lines.size()) {
			end_right_l = lines.size();
		}
		
		if (end_right_c > lines[end_right_l].line_text.length()) {
			end_right_c = lines[end_right_l].line_text.length();
		}
		
		c.head_char = end_right_c;
		c.head_line = end_right_l;
		c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
		
		if (shift && (slelscec.first.first < end_left_l || (slelscec.first.first == end_left_l && slelscec.second.first < end_left_c))) {
			// we started farther left and we're shifting
			c.anchor_line = slelscec.first.first;
			c.anchor_char = slelscec.second.first;
		}else{
			c.anchor_line = end_left_l;
			c.anchor_char = end_left_c;
		}
	}else if (key == CODEWIZARD_MATCHING_BRACKET_LEFT) {
		UChar32 left_of = U' ';
		if (c.head_char-1 >= 0){
			left_of = lines[c.head_line].line_text.char32At(c.head_char-1);
		}
		UChar32 right_of = U' ';
		if (c.head_char < lines[c.head_line].line_text.length()){
			right_of = lines[c.head_line].line_text.char32At(c.head_char);
		}
		
		std::pair<int,int> res = {-1, -1};
		
		auto indx_r = closers.indexOf(right_of);
		auto indx_l = closers.indexOf(left_of);
		
		if (indx_r != -1) {
			res = findMatchingBracket(indx_r, -1, c.head_line, c.head_char);
		}else if (indx_l != -1) {
			res = findMatchingBracket(indx_l, -1, c.head_line, c.head_char-1);
		}
		
		if (res.first != -1 && res.second != -1) {
			c.head_line = res.first;
			c.head_char = res.second+1;
		}
	}else if (key == CODEWIZARD_MATCHING_BRACKET_RIGHT) {
		UChar32 left_of = U' ';
		if (c.head_char-1 >= 0){
			left_of = lines[c.head_line].line_text.char32At(c.head_char-1);
		}
		UChar32 right_of = U' ';
		if (c.head_char < lines[c.head_line].line_text.length()){
			right_of = lines[c.head_line].line_text.char32At(c.head_char);
		}
		
		std::pair<int,int> res = {-1, -1};
		
		auto indx_r = openers.indexOf(right_of);
		auto indx_l = openers.indexOf(left_of);
		
		if (indx_l != -1) {
			res = findMatchingBracket(indx_l, 1, c.head_line, c.head_char-1);
		}else if (indx_r != -1) {
			res = findMatchingBracket(indx_r, 1, c.head_line, c.head_char);
		}
		
		if (res.first != -1 && res.second != -1) {
			c.head_line = res.first;
			c.head_char = res.second;
		}
	}
	
	if (!shift && !special_exceptions) {
		c.anchor_char = c.head_char;
		c.anchor_line = c.head_line;
	}
	
	return c;
}

int TextEdit::_mapFromRealToVisual(int line, int c) {
	auto str = lines[line].line_text;
	
	if (c == 0) {
		return 0;
	}
	
	int visual_loc = 0;
	
	for (int i = 0; i < str.length(); i++) {
		UChar32 chr = str.char32At(i);
		
		if (chr == U'\t') {
			visual_loc += 4;
		}else{
			visual_loc += 1;
		}
		
		if (i+1 == c) {
			return visual_loc;
		}
	}
	
	return visual_loc;
}

int TextEdit::_mapFromVisualToReal(int line, int c) {
	auto str = lines[line].line_text;
	
	int visual_loc = 0;
	int real_loc = 0;
	
	if (c == 0 || str.length() == 0) {
		return 0;
	}
	
	for (int i = 0; i < str.length(); i++) {
		real_loc = i;
		
		UChar32 chr = str.char32At(real_loc);
		
		int new_vis = visual_loc;
		
		if (chr == U'\t') {
			new_vis += 4;
		}else{
			new_vis += 1;
		}
		
		if (new_vis == c) {
			return real_loc+1;
		}
		
		if (new_vis > c) {
			int d1 = c-visual_loc;
			int d2 = new_vis-c;
			
			if (d1 < d2) {
				return real_loc;
			}
			return real_loc+1;
		}
		
		visual_loc = new_vis;
	}
	
	return real_loc+1;
}

void TextEdit::applyMoveToAllCursors(int key, bool shift, bool control) {
	if (vim_repeater == 0) {
		vim_repeater = 1;
	}
	
	for (int i = 0; i < vim_repeater; i++) {
		for (int indx = 0; indx < cursors.size(); indx ++) {
			auto c = applyMoveToCursor(cursors[indx], key, shift, control);
			cursors[indx] = c;
		}
	}
	
	tryingToEnsureCursorPos = true;
	vim_repeater = 0;
}

void TextEdit::insertNewCursorDown() {
	Cursor lowest = cursors[0];
	for (auto c : cursors) {
		if (c.head_line > lowest.head_line) {
			lowest = c;
		}
	}
	
	auto nc = applyMoveToCursor(lowest, GLFW_KEY_DOWN, false, false);
	
	cursors.push_back(nc);
}

void TextEdit::insertNewCursorUp() {
	Cursor highest = cursors[0];
	for (auto c : cursors) {
		if (c.head_line < highest.head_line) {
			highest = c;
		}
	}
	
	auto nc = applyMoveToCursor(highest, GLFW_KEY_UP, false, false);
	
	cursors.push_back(nc);
}

void TextEdit::HandleOverlappingCursors() {
	std::vector<Cursor> new_list = {};
	
	for (int i = 0; i < cursors.size(); i++) {
		auto c = cursors[i];
		
		bool works = true;
		for (int j = 0; j < new_list.size(); j++) {
			auto b = new_list[j];
			
			if (c.head_char == b.head_char && c.anchor_char == b.anchor_char && c.anchor_line == b.anchor_line && c.head_line == b.head_line) {
				works = false;
				break;
			}
		}
		
		if (works) {
			new_list.push_back(c);
		}
	}
	
	cursors = new_list;
}

std::pair<int,int> TextEdit::_handleSectionRemoved(int l, int c, int sl, int el, int sc, int ec) {
	if (l < sl || (l == sl && c < sc)) { // before selection
		return {l, c};
	}
	
	bool in_selection = false;
	if (l == sl && sl == el && c >= sc && c <= ec) {
		in_selection = true;
	}if (sl != el) {
		if (l > sl && l < el) {
			in_selection = true;
		}else if (l == sl && c >= sc) {
			in_selection = true;
		}else if (l == el && c <= ec) {
			in_selection = true;
		}
	}
	
	if (in_selection) { // case 1 - in selection (move to start)
		return {sl, sc};
	}
	
	if (l == el) { // we already know it's not in the selection
		// this must be on the end line, to the right
		int new_x = sc + (c-ec);
		return {sl, new_x};
	}
	
	// now it must be on a line below the end line
	int new_line = l-(el-sl);
	return {new_line, c};
}

std::pair<int,int> TextEdit::_handleSectionAdded(int l, int c, int sl, int sc, int nl, int nc) {
	if (l < sl || (l == sl && c < sc)) { // before new text
		return {l, c};
	}
	
	if (l == sl && nl == 0) { // not adding a new line but we're on the insertion linek
		return {l, c+nc};
	}
	
	if (l == sl) { // on the line where we're inserting. (and we're adding more lines)
		return {l+nl, c+nc-sc};
	}
	
	return {l+nl, c};
}

void TextEdit::deleteTextAtCursor(Cursor c, int key, bool control) {
	auto curs_sel = _getCursSelec(c);
	
	int sl = curs_sel.first.first;
	int el = curs_sel.first.second;
	int sc = curs_sel.second.first;
	int ec = curs_sel.second.second;
	
	if (sl == el && sc == ec) { // if there's no selection - let's fix that.
		if (key == GLFW_KEY_BACKSPACE) {
			c = applyMoveToCursor(c, GLFW_KEY_LEFT, true, control);
		}else{ // delete
			c = applyMoveToCursor(c, GLFW_KEY_RIGHT, true, control);
		}
		
		curs_sel = _getCursSelec(c); // recapture selection
		
		sl = curs_sel.first.first;
		el = curs_sel.first.second;
		sc = curs_sel.second.first;
		ec = curs_sel.second.second;
		
		if (sl == el && sc == ec) {
			return; // there's nothing to delete in this direction
		}
	}
	
	// move cursors
	for (int i = 0; i < cursors.size(); i ++) {
		auto curs = cursors[i];
		auto nh = _handleSectionRemoved(curs.head_line, curs.head_char, sl, el, sc, ec);
		auto na = _handleSectionRemoved(curs.anchor_line, curs.anchor_char, sl, el, sc, ec);
		
		curs.head_line = nh.first;
		curs.head_char = nh.second;
		curs.anchor_line = na.first;
		curs.anchor_char = na.second;
		
		cursors[i] = curs;
	}
	
	// delete text
	icu::UnicodeString start = lines[sl].line_text.tempSubStringBetween(0, sc);
	icu::UnicodeString end;
	lines[el].line_text.extractBetween(ec, lines[el].line_text.length(), end);
	
	changed_during_update = true;
	
	Edit e = { EditType::ChangeLine, lines[sl], sl };
	historyThisUpdate.edits.push_back(e);
	
	lines[sl].line_text = start+end;
	lines[sl].changed = true;
	
	if (sl != el) {
		for (int j = el; j >= sl+1; j--) {
			Edit e = { EditType::DeleteLine, lines[j], j };
			historyThisUpdate.edits.push_back(e);
		}
		
		lines.erase(lines.begin() + sl + 1, lines.begin() + el+1);
	}
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

void TextEdit::applyDeleteToAllCursors(int key, bool control) {
	if (vim_repeater == 0) {
		vim_repeater = 1;
	}
	
	bool needtocall = false;
	if (!largereditblock) {
		needtocall = true;
		largereditblock = true;
	}
	
	for (int i = 0; i < vim_repeater; i++) {
		for (int i = 0; i < cursors.size(); i++) {
			auto c = cursors[i];
			deleteTextAtCursor(c, key, control);
		}
	}
	
	tryingToEnsureCursorPos = true;
	vim_repeater = 0;
	
	for (int i = 0; i < cursors.size(); i++) {
		cursors[i].preffered_collumn = _mapFromRealToVisual(cursors[i].head_line, cursors[i].head_char);
	}
	
	if (needtocall) {
		largereditblock = false;
		if (ontextchange) {
			ontextchange(this);
		}
	}
}

void TextEdit::applyIndentChangeToCursor(Cursor c, int change_by) {
	auto sel = _getCursSelec(c);
	
	int sl = sel.first.first;
	int el = sel.first.second;
	
	UChar32 tab_char = U'\t';
	
	icu::UnicodeString new_text;
	if (change_by > 0) {
		for (int i = 0; i < change_by; i++) {
			new_text.append(tab_char);
		}
	}
	
	for (int line = sl; line <= el; line ++) {
		Cursor c = Cursor();
		c.anchor_line = line;
		c.head_line = line;
		c.anchor_char = 0;
		c.head_char = 0;
		
		if (change_by > 0){
			insertTextAtCursor(c, new_text);
		}else{
			auto line_text = lines[line].line_text;
			for (int c_indx = 0; c_indx < line_text.length(); c_indx++){
				if (c_indx >= -change_by) {
					break;
				}
				
				auto ch = line_text.char32At(c_indx);
				if (ch != tab_char) {
					break;
				}
				c.head_char ++;
			}
			if (c.head_char != 0) {
				deleteTextAtCursor(c, GLFW_KEY_BACKSPACE, false);
			}
		}
	}
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

void TextEdit::applyIndentChangeToAllCursors(int change_by) {
	if (vim_repeater == 0) {
		vim_repeater = 1;
	}
	
	bool needtocall = false;
	if (!largereditblock) {
		needtocall = true;
		largereditblock = true;
	}
	
	for (int i = 0; i < cursors.size(); i++) {
		auto c = cursors[i];
		applyIndentChangeToCursor(c, change_by*vim_repeater);
	}
	
	tryingToEnsureCursorPos = true;
	vim_repeater = 0;
	
	if (needtocall) {
		largereditblock = false;
		if (ontextchange) {
			ontextchange(this);
		}
	}
}

icu::UnicodeString TextEdit::getFullText() {
	icu::UnicodeString text;
	
	for (int i = 0; i < lines.size(); i++) {
		text += lines[i].line_text;
		if (i != lines.size()-1) {
			text += icu::UnicodeString::fromUTF8("\n");
		}
	}
	
	return text;
}

bool TextEdit::handleInsertKey(int key, int scancode, int action, int mods) {
	bool is_shift_held = ((mods & GLFW_MOD_SHIFT) != 0);
	bool is_control_held = ((mods & GLFW_MOD_CONTROL) != 0);
	bool is_alt_held = ((mods & GLFW_MOD_ALT) != 0);
	
	largereditblock = true;
	bool donesomthing = false;
	
	if (key == GLFW_KEY_TAB) {
		applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\t"));
		donesomthing = true;
	}else if (key == GLFW_KEY_SPACE) {
		applyInsertToAllCursors(icu::UnicodeString::fromUTF8(" "));
		donesomthing = true;
	}else if (key == GLFW_KEY_ENTER) {
		applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\n"));
		donesomthing = true;
	}
	
	if (key == GLFW_KEY_V && (is_control_held || mode == 'n')) {
		std::string clipboard_text = GetClipboardText();
		applyInsertToAllCursors(run_fixit_on_text(icu::UnicodeString::fromUTF8(clipboard_text)));
		donesomthing = true;
	}else if (key == GLFW_KEY_C && (is_control_held || mode == 'n')) {
		auto icutext = getSelectedText(cursors[0]);
		if (icutext.length() != 0) {
			std::string text;
			icutext.toUTF8String(text);
			SetClipboardText(text);
		}
		donesomthing = true;
	}else if (key == GLFW_KEY_X && (is_control_held || mode == 'n')) {
		auto icutext = getSelectedText(cursors[0]);
		if (icutext.length() != 0) {
			std::string text;
			icutext.toUTF8String(text);
			SetClipboardText(text);
		}
		applyDeleteToAllCursors(GLFW_KEY_BACKSPACE, false);
		donesomthing = true;
	}else if (key == GLFW_KEY_Z && !is_shift_held && (is_control_held || mode == 'n')) {
		activateUndo();
		donesomthing = true;
	}else if (key == GLFW_KEY_Z && is_shift_held && (is_control_held || mode == 'n')) {
		activateRedo();
		donesomthing = true;
	}else if (key == GLFW_KEY_LEFT_BRACKET && is_control_held) { // this one has to be control because it's a common mistake for typing otherwise
		applyIndentChangeToAllCursors(-1);
		donesomthing = true;
	}else if (key == GLFW_KEY_RIGHT_BRACKET && is_control_held) { // this one has to be control because it's a common mistake for typing otherwise
		applyIndentChangeToAllCursors(1);
		donesomthing = true;
	}
	
	largereditblock = false;
	
	if (donesomthing && ontextchange) {
		ontextchange(this);
		return true;
	}
	
	return false;
}

icu::UnicodeString TextEdit::getSelectedText(Cursor c) {
	auto sel = _getCursSelec(c);
	
	int sl = sel.first.first;
	int el = sel.first.second;
	int sc = sel.second.first;
	int ec = sel.second.second;
	
	if (sl == el) {
		return lines[sl].line_text.tempSubStringBetween(sc, ec);
	}
	
	auto start = lines[sl].line_text.tempSubStringBetween(sc, lines[sl].line_text.length());
	icu::UnicodeString end;
	lines[el].line_text.extractBetween(0, ec, end);
	
	std::vector<icu::UnicodeString> ourlines = {start};
	
	for (int line = sl + 1; line < el; line++) {
		ourlines.push_back(lines[line].line_text);
	}
	
	ourlines.push_back(end);
	
	return joinByString(ourlines, icu::UnicodeString::fromUTF8("\n"));
}

void TextEdit::ensureCursorVisible(Cursor c) {
	int line_start = floor(scrolled_to_vert);
	int char_start = floor(scrolled_to_horz);
	
	int end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height());
	int end_char = char_start+ceil((float)t_w/(float)TextRenderer::get_text_width(1))-1;
	
	if (end_line-line_start < 14) {
		scrolled_to_vert = c.head_line-(end_line-line_start)/2;
		if (scrolled_to_vert < 0) {
			scrolled_to_vert = 0;
		}if (scrolled_to_vert > max_scroll_vert) {
			scrolled_to_vert = max_scroll_vert;
		}
	}else{
		int l1 = c.head_line;
		
		if (l1-4 < line_start) {
			scrolled_to_vert = l1-4;
		}else if (l1+7 > end_line) {
			scrolled_to_vert = l1-(end_line-line_start)+7;
		}
		
		if (scrolled_to_vert < 0) {
			scrolled_to_vert = 0;
		}if (scrolled_to_vert > max_scroll_vert) {
			scrolled_to_vert = max_scroll_vert;
		}
	}
	
	int c1 = _mapFromRealToVisual(c.head_line, c.head_char);
	
	if (c1-4 < char_start) {
		scrolled_to_horz = c1-4;
	}else if (c1+7 > end_char) {
		scrolled_to_horz = c1-(end_char-char_start)+7;
	}
	
	if (scrolled_to_horz < 0) {
		scrolled_to_horz = 0;
	}if (scrolled_to_horz > max_scroll_horz) {
		scrolled_to_horz = max_scroll_horz;
	}
}

void TextEdit::applyInsertToAllCursors(icu::UnicodeString to_insert) {
	to_insert = stripOfChar(to_insert, U'\r');
	
	for (int i = 0; i < cursors.size(); i++) {
		insertTextAtCursor(cursors[i], to_insert);
	}
	
	tryingToEnsureCursorPos = true;
	
	for (int i = 0; i < cursors.size(); i++) {
		cursors[i].preffered_collumn = _mapFromRealToVisual(cursors[i].head_line, cursors[i].head_char);
	}
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

void TextEdit::insertTextAtCursor(Cursor c, icu::UnicodeString insert_text) {
	auto sel = _getCursSelec(c);
	
	if (sel.first.first != sel.first.second || sel.second.first != sel.second.second) {
		deleteTextAtCursor(c, GLFW_KEY_BACKSPACE, false); // delete the text before insertion
	}
	
	int sl = sel.first.first;
	int sc = sel.second.first;
	
	if (getIndentationLevelAfterLine && insert_text == icu::UnicodeString::fromUTF8("\n")) {
		icu::UnicodeString line = lines[sl].line_text.tempSubStringBetween(0, sc);
		icu::UnicodeString nextline = lines[sl].line_text.tempSubString(sc);
		
		int indentation_level = getIndentationLevelAfterLine(line, nextline);
		
		for (int i = 0; i < indentation_level; i++) {
			insert_text += "\t";
		}
	}
	
	auto insert_lines = splitByChar(insert_text, u'\n');
	
	int nl = insert_lines.size()-1;
	int nc = insert_lines[insert_lines.size()-1].length();
	
	// move cursors
	for (int i = 0; i < cursors.size(); i ++) {
		auto curs = cursors[i];
		auto nh = _handleSectionAdded(curs.head_line, curs.head_char, sl, sc, nl, nc);
		auto na = _handleSectionAdded(curs.anchor_line, curs.anchor_char, sl, sc, nl, nc);
		
		curs.head_line = nh.first;
		curs.head_char = nh.second;
		curs.anchor_line = na.first;
		curs.anchor_char = na.second;
		
		cursors[i] = curs;
	}
	
	auto start = lines[sl].line_text.tempSubStringBetween(0, sc);
	icu::UnicodeString end;
	lines[sl].line_text.extractBetween(sc, lines[sl].line_text.length(), end);
	
	std::string endthing;
	end.toUTF8String(endthing);
	
	changed_during_update = true;
	
	if (nl == 0) {
		Edit e = { EditType::ChangeLine, lines[sl], sl };
		historyThisUpdate.edits.push_back(e);
		
		lines[sl].line_text = start+insert_lines[0]+end;
		lines[sl].changed = true;
	}else{
		Edit e = { EditType::ChangeLine, lines[sl], sl };
		historyThisUpdate.edits.push_back(e);
		
		lines[sl].line_text = start+insert_lines[0];
		
		for (int i = 1; i < insert_lines.size(); i ++) {
			auto new_line = Line();
			new_line.line_text = insert_lines[i];
			
			std::string endthing;
			end.toUTF8String(endthing);
			
			if (i == insert_lines.size()-1) {
				new_line.line_text += end;
			}
			new_line.changed = true;
			lines.insert(lines.begin()+sl+i, new_line);
			
			Edit e = { EditType::InsertLine, lines[sl+i], sl+i };
			historyThisUpdate.edits.push_back(e);
		}
		
		lines[sl].changed = true;
	}
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

bool TextEdit::handleDeleteKey(int key, int scancode, int action, int mods) {
	bool is_control_held = ((mods & GLFW_MOD_CONTROL) != 0);
	
	if (key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) {
		largereditblock = true;
		applyDeleteToAllCursors(key, is_control_held);
		if (ontextchange) {
			ontextchange(this);
		}
		return true;
	}
	
	return false;
}

bool TextEdit::handleNavKey(int key, int scancode, int action, int mods) {
	bool is_shift_held = ((mods & GLFW_MOD_SHIFT) != 0);
	bool is_control_held = ((mods & GLFW_MOD_CONTROL) != 0);
	bool is_alt_held = ((mods & GLFW_MOD_ALT) != 0);
	
	if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN || key == GLFW_KEY_HOME || key == GLFW_KEY_END) {
		if (is_alt_held && key == GLFW_KEY_DOWN) {
			insertNewCursorDown();
		}else if (is_alt_held && key == GLFW_KEY_UP) {
			insertNewCursorUp();
		}else{
			applyMoveToAllCursors(key, is_shift_held, is_control_held);
		}
		
		return true;
	}
	
	if (key == GLFW_KEY_A && is_control_held) {
		cursors = {cursors[0]}; // go ahead and get rid of all there rest.
		cursors[0].anchor_char = 0;
		cursors[0].anchor_line = 0;
		cursors[0].head_line = lines.size()-1;
		cursors[0].head_char = lines[cursors[0].head_line].line_text.length();
		cursors[0].preffered_collumn = _mapFromRealToVisual(cursors[0].head_line, cursors[0].head_char);
		return true;
	}
	
	if (key == GLFW_KEY_COMMA && (is_control_held || mode == 'n')) {
		vim_repeater = 0;
		applyMoveToAllCursors(CODEWIZARD_MATCHING_BRACKET_LEFT, is_shift_held, false);
		return true;
	}else if (key == GLFW_KEY_PERIOD && (is_control_held || mode == 'n')) {
		vim_repeater = 0;
		applyMoveToAllCursors(CODEWIZARD_MATCHING_BRACKET_RIGHT, is_shift_held, false);
		return true;
	}
	
	if (mode == 'n') {
		if (key == GLFW_KEY_ESCAPE) {
			cursors = {cursors[0]};
		}else if (key == GLFW_KEY_H && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_LEFT, is_shift_held, is_control_held);
			return true;
		}else if (key == GLFW_KEY_L && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_RIGHT, is_shift_held, is_control_held);
			return true;
		}else if (key == GLFW_KEY_K && !is_control_held) {
			if (is_alt_held) {
				insertNewCursorUp();
			}else{
				applyMoveToAllCursors(GLFW_KEY_UP, is_shift_held, is_control_held);
			}
			return true;
		}else if (key == GLFW_KEY_J && !is_control_held) {
			if (is_alt_held) {
				insertNewCursorDown();
			}else{
				applyMoveToAllCursors(GLFW_KEY_DOWN, is_shift_held, is_control_held);
			}
			return true;
		}else if (key == GLFW_KEY_W && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_LEFT, is_shift_held, true);
			return true;
		}else if (key == GLFW_KEY_E && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_RIGHT, is_shift_held, true);
			return true;
		}else if (key == GLFW_KEY_A && !is_control_held) {
			vim_repeater = 0;
			applyMoveToAllCursors(GLFW_KEY_END, is_shift_held, false);
			return true;
		}else if (key == GLFW_KEY_O && !is_control_held) {
			vim_repeater = 0;
			applyMoveToAllCursors(GLFW_KEY_END, is_shift_held, false);
			applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\n"));
			return true;
		}else if (key == GLFW_KEY_S && !is_control_held) {
			vim_repeater = 0;
			applyMoveToAllCursors(CODEWIZARD_WORD_WRAP, is_shift_held, false);
			return true;
		}else if (key == GLFW_KEY_G && !is_control_held && vim_repeater != 0) {
			if (vim_repeater > lines.size()){
				vim_repeater = lines.size();
			}
			cursors = { { vim_repeater-1, 0, vim_repeater-1, 0, 0 } };
			vim_repeater = 0;
			tryingToEnsureCursorPos = true;
			return true;
		}
	}else{
		if (key == GLFW_KEY_ESCAPE && !App::settings->getValue("use_vim", false)) {
			cursors = {cursors[0]};
		}
	}
	
	return false;
}

bool TextEdit::handleUserKey(int key, int scancode, int action, int mods) {
	if (mode == 'n') {
		if (key == GLFW_KEY_I) {
			mode = 'i';
			vim_repeater = 0;
			HandleOverlappingCursors();
			return true;
		}
		
		for (int indx = 0; indx < DIGITS_KEYS.size(); indx ++) {
			int digit = DIGITS_KEYS[indx];
			
			if (key == digit) {
				vim_repeater *= 10;
				vim_repeater += indx;
				
				HandleOverlappingCursors();
				return true;
			}
		}
	}
	
	if (mode == 'i') {
		if (key == GLFW_KEY_ESCAPE && App::settings->getValue("use_vim", false)) {
			mode = 'n';
			HandleOverlappingCursors();
			return true;
		}
	}
	
	if (handleNavKey(key, scancode, action, mods)) {
		HandleOverlappingCursors();
		return true;
	}
	
	if (handleDeleteKey(key, scancode, action, mods)) {
		HandleOverlappingCursors();
		return true;
	}
	
	if (handleInsertKey(key, scancode, action, mods)) {
		HandleOverlappingCursors();
		return true;
	}
	
	HandleOverlappingCursors();
	return true;
}

bool TextEdit::on_char_event(unsigned int codepoint) {
	if (App::activeLeafNode != this) {
		return false;
	}
	
	UChar32 ch = static_cast<UChar32>(codepoint);
	
	char utf8[5] = {};
	int len = std::snprintf(utf8, sizeof(utf8), "%c", codepoint);
	if (len > 0) { // there is something printable
		// this doesn't detect newlines, tabs, 
		// we're going to handle all whitespace in the key down
		if (ch == U'\t' || ch == U' ' || ch == '\n') {
			return true;
		}
		
		if (wasmode == 'n') {
			return true;
		}
		
		icu::UnicodeString to_insert;
		to_insert.append(ch);
		
		largereditblock = true;
		applyInsertToAllCursors(to_insert);
		largereditblock = false;
		if (ontextchange) {
			ontextchange(this);
		}
	}
	
	return true;
}

bool TextEdit::on_key_event(int key, int scancode, int action, int mods) {
	if (App::activeLeafNode == this) {
		if (action == GLFW_RELEASE) {
			return true;
		}
		
		return handleUserKey(key, scancode, action, mods);
	}
	
	return false;
}

void TextEdit::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, background_color);
	
	int curx = start_x+t_x+App::text_padding;
	int cury = start_y+t_y+App::text_padding;
	
	for (auto cs : draw_selection) {
		int y = TextRenderer::get_text_height()*cs.rel_line+start_y+t_y+App::text_padding;
		
		int sx = TextRenderer::get_text_width(cs.rel_char_start)+start_x+t_x+App::text_padding;
		int ex = TextRenderer::get_text_width(cs.rel_char_end)+start_x+t_x+App::text_padding;
		
		int w = ex-sx;
		
		App::DrawRect(sx, y, w, TextRenderer::get_text_height(), App::theme.hover_background_color);
	}
	
	for (auto cs : draw_diagnostics) {
		int y = TextRenderer::get_text_height()*cs.rel_line+start_y+t_y+App::text_padding;
		
		int sx = TextRenderer::get_text_width(cs.rel_char_start)+start_x+t_x+App::text_padding;
		int ex = TextRenderer::get_text_width(cs.rel_char_end)+start_x+t_x+App::text_padding;
		
		int w = ex-sx;
		
		Color* c;
		if (cs.type == 0) {
			c = App::theme.error_color;
		}else if (cs.type == 1) {
			c = App::theme.warning_color;
		}else {
			c = App::theme.suggestion_color;
		}
		
		App::DrawRect(sx, y+TextRenderer::get_text_height(), w, 2, c);
	}
	
	for (int ln_ren = 0; ln_ren < draw_text.size(); ln_ren++) {;
		TextRenderer::draw_text(curx, cury, draw_text[ln_ren], draw_color[ln_ren]);
		cury += TextRenderer::get_text_height();
	}
	
	for (auto cs : draw_cursor) {
		int x = TextRenderer::get_text_width(cs.rel_char)+start_x+t_x+App::text_padding;
		int y = TextRenderer::get_text_height()*cs.rel_line+start_y+t_y+App::text_padding;
		int w = 2;
		
		if (mode == 'n') {
			w = TextRenderer::get_text_width(1);
		}
		
		App::DrawRect(x, y, w, TextRenderer::get_text_height(), cs.color);
		
		if (mode == 'n' && cs.rel_char < draw_text[cs.rel_line].length()) {
			TextRenderer::draw_text(x, y, draw_text[cs.rel_line].char32At(cs.rel_char), {App::theme.darker_background_color});
		}
	}
	
	Widget::render();
}

Color* TextEdit::getColorFromTokens(int indx, std::vector<ColoredTokens> tokens, bool* aragne) {
	for (int i = tokens.size()-1; i >= 0; i--) {
		auto t = tokens[i];
		if (t.start <= indx && indx < t.end) {
			if (t.color < 0) {
				// it's a difference token
				if (t.color == -1) {
					return App::theme.add_diff;
				}else if (t.color == -2) {
					return App::theme.del_diff;
				}else {
					return App::theme.equal_diff;
				}
				
			}else{
				if (t.color == 1 || t.color == 2) {
					*aragne = true;
				}
				
				return App::theme.syntax_colors[t.color];
			}
		}
	}
	return App::theme.main_text_color;
}

void TextEdit::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	POS_FUNC(this);
	
	Widget::position(x, y, w, h);
	
	// let's make sure vim mode is allowed...
	
	if (!App::settings->getValue("use_vim", false)) {
		mode = 'i';
	}
	
	// this used to run before position, but we had issues.
	
	max_scroll_vert = lines.size()-1;
	max_scroll_horz = max_line_len-1;
	
	if (tryingToEnsureCursorPos) {
		tryingToEnsureCursorPos = false;
		ensureCursorVisible(cursors[0]);
	}
	
	// all widgets positioned, time to determine visible text
	
	draw_text.clear();
	draw_color.clear();
	draw_cursor.clear();
	draw_selection.clear();
	draw_diagnostics.clear();
	
	int line_start = floor(scrolled_to_vert);
	int char_start = floor(scrolled_to_horz);
	
	start_x = -fmod(scrolled_to_horz, 1) * TextRenderer::get_text_width(1);
	start_y = -fmod(scrolled_to_vert, 1) * TextRenderer::get_text_height();
	
	if (char_start >= 1) { // huh? Why in the fuck.... I mean it works? (also yes, I did write this... (the code I mean))
		char_start -= 1;
		start_x -= TextRenderer::get_text_width(1);
	}
	
	int end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	int end_char = char_start+ceil((float)t_w/(float)TextRenderer::get_text_width(1)) + 1;
	
	int start_highlight = fmax(0, line_start-5);
	int end_highlight = fmin(lines.size(), end_line+5);
	Highlight(start_highlight, end_highlight);
	
	for (int ln_num = line_start; ln_num < end_line; ln_num ++) {
		if (ln_num < 0 || ln_num >= lines.size()) {
			continue;
		}
		
		icu::UnicodeString thisLn = lines[ln_num].line_text;
		
		int cur_char = 0;
		
		icu::UnicodeString final_line = icu::UnicodeString();
		std::vector<Color*> final_color = {};
		
		// diagnostics
		
		std::vector<std::vector<int>> draw_diag = {};
		auto diagnostics = lines[ln_num].diagnostics;
		
		int thisdiagtype = 10;
		icu::UnicodeString thisdiag;
		
		for (auto d : diagnostics) {
			draw_diag.push_back({-1, -1});
			
			if (d.type < thisdiagtype) {
				thisdiagtype = d.type;
				thisdiag = d.message;
			}
		}
		
		// selection
		
		std::vector<std::vector<int>> selections = {};
		std::vector<std::vector<int>> draw_selec = {};
		
		for (auto c : cursors) {
			auto slelscec = _getCursSelec(c);
			
			int start_line = slelscec.first.first;
			int end_line = slelscec.first.second;
			
			int start_char = slelscec.second.first;
			int end_char = slelscec.second.second;
			
			if (start_char == end_char && start_line == end_line) {
				continue;
			}
			
			if (start_line > ln_num || end_line < ln_num) {
				continue;
			}
			
			int start_sec = 0;
			if (start_line == ln_num) {
				start_sec = start_char;
			}
			int end_sec = thisLn.length();
			if (end_line == ln_num) {
				end_sec = end_char;
			}
			
			
			int first_selec = -1;
			int last_selec = -1;
			
			bool add_one_to_last_char = false;
			
			if (start_sec < first_selec || first_selec == -1) {
				first_selec = start_sec;
			}
			if (end_sec > last_selec) {
				last_selec = end_sec;
			}
			
			if (end_line > ln_num) {
				add_one_to_last_char = true;
			}
			
			selections.push_back({first_selec, last_selec, add_one_to_last_char});
			draw_selec.push_back({-1, -1});
		}
		
		// run through
		
		for (int true_char_indx = 0; true_char_indx < thisLn.length()+1; true_char_indx ++) {
			for (int i = 0; i < selections.size(); i++) {
				auto s = selections[i];
				if (s[0] <= true_char_indx && draw_selec[i][0] == -1) {
					draw_selec[i][0] = final_line.length();
				}
				if (s[0] <= true_char_indx && s[1] >= true_char_indx) {
					draw_selec[i][1] = final_line.length();
				}
			}
			
			for (int i = 0; i < diagnostics.size(); i++) {
				auto d = diagnostics[i];
				if (d.sc <= true_char_indx && draw_diag[i][0] == -1) {
					draw_diag[i][0] = final_line.length();
				}
				if (d.sc <= true_char_indx && d.ec >= true_char_indx) {
					draw_diag[i][1] = final_line.length();
				}
				
			}
			
			for (auto c : cursors) {
				if (App::activeLeafNode == this && c.head_line == ln_num && c.head_char == true_char_indx && cur_char >= char_start) {
					CursorScreen dc = CursorScreen();
					dc.rel_line = draw_text.size();
					dc.rel_char = final_line.length();
					draw_cursor.push_back(dc);
				}
			}
			
			if (true_char_indx >= thisLn.length()) {
				continue;
			}
			
			bool finished_line = false;
			
			auto chr = thisLn.char32At(true_char_indx);
			
			std::vector<UChar32> tohandle = {};
			
			if (chr == U'\t') {
				tohandle = {U' ', U' ', U' ', U' '};
			}else{
				tohandle = {chr};
			}
			
			for (auto c : tohandle) {
				if (cur_char >= char_start) {
					final_line.append(c);
					
					bool arange = false;
					Color* col = getColorFromTokens(true_char_indx, lines[ln_num].tokens, &arange);
					
					if (!arange && punctuationset.count(chr)) {
						col = App::theme.syntax_colors[7];
					}
					
					final_color.push_back(col);
				}
				
				cur_char ++;
				
				if (cur_char > end_char) {
					finished_line = true;
					break;
				}
			}
			
			if (finished_line) {
				break;
			}
		}
		
		for (int i = 0; i < selections.size(); i++) {
			if (selections[i][2] == 1) {
				draw_selec[i][1] ++;
			}
			
			auto selec = CursorSelect();
			
			selec.rel_line = draw_text.size();
			selec.rel_char_start = draw_selec[i][0];
			selec.rel_char_end = draw_selec[i][1];
			draw_selection.push_back(selec);
		}
		
		for (int i = 0; i < diagnostics.size(); i++) {
			auto diag = DiagnosticUnderline();
			
			diag.rel_line = draw_text.size();
			diag.rel_char_start = draw_diag[i][0];
			diag.rel_char_end = draw_diag[i][1];
			diag.type = diagnostics[i].type;
			
			draw_diagnostics.push_back(diag);
		}
		
		if (thisdiag != "") {
			final_line += icu::UnicodeString::fromUTF8("  ■ ") + splitByChar(thisdiag, U'\n')[0];
			for (int i = final_color.size(); i < final_line.length(); i++) {
				if (thisdiagtype == 0) {
					final_color.push_back(App::theme.error_color);
				}else if (thisdiagtype == 1) {
					final_color.push_back(App::theme.warning_color);
				}else {
					final_color.push_back(App::theme.suggestion_color);
				}
			}
		}
		
		draw_text.push_back(final_line);
		draw_color.push_back(final_color);
	}
	
	wasmode = mode;
	
	changed_during_update = false;
	updateUndoHistory();
}

bool TextEdit::on_scroll_event(double xchange, double ychange) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (glfwGetKey(App::window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
		xchange = ychange;
		ychange = 0;
	}
	
	if (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h) {
		return false;
	}
	
	int initial_scroll_vert = scrolled_to_vert;
	int initial_scroll_horz = scrolled_to_horz;
	
	scrolled_to_horz += xchange*6;
	scrolled_to_vert += ychange*6;
	
	if (scrolled_to_vert > max_scroll_vert) {
		scrolled_to_vert = max_scroll_vert;
	}else if (scrolled_to_vert < 0.0) {
		scrolled_to_vert = 0.0;
	}
	
	if (scrolled_to_horz > max_scroll_horz) {
		scrolled_to_horz = max_scroll_horz;
	}else if (scrolled_to_horz < 0.0) {
		scrolled_to_horz = 0.0;
	}
	
	if (initial_scroll_horz == scrolled_to_horz && initial_scroll_vert == scrolled_to_vert) {
		return false; // no change
	}
	
	return true;
}

Cursor TextEdit::getCursorForMousePosition(int mx, int my, bool* gottoit) {
	int first_line = floor(scrolled_to_vert);
	int first_char = floor(scrolled_to_horz);
	
	if (first_char >= 1) { // this is for some reason correct... (I'm not planning on questioning it.)
		first_char -= 1;
	}
	
	int mouse_line = ((my-start_y-t_y) / TextRenderer::get_text_height())+first_line;
	
	if (mouse_line < 0) {
		mouse_line = 0;
	}else if (mouse_line >= lines.size()) {
		mouse_line = lines.size()-1;
	}
	
	int mouse_visual_char = ((mx-start_x-t_x) / TextRenderer::get_text_width(1)) + first_char;
	
	int mouse_char = _mapFromVisualToReal(mouse_line, mouse_visual_char);
	
	if (gottoit) {
		int checkby = _mapFromRealToVisual(mouse_line, mouse_char);
		
		*gottoit = (mouse_visual_char == checkby);
	}
	
	Cursor c;
	c.head_line = mouse_line;
	c.anchor_line = mouse_line;
	c.head_char = mouse_char;
	c.anchor_char = mouse_char;
	c.preffered_collumn = mouse_visual_char;
	return c;
}

bool TextEdit::on_mouse_button_event(int button, int action, int mods) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (is_selecting_text_with_mouse && action == GLFW_RELEASE) {
		is_selecting_text_with_mouse = false; // this is always true even if the mouse isn't over this element. (or if it's not active.)
	}
	
	if (is_visible && (mx < t_x || mx > t_x+t_w || my < t_y || my > t_y+t_h)) {
		return false;
	}
	
	if (action == GLFW_PRESS && App::activeLeafNode != this) {
		App::setActiveLeafNode(this);
	}
	
	if (App::activeLeafNode != this) {
		return false;
	}
	
	if (action == GLFW_PRESS) {
		Cursor crsr = getCursorForMousePosition(mx, my);
		
		if (glfwGetKey(App::window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
			cursors.push_back(crsr);
			return true;
		}
		
		bool keepanchor = (glfwGetKey(App::window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
		
		is_selecting_text_with_mouse = true;
		
		if (keepanchor) {
			Cursor cur = cursors[0];
			crsr.anchor_char = cur.anchor_char;
			crsr.anchor_line = cur.anchor_line;
		}
		
		cursors = { crsr };
	}
	return true;
	
	return Widget::on_mouse_button_event(button, action, mods);
}

bool TextEdit::on_mouse_move_event() {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (is_selecting_text_with_mouse) {
		Cursor crsr = getCursorForMousePosition(mx, my);
		Cursor cur = cursors[0];
		
		cur.head_char = crsr.head_char; // because this is drag, we don't need to move the anchor
		cur.head_line = crsr.head_line;
		cur.preffered_collumn = crsr.preffered_collumn;
		
		cursors = { cur };
		
		tryingToEnsureCursorPos = true;
	}
	
	return Widget::on_mouse_move_event();
}

icu::UnicodeString TextEdit::getCurrentWord(const icu::UnicodeString& blockText, int blockPos) {
	icu::UnicodeString word;

	int start = blockPos - 1;
	while (start >= 0) {
		UChar32 ch = blockText.char32At(start);
		if (u_isalnum(ch) || ch == U'_') {
			// If current character is part of the word, move left
			// Handle surrogate pairs (if any) by decrementing by character length
			start -= U16_LENGTH(ch);
		} else {
			break;
		}
	}

	// Compute actual start index after exiting loop
	int wordStart = start + 1;
	int wordLength = blockPos - wordStart;

	// Extract substring if length is valid
	if (wordLength > 0)
		word = blockText.tempSubStringBetween(wordStart, blockPos);

	return word;
}