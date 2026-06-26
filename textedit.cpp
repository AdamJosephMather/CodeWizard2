#include "textedit.h"
#include "text_renderer.h"
#include "application.h"
#include <set>
#include <chrono>
#include <cctype>
#include "helper_types.h"
#include <unicode/brkiter.h>
#include <unicode/uchar.h>
#include "syntect_bridge.h"

//#define DEBUG

#define CODEWIZARD_WORD_WRAP 10000
#define CODEWIZARD_MATCHING_BRACKET_LEFT 10001
#define CODEWIZARD_MATCHING_BRACKET_RIGHT 10002

std::set<UChar32> whitespace = {0x20, 0x09, 0x0A, 0x0D, 0x00A0, 0x2028, 0x2029};
std::set<UChar32> numeric = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
std::set<UChar32> allowed_in_var_names = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5F};
static std::set<UChar32> punctuationset = {U'!', U'#', U'$', U'%', U'&', U'(', U')', U'*', U'+', U',', U'-', U'.', U'/', U':', U';', U'<', U'=', U'>', U'?', U'@', U'[', U'\\', U']', U'^', U'`', U'{', U'|', U'}', U'~'};

icu::UnicodeString openers = icu::UnicodeString("({[<");
icu::UnicodeString closers = icu::UnicodeString(")}]>");

std::vector<int> DIGITS_KEYS = {GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9};

TextEdit::TextEdit(Widget* parent, App::PosFunction fnct) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("TextEdit");
	
	POS_FUNC = fnct;
	
	borderColor = App::theme.border;
	activeBorderColor = App::theme.active_color;
	
	Line ln = Line();
	ln.line_text = icu::UnicodeString::fromUTF8("");
	ln.tokens = {};
	changed_during_update = true;
	
	lines.clear();
	lines.push_back(std::move(ln)); // one empty line.
	cursors = {Cursor()}; // we have to set these two before we init the undo history.
	coppiedText = {};
	historyThisUpdate = createHistory();
	
	draw_cursor = {};
	draw_diagnostics = {};
	draw_mark = {false};
	
	scrollbar_v = new Scrollbar(this);
	scrollbar_v->getScrollInfo = [&](){
		std::vector<double> out = {0.0, 0.0};
		double screenlines = (double)t_h/(double)TextRenderer::get_text_height();
		double end_line = scrolled_to_vert+screenlines;
		
		out[0] = scrolled_to_vert/((double)lines.size()+screenlines-1);
		out[1] = end_line/((double)lines.size()+screenlines-1);
		
		return out;
	};
	scrollbar_v->scrollTo = [&](double newval){
		double screenlines = (double)t_h/(double)TextRenderer::get_text_height();
		scrolled_to_vert = newval*((double)lines.size()+screenlines-1);
		
		if (scrolled_to_vert > max_scroll_vert) {
			scrolled_to_vert = max_scroll_vert;
		}else if (scrolled_to_vert < 0.0) {
			scrolled_to_vert = 0.0;
		}
		
		scroll_vertical_change = 0;
		DO_POSITION = true;
	};
	
	scrollbar_h = new Scrollbar(this);
	scrollbar_h->getScrollInfo = [&](){
		std::vector<double> out = {0.0, 0.0};
		double screenchars = (double)t_w/(double)TextRenderer::get_text_width(1);
		double end_char = scrolled_to_horz+screenchars;
		
		out[0] = scrolled_to_horz / ((double)max_scroll_horz+screenchars-1);
		out[1] = end_char/((double)max_scroll_horz+screenchars-1);
		
		return out;
	};
	scrollbar_h->scrollTo = [&](double newval){
		double screenchars = (double)t_w/(double)TextRenderer::get_text_width(1);
		scrolled_to_horz = newval*((double)max_scroll_horz+screenchars-1);
		
		if (scrolled_to_horz > max_scroll_horz) {
			scrolled_to_horz = max_scroll_horz;
		}else if (scrolled_to_horz < 0.0) {
			scrolled_to_horz = 0.0;
		}
		
		scroll_horizontal_change = 0;
		DO_POSITION = true;
	};
	scrollbar_h->horizontal = true;
	
	contextmenu = new ContextMenu(this);
	contextmenu->is_visible_2 = false;
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Undo\t(Ctrl+Z)"), [&](Widget* w){
		activateUndo();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Redo\t(Ctrl+Shift+Z)"), [&](Widget* w){
		activateRedo();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addSeparaterToMenu();
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Cut\t(Ctrl+X)"),   [&](Widget* w){
		cut();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Copy\t(Ctrl+C)"),  [&](Widget* w){
		copy();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Paste\t(Ctrl+V)"), [&](Widget* w){
		paste();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addSeparaterToMenu();
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Toggle Mark\t(Ctrl+M)"), [&](Widget* w){
		toggleMark();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Next Mark\t(Alt+Right)"), [&](Widget* w){
		gotoNextMark();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Prev Mark\t(Alt+Left)"), [&](Widget* w){
		gotoPrevMark();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Clear Marks"), [&](Widget* w){
		clearMarks();
		contextmenu->is_visible_2 = false;
	});
	
	contextmenu->recalcButtonTexts();
}

void TextEdit::toggleMark() {
	for (auto c : cursors) {
		for (int l = fmin(c.head_line, c.anchor_line); l <= fmax(c.head_line, c.anchor_line); l++) {
			lines[l].isMarked = !lines[l].isMarked;
		}
	}
	DO_POSITION = true;
}

void TextEdit::clearMarks() {
	for (int l = 0; l < lines.size(); l++) {
		lines[l].isMarked = false;
	}
	DO_POSITION = true;
}

void TextEdit::gotoNextMark() {
	for (int ci = 0; ci < cursors.size(); ci++) {
		auto c = cursors[ci];
		bool jumpto = !lines[c.head_line].isMarked; // don't jump till we've exited the current marking
		bool done = false;
		
		for (int l = c.head_line+1; l < lines.size(); l++) {
			if (!jumpto) {
				if (!lines[l].isMarked) { // now we can jump - we've found a non marked line
					jumpto = true;
				}
			}else if (lines[l].isMarked) {
				cursors[ci] = {l,0,l,0,0}; // line, char, line, char, preffered col
				done = true;
				break;
			}
		}
		
		if (done) {
			continue;
		}
		
		for (int l = 0; l < c.head_line; l++) {
			if (lines[l].isMarked) { // don't care about jumpto, we wrapped
				cursors[ci] = {l,0,l,0,0}; // line, char, line, char, preffered col
				done = true;
				break;
			}
		}
	}
	
	tryingToEnsureCursorPos = true;
	DO_POSITION = true;
}

void TextEdit::gotoPrevMark() {
	for (int ci = 0; ci < cursors.size(); ci++) {
		auto c = cursors[ci];
		bool jumpto = !lines[c.head_line].isMarked; // don't jump till we've exited the current marking
		bool done = false;
		
		for (int l = c.head_line-1; l >= 0; l--) {
			if (!jumpto) {
				if (!lines[l].isMarked) { // now we can jump - we've found a non marked line
					jumpto = true;
				}
			}else if (lines[l].isMarked) {
				cursors[ci] = {l,0,l,0,0}; // line, char, line, char, preffered col
				done = true;
				break;
			}
		}
		
		if (done) {
			continue;
		}
		
		for (int l = lines.size()-1; l > c.head_line; l--) {
			if (lines[l].isMarked) { // don't care about jumpto, we wrapped
				cursors[ci] = {l,0,l,0,0}; // line, char, line, char, preffered col
				done = true;
				break;
			}
		}
	}
	
	tryingToEnsureCursorPos = true;
	DO_POSITION = true;
}

int TextEdit::getVisLen(const icu::UnicodeString& line) {
	int ln = 0;
	
	for (int c_indx = 0; c_indx < line.length(); c_indx++) {
		UChar32 c = line.char32At(c_indx);

		if (c == U'\t') {
			ln += tabWidth;
		}else{
			ln ++;
		}
	}
	return ln;
}

void TextEdit::setFullText(icu::UnicodeString text) {
	auto lns = splitByChar(text, U'\n');

	lines.clear();

	for (auto l : lns) {
		Line line;
		line.line_text = l;
		line.tokens = {};
		line.changed = true;
		lines.push_back(std::move(line));
	}

	undo_stack.clear();
	redo_stack.clear();
	historyThisUpdate = createHistory();
	
	cursors = {Cursor()};
	
	scrolled_to_horz = 0;
	scrolled_to_vert = 0;
	scroll_vertical_change = 0;
	scroll_horizontal_change = 0;
	
	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
	
	DO_POSITION = true;
	contextmenu->is_visible_2 = false;
}

History TextEdit::createHistory() {
	History hs;
	hs.millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	hs.cursors_before = cursors;
	hs.cursors_after = {};
	hs.edits.clear();
	return hs;
}

void TextEdit::Highlight(int first_visible_line, int last_visible_line) {
	max_line_len = 0;
	
	for (int i = first_visible_line; i < last_visible_line && i < lines.size(); i++) {
		Line* line = &lines[i];
		
		if (line->changed) {
			line->visual_length = getVisLen(line->line_text);
		}
		
		if (line->visual_length > max_line_len) {
			max_line_len = line->visual_length;
		}
	}
	
	if (!highlighter || !highlighter_initial_state) {
		return;
	}

	int highlighted = 0;

	for (int i = 0; i < lines.size(); i++) {
		Line* line = &lines[i];

		if (line->changed) {
			line->changed = false;
			line->highlightinguptodate = false;

			int ln = 0;

			for (int c_indx = 0; c_indx < line->line_text.length(); c_indx++) {
				UChar32 c = line->line_text.char32At(c_indx);
				
				if (c == U'\t') {
					ln += tabWidth;
				} else {
					ln++;
				}
			}
			
			line->visual_length = ln;
		}

		CW_SyntaxState* prev_state = nullptr;
		uint64_t prev_hash = 0;

		if (i == 0) {
			prev_state = highlighter_initial_state.get();
			prev_hash = cw_syntect_state_hash(highlighter_initial_state.get());
		} else {
			Line* prev_line = &lines[i - 1];

			if (!prev_line->after_line_state) {
				// Previous line has not been highlighted yet.
				// We cannot correctly highlight this line until the previous state exists.
				line->highlightinguptodate = false;
				continue;
			}
			
			prev_state = lines[i - 1].after_line_state.get();
			prev_hash = prev_line->after_line_hash;
		}

		bool input_state_changed = line->prev_hash != prev_hash;
		bool needs_highlight =
			!line->highlightinguptodate ||
			input_state_changed ||
			!line->after_line_state;

		if (!needs_highlight) {
			continue;
		}
		
		DO_POSITION = true;

		std::string utf8_line = to_ascii_replacing_non_ascii(line->line_text);

		CW_LineResult result = cw_syntect_highlight_line(
			highlighter,
			prev_state,
			utf8_line.c_str()
		);

		if (result.status != 0) {
			cw_syntect_destroy_tokens(result.tokens, result.token_count);
			line->highlightinguptodate = false;
			continue;
		}

		if (i >= first_visible_line && i <= last_visible_line) {
			App::rerender = true;
		}else {
			App::forceWaitTime = false; // we have like twenty ways to do the same goddamn thing
		}

		// Replace old after-line state.
		if (line->after_line_state) {
			line->after_line_state = nullptr;
		}
		
		line->after_line_state.reset(result.next_state);
		line->after_line_hash = result.state_hash;
		line->prev_hash = prev_hash;
		
		
		line->tokens.assign(result.tokens, result.tokens + result.token_count);
		cw_syntect_destroy_tokens(result.tokens, result.token_count);
		
		line->highlightinguptodate = true;
		
//		for (int i = 0; i < line->token_count; i++) {
//			std::cout << "Token: " << line->tokens[i].start_byte << " - " << line->tokens[i].end_byte << " : " << line->tokens[i].role << "\n";
//		}

		highlighted++;

		if (i < first_visible_line && highlighted > 40) {
			break;
		} else if (i >= first_visible_line && highlighted > 10) {
			break;
		}
	}
}

void TextEdit::updateUndoHistory() {
	historyThisUpdate.cursors_after = cursors;
	
	if (historyThisUpdate.edits.size() == 0) {
		historyThisUpdate = createHistory();
		return;
	}
	
	// Any real document edit invalidates redo.
	// This must happen before every push/merge return path.
	redo_stack.clear();
	
	if (undo_stack.empty()) {
		undo_stack.push_back(std::move(historyThisUpdate));
		historyThisUpdate = createHistory();
		return;
	}
	
	auto last_millis = undo_stack.back().millis;
	
	if (historyThisUpdate.millis-last_millis > 300) { // save every ___ millis
		undo_stack.push_back(std::move(historyThisUpdate));
	}else{
		for (auto& i : historyThisUpdate.edits) {
			undo_stack.back().edits.push_back(std::move(i));
		}
		undo_stack.back().millis = historyThisUpdate.millis;
		undo_stack.back().cursors_after = historyThisUpdate.cursors_after;
	}
	
	historyThisUpdate = createHistory();
}

void TextEdit::activateUndo() {
	updateUndoHistory();
	
	if (undo_stack.empty()) {
		std::cout << "No undo stack\n";
		return;
	}

	auto hs = std::move(undo_stack.back());

	History redoHistory;
	redoHistory.millis = historyThisUpdate.millis;
	redoHistory.cursors_after = hs.cursors_after;
	redoHistory.cursors_before = hs.cursors_before;

	undo_stack.pop_back();

	for (int i = hs.edits.size()-1; i >= 0; i--) {
		auto& e = hs.edits[i];

		if (e.type == EditType::ChangeLine) {
			redoHistory.edits.push_back( { EditType::ChangeLine, std::move(lines[e.index]), e.index } );

			lines[e.index] = std::move(e.line);
			
			if (onlinechange) { onlinechange(EditType::ChangeLine, e.index); }
		} else if (e.type == EditType::DeleteLine) {
			redoHistory.edits.push_back( { EditType::InsertLine, e.line.clone(), e.index } ); // clone, not move
			
			lines.insert(lines.begin()+e.index, std::move(e.line));                           // move into vector
			
			if (onlinechange) { onlinechange(EditType::InsertLine, e.index); }
		}else if (e.type == EditType::InsertLine) { // let's go ahead and delete that sucker
			redoHistory.edits.push_back( { EditType::DeleteLine, std::move(lines[e.index]), e.index } );

			lines.erase(lines.begin()+e.index);
			
			if (onlinechange) { onlinechange(EditType::DeleteLine, e.index); }
		}
	}

	redo_stack.push_back(std::move(redoHistory));

	cursors = hs.cursors_before;

	tryingToEnsureCursorPos = true;
	DO_POSITION = true;
	
	historyThisUpdate = createHistory();

	if (!largereditblock && ontextchange) {
		ontextchange(this);
	}
}

void TextEdit::activateRedo() {
	updateUndoHistory();
	
	if (redo_stack.empty()) {
		std::cout << "No redo stack\n";
		return;
	}

	auto hs = std::move(redo_stack.back());

	History undoHistory;
	undoHistory.millis = historyThisUpdate.millis;
	undoHistory.cursors_before  = hs.cursors_before;
	undoHistory.cursors_after = hs.cursors_after;

	redo_stack.pop_back();

	for (int i = hs.edits.size()-1; i >= 0; i--) {
		auto& e = hs.edits[i];

		if (e.type == EditType::ChangeLine) {
			undoHistory.edits.push_back( { EditType::ChangeLine, std::move(lines[e.index]), e.index } );
			
			lines[e.index] = std::move(e.line);
			
			if (onlinechange) { onlinechange(EditType::ChangeLine, e.index); }
		} else if (e.type == EditType::DeleteLine) {
			undoHistory.edits.push_back( { EditType::InsertLine, e.line.clone(), e.index } ); // clone, not move
			lines.insert(lines.begin()+e.index, std::move(e.line));
			if (onlinechange) { onlinechange(EditType::InsertLine, e.index); }
		}else if (e.type == EditType::InsertLine) { // let's go ahead and delete that sucker
			undoHistory.edits.push_back( { EditType::DeleteLine, std::move(lines[e.index]), e.index } );

			lines.erase(lines.begin()+e.index);
			
			if (onlinechange) { onlinechange(EditType::DeleteLine, e.index); }
		}
	}

	undo_stack.push_back(std::move(undoHistory));

	cursors = hs.cursors_after;

	tryingToEnsureCursorPos = true;
	DO_POSITION = true;
	
	historyThisUpdate = createHistory();

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
				int len = std::max(get_emoji_sequence_length_backward(lines[c.head_line].line_text, c.head_char), 1);
				new_x = c.head_char - len;
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
				int len = std::max(get_emoji_sequence_length(lines[c.head_line].line_text, c.head_char), 1);
				new_x = c.head_char + len;
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
			end_right_l = lines.size()-1;
			end_right_c = lines[end_right_l].line_text.length();
		}else if (end_right_c > lines[end_right_l].line_text.length()) {
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
			c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
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
			c.preffered_collumn = _mapFromRealToVisual(c.head_line, c.head_char);
		}
	}

	if (!shift && !special_exceptions) {
		c.anchor_char = c.head_char;
		c.anchor_line = c.head_line;
	}

	return c;
}

int TextEdit::_mapFromRealToVisual(int line, int c) {
	const icu::UnicodeString& str = lines[line].line_text;
	const int32_t len = str.length();

	if (c <= 0) {
		return 0;
	}

	if (c > len) {
		c = len;
	}

	int visual_loc = 0;

	for (int32_t i = 0; i < len; ) {
		const int32_t real_start = i;

		int32_t emoji_len = get_emoji_sequence_length(str, i);

		int32_t real_advance;
		int visual_advance;

		if (emoji_len > 0) {
			real_advance = emoji_len;
			visual_advance = 2;
		} else {
			UChar32 chr = str.char32At(i);
			real_advance = U16_LENGTH(chr);

			if (chr == U'\t') {
				visual_advance = tabWidth;
			} else {
				visual_advance = 1;
			}
		}

		const int32_t real_end = real_start + real_advance;

		// If c lands anywhere inside this visual unit, map it to the visual
		// position after that unit. This keeps emoji/surrogate pairs indivisible.
		if (c <= real_end) {
			return visual_loc + visual_advance;
		}

		visual_loc += visual_advance;
		i = real_end;
	}

	return visual_loc;
}

int TextEdit::_mapFromVisualToReal(int line, int c) {
	const icu::UnicodeString& str = lines[line].line_text;
	const int32_t len = str.length();

	if (c <= 0 || len == 0) {
		return 0;
	}

	int visual_loc = 0;

	for (int32_t i = 0; i < len; ) {
		const int32_t real_start = i;

		int32_t emoji_len = get_emoji_sequence_length(str, i);

		int32_t real_advance;
		int visual_advance;

		if (emoji_len > 0) {
			real_advance = emoji_len;
			visual_advance = 2;
		} else {
			UChar32 chr = str.char32At(i);
			real_advance = U16_LENGTH(chr);

			if (chr == U'\t') {
				visual_advance = tabWidth;
			} else {
				visual_advance = 1;
			}
		}

		const int visual_end = visual_loc + visual_advance;
		const int32_t real_end = real_start + real_advance;

		if (visual_end == c) {
			return real_end;
		}

		if (visual_end > c) {
			const int d1 = c - visual_loc;
			const int d2 = visual_end - c;

			// Snap to whichever side is visually closer.
			// For emoji width 2:
			//   clicking/locating at first half => before emoji
			//   second half or exact middle => after emoji
			if (d1 < d2) {
				return real_start;
			}

			return real_end;
		}

		visual_loc = visual_end;
		i = real_end;
	}

	return len;
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
	DO_POSITION = true;
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
	DO_POSITION = true;
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
	DO_POSITION = true;

	Edit e = { EditType::ChangeLine, std::move(lines[sl]), sl };
	historyThisUpdate.edits.push_back(std::move(e));
	
	lines[sl].line_text = start+end;
	lines[sl].changed = true;
	
	if (onlinechange) { onlinechange(EditType::ChangeLine, sl); }

	if (sl != el) {
		for (int j = el; j >= sl+1; j--) {
			Edit e = { EditType::DeleteLine, std::move(lines[j]), j };
			historyThisUpdate.edits.push_back(std::move(e));
			if (onlinechange) { onlinechange(EditType::DeleteLine, j); }
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
//	bool is_alt_held = ((mods & GLFW_MOD_ALT) != 0);

	largereditblock = true;
	bool donesomthing = false;

	if (key == GLFW_KEY_TAB) {
		if (cursors.size() == 1) {
			if (is_shift_held) {
				applyIndentChangeToAllCursors(-1);
			}else if (getSelectedText(cursors[0]).length() == 0) {
				applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\t"));
			}else{
				applyIndentChangeToAllCursors(1);
			}
		}else{
			applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\t"));
		}
		
		donesomthing = true;
	}else if (key == GLFW_KEY_SPACE) {
		applyInsertToAllCursors(icu::UnicodeString::fromUTF8(" "));
		donesomthing = true;
	}else if (key == GLFW_KEY_ENTER) {
		applyInsertToAllCursors(icu::UnicodeString::fromUTF8("\n"));
		donesomthing = true;
	}

	if (key == GLFW_KEY_V && (is_control_held || mode == 'n')) {
		paste();
		donesomthing = true;
	}else if (key == GLFW_KEY_C && (is_control_held || mode == 'n')) {
		copy();
		donesomthing = true;
	}else if (key == GLFW_KEY_X && (is_control_held || mode == 'n')) {
		cut();
		donesomthing = true;
	}else if (key == GLFW_KEY_Z && !is_shift_held && (is_control_held || mode == 'n')) {
		activateUndo();
		donesomthing = true;
	}else if (key == GLFW_KEY_U && mode == 'n') {
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

void TextEdit::cut() {
	if (cursors.size() > 1) {
		coppiedText.clear();
		for (auto ci = 0; ci < cursors.size(); ci++) {
			coppiedText.push_back(getSelectedText(cursors[ci]));
		}
	}else{
		auto icutext = getSelectedText(cursors[0]);
		if (icutext.length() != 0) {
			std::string text;
			icutext.toUTF8String(text);
			SetClipboardText(text);
		}
	}
	
	applyDeleteToAllCursors(GLFW_KEY_DELETE, false);
}

void TextEdit::copy(){
	coppiedText.clear();
	
	if (cursors.size() > 1) {
		for (auto ci = 0; ci < cursors.size(); ci++) {
			coppiedText.push_back(getSelectedText(cursors[ci]));
		}
	}else{
		auto icutext = getSelectedText(cursors[0]);
		if (icutext.length() != 0) {
			std::string text;
			icutext.toUTF8String(text);
			SetClipboardText(text);
		}
	}
}

void TextEdit::paste(){
	if (coppiedText.size() == cursors.size()) {
		for (int ci = 0; ci < cursors.size(); ci++) {
			insertTextAtCursor(cursors[ci], coppiedText[ci]);
		}
	}else{
		std::string clipboard_text = GetClipboardText();
		applyInsertToAllCursors(run_fixit_on_text(icu::UnicodeString::fromUTF8(clipboard_text)));
	}
	tryingToEnsureCursorPos = true;
}

icu::UnicodeString TextEdit::getSelectedText(Cursor c) {
	auto sel = _getCursSelec(c);

	int sl = sel.first.first;
	int el = sel.first.second;
	int sc = sel.second.first;
	int ec = sel.second.second;

	icu::UnicodeString start;
	icu::UnicodeString end;

	if (sl == el) {
		lines[sl].line_text.extractBetween(sc, ec, start);
		return start;
	}

	lines[sl].line_text.extractBetween(sc, lines[sl].line_text.length(), start);
	lines[el].line_text.extractBetween(0, ec, end);

	std::vector<icu::UnicodeString> ourlines = {start};

	for (int line = sl + 1; line < el; line++) {
		ourlines.push_back(lines[line].line_text);
	}

	ourlines.push_back(end);

	return joinByString(ourlines, icu::UnicodeString::fromUTF8("\n"));
}

void TextEdit::ensureCursorVisible(Cursor c) {
	double line_start = scrolled_to_vert;
	double char_start = scrolled_to_horz;

	double end_line = line_start+t_h/(float)TextRenderer::get_text_height();
	double end_char = char_start+t_w/(float)TextRenderer::get_text_width(1)-1;
	
	if (!DONT_SCROLL_VERT_CURS) {
		if (end_line-line_start < 14) {
			scroll_vertical_change = (c.head_line-(end_line-line_start)/2) - scrolled_to_vert;
		}else{
			int l1 = c.head_line;
			
			if (l1-4 < line_start) {
				scroll_vertical_change = l1-4 - line_start;
			}else if (l1+7 > end_line) {
				scroll_vertical_change = l1+7 - end_line;
			}
		}
	}

	int c1 = _mapFromRealToVisual(c.head_line, c.head_char);

	if (c1-4 < char_start) {
		scroll_horizontal_change = c1-4-char_start;
	}else if (c1+7 > end_char) {
		scroll_horizontal_change = c1+7-end_char;
	}

	DO_POSITION = true;
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
	DO_POSITION = true;

	if (nl == 0) {
		Edit e = { EditType::ChangeLine, std::move(lines[sl]), sl };
		historyThisUpdate.edits.push_back(std::move(e));

		lines[sl].line_text = start+insert_lines[0]+end;
		lines[sl].changed = true;
		
		if (onlinechange) { onlinechange(EditType::ChangeLine, sl); }
	}else{
		Edit e = { EditType::ChangeLine, std::move(lines[sl]), sl };
		historyThisUpdate.edits.push_back(std::move(e));

		lines[sl].line_text = start+insert_lines[0];
		
		if (onlinechange) { onlinechange(EditType::ChangeLine, sl); }

		for (int i = 1; i < insert_lines.size(); i ++) {
			auto new_line = Line();
			new_line.line_text = insert_lines[i];

			std::string endthing;
			end.toUTF8String(endthing);

			if (i == insert_lines.size()-1) {
				new_line.line_text += end;
			}
			new_line.changed = true;
			
			Edit e = { EditType::InsertLine, new_line.clone(), sl+i };
			historyThisUpdate.edits.push_back(std::move(e));
			
			lines.insert(lines.begin()+sl+i, std::move(new_line));
			
			if (onlinechange) { onlinechange(EditType::InsertLine, sl+i); }
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
		largereditblock = false;
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
	
	if (mode == 'n' && key == GLFW_KEY_SEMICOLON) {
		App::setActiveLeafNode(App::commandPalette);
		return true;
	}

	if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN || key == GLFW_KEY_HOME || key == GLFW_KEY_END) {
		if (is_alt_held && key == GLFW_KEY_DOWN) {
			insertNewCursorDown();
		}else if (is_alt_held && key == GLFW_KEY_UP) {
			insertNewCursorUp();
		}else if (is_alt_held && key == GLFW_KEY_LEFT) {
			gotoPrevMark();
		}else if (is_alt_held && key == GLFW_KEY_RIGHT) {
			gotoNextMark();
		}else{
			applyMoveToAllCursors(key, is_shift_held, is_control_held);
		}

		return true;
	}

	if (key == GLFW_KEY_A && is_control_held) {
		DO_POSITION = true;
		cursors = {cursors[0]};
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
	
	if (is_alt_held || is_control_held) {
		if (App::settings->getValue("use_vim", true)) { // we'll do it in normal or regular mode
			if (key == GLFW_KEY_K) {
				insertNewCursorUp();
				return true;
			}else if (key == GLFW_KEY_J) {
				insertNewCursorDown();
				return true;
			}else if (key == GLFW_KEY_H) {
				gotoPrevMark();
				return true;
			}else if (key == GLFW_KEY_L) {
				gotoNextMark();
				return true;
			}
		}
	}

	if (mode == 'n') {
		if (key == GLFW_KEY_ESCAPE) {
			cursors = {cursors[0]};
			DO_POSITION = true;
		}else if (key == GLFW_KEY_H && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_LEFT, is_shift_held, is_control_held);
			return true;
		}else if (key == GLFW_KEY_L && !is_control_held) {
			applyMoveToAllCursors(GLFW_KEY_RIGHT, is_shift_held, is_control_held);
			return true;
		}else if (key == GLFW_KEY_K && !is_control_held) { // alt is not held because we handled that earlier
			applyMoveToAllCursors(GLFW_KEY_UP, is_shift_held, is_control_held);
			return true;
		}else if (key == GLFW_KEY_J && !is_control_held) { // alt is not held because we handled that earlier
			applyMoveToAllCursors(GLFW_KEY_DOWN, is_shift_held, is_control_held);
			return true;
		}else if ((key == GLFW_KEY_W || key == GLFW_KEY_B) && !is_control_held) {
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
			DO_POSITION = true;
		}
	}

	return false;
}

bool TextEdit::handleUserKey(int key, int scancode, int action, int mods) {
	bool is_shift_held   = ((mods & GLFW_MOD_SHIFT) != 0);
	bool is_control_held = ((mods & GLFW_MOD_CONTROL) != 0);
	
	if (mode == 'n') {
		if (key == GLFW_KEY_I) {
			mode = 'i';
			DO_POSITION = true;
			vim_repeater = 0;
			HandleOverlappingCursors();
			ignoringChar = 'i';
			return true;
		}else if (key == GLFW_KEY_N) {
			mode = 'i';
			vim_repeater = 0;
			applyMoveToAllCursors(GLFW_KEY_RIGHT, is_shift_held, false);
			HandleOverlappingCursors();
			ignoringChar = 'n';
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
			DO_POSITION = true;
			HandleOverlappingCursors();
			return true;
		}
	}
	
	if (mode == 'n' || is_control_held) {
		if (key == GLFW_KEY_M) {
			toggleMark();
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
#ifdef DEBUG
	if (cursors[0].head_line >= lines.size() || cursors[0].head_char > lines[cursors[0].head_line].line_text.length()) {
		std::cout << "NO BUENO\n";
	}if (cursors[0].head_line < 0) {
		std::cout << "LESS THAN 0???\n";
	}

	std::cout << "Char: " << codepoint << "\n";
#endif	
	
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

		if ((char)std::tolower(ch) == ignoringChar || wasmode == 'n') {
			ignoringChar = '\0';
#ifdef DEBUG
			std::cout << "Ignoring character\n";
#endif
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
#ifdef DEBUG
	if (cursors[0].head_line >= lines.size() || cursors[0].head_char > lines[cursors[0].head_line].line_text.length()) {
		std::cout << "NO BUENO\n";
	}if (cursors[0].head_line < 0) {
		std::cout << "LESS THAN 0???\n";
	}

	const char* keyName = glfwGetKeyName(key, 0);
	if (keyName) {
		std::cout << "Key: " << keyName << " press: " << (action==GLFW_PRESS) << "\n";
	}else{
		std::cout << "Key: " << key << " press: " << (action==GLFW_PRESS) << "\n";
	}
#endif
	if (App::activeLeafNode == this) {
		if (action == GLFW_RELEASE) {
			return true;
		}
		
		contextmenu->is_visible_2 = false;
		
		return handleUserKey(key, scancode, action, mods);
	}
	
	return false;
}

void TextEdit::render() {
//#ifdef DEBUG
//		std::cout << "Render\n";
//#endif
	
	if (rounded) {
		App::DrawRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, background_color);
	}else {
		App::DrawRect(t_x, t_y, t_w, t_h, background_color);
	}
	
	int curx = start_x+t_x+App::text_padding;
	int cury = start_y+t_y+App::text_padding;
	
	std::vector<int> drawMarkLines = {};
	
	for (int ln_ren = 0; ln_ren < draw_text.size(); ln_ren++) {
		if (draw_mark[ln_ren]) {
			App::DrawRect(t_x, cury, t_w, TextRenderer::get_text_height(), App::theme.overlay_background_color);
			if (ln_ren == 0 || !draw_mark[ln_ren-1]) {
				drawMarkLines.push_back(cury-1);
			}
			if (ln_ren == draw_text.size()-1 || !draw_mark[ln_ren+1]) {
				drawMarkLines.push_back(cury+TextRenderer::get_text_height());
			}
		}
		cury += TextRenderer::get_text_height();
	}
	
	cury = start_y+t_y+App::text_padding; // reset for later use
	
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

	for (int ln_ren = 0; ln_ren < draw_text.size(); ln_ren++) {
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

		if (mode == 'n' && cs.charUnder != U'\0' && cs.charUnder != U'\t') {
			TextRenderer::draw_text(x, y, cs.charUnder, App::theme.darker_background_color, false); // don't draw emojis here because... it doesn't work
		}
	}
	
	for (int y : drawMarkLines) {
		App::DrawRect(t_x, y, t_w, 1, App::theme.active_color);
	}
	
	Widget::render();
	
	Color* bC = borderColor;
	if (App::activeLeafNode == this) {
		bC = activeBorderColor;
	}
	
	if (bC) {
		if (rounded) {
			App::DrawRoundBorder(t_x, t_y, t_w, t_h, bC, 5, App::text_padding);
		}else{
			App::DrawBorder(t_x, t_y, t_w, t_h, bC);
		}
	}
}

Color* TextEdit::getColorFromToken(int idx, const CW_HighlightToken& token) { // because syntect now returns tokens that are non-overlapping and in order, we'll do binary search
	if (idx < token.start_byte || idx >= token.end_byte) {
		return App::theme.main_text_color;
	}
	
	if (token.role < 0) {
		// it's a difference token
		if (token.role == -1) {
			return App::theme.add_diff;
		}else if (token.role == -2) {
			return App::theme.del_diff;
		}else {
			return App::theme.equal_diff;
		}
	}else{
		return App::theme.syntax_colors[token.role];
	}
}

void TextEdit::executeAction(WidgetActionType typ) {
	if (typ == TAB_WIDTH_CHANGE) {
		DO_POSITION = true;
	}
	Widget::executeAction(typ);
}

inline bool is_emoji_modifier(UChar32 cp) {
	return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

inline bool is_regional_indicator(UChar32 cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

inline bool is_tag_char(UChar32 cp) {
	return cp >= 0xE0020 && cp <= 0xE007E;
}

inline bool looks_like_emoji_base(UChar32 cp) {
	return u_hasBinaryProperty(cp, UCHAR_EXTENDED_PICTOGRAPHIC) ||
		   u_hasBinaryProperty(cp, UCHAR_EMOJI_PRESENTATION)    ||
		   is_regional_indicator(cp);
}

inline bool prev_codepoint(
	const icu::UnicodeString& str,
	int32_t before,
	int32_t& cp_start,
	UChar32& cp
) {
	if (before <= 0) {
		return false;
	}

	cp_start = before - 1;

	const UChar last = str[cp_start];

	if (U16_IS_TRAIL(last) && cp_start > 0) {
		const UChar lead = str[cp_start - 1];
		if (U16_IS_LEAD(lead)) {
			--cp_start;
		}
	}

	cp = str.char32At(cp_start);
	return true;
}

// Consumes one emoji "atom" backwards:
//   base
//   base + VS16
//   base + skin-tone modifier
//   base + VS16 + skin-tone modifier
//
// Returns the UTF-16 start offset of that atom, or -1 if not an emoji atom.
inline int32_t consume_emoji_atom_backward(
	const icu::UnicodeString& str,
	int32_t end
) {
	int32_t p = end;

	int32_t tail_start;
	UChar32 tail;
	if (!prev_codepoint(str, p, tail_start, tail)) {
		return -1;
	}

	// Optional skin-tone modifier at the end.
	if (is_emoji_modifier(tail)) {
		p = tail_start;

		int32_t maybe_vs_start;
		UChar32 maybe_vs;
		if (prev_codepoint(str, p, maybe_vs_start, maybe_vs) && maybe_vs == 0xFE0F) {
			p = maybe_vs_start;
		}

		int32_t base_start;
		UChar32 base;
		if (!prev_codepoint(str, p, base_start, base)) {
			return -1;
		}

		return looks_like_emoji_base(base) ? base_start : -1;
	}

	// Optional VS16 at the end.
	if (tail == 0xFE0F) {
		p = tail_start;

		int32_t base_start;
		UChar32 base;
		if (!prev_codepoint(str, p, base_start, base)) {
			return -1;
		}

		return looks_like_emoji_base(base) ? base_start : -1;
	}

	// Plain emoji base.
	return looks_like_emoji_base(tail) ? tail_start : -1;
}

int32_t TextEdit::get_emoji_sequence_length_backward(
	const icu::UnicodeString& str,
	int32_t index
) {
	const int32_t len = str.length();

	// Here, index is the UTF-16 offset immediately after the emoji candidate.
	if (index <= 0 || index > len) {
		return 0;
	}

	// 1. FAST PATH: Keycap sequence ending at index.
	//
	//   base + U+20E3
	//   base + U+FE0F + U+20E3
	//
	// Examples:
	//   3⃣
	//   3️⃣
	if (str[index - 1] == 0x20E3) {
		if (index >= 2 && is_keycap_base_fast(str[index - 2])) {
			return 2;
		}

		if (
			index >= 3 &&
			str[index - 2] == 0xFE0F &&
			is_keycap_base_fast(str[index - 3])
		) {
			return 3;
		}

		return 0;
	}

	// 2. FAST PATH: subdivision/tag flag sequence.
	//
	// Usually:
	//   U+1F3F4 + tag chars + U+E007F
	//
	// Example class: England/Scotland/Wales flags.
	{
		int32_t cp_start;
		UChar32 cp;

		if (prev_codepoint(str, index, cp_start, cp) && cp == 0xE007F) {
			int32_t p = cp_start;
			bool saw_tag = false;

			while (true) {
				int32_t tag_start;
				UChar32 tag_cp;

				if (!prev_codepoint(str, p, tag_start, tag_cp)) {
					break;
				}

				if (!is_tag_char(tag_cp)) {
					break;
				}

				saw_tag = true;
				p = tag_start;
			}

			int32_t base_start;
			UChar32 base;

			if (
				saw_tag &&
				prev_codepoint(str, p, base_start, base) &&
				base == 0x1F3F4
			) {
				return index - base_start;
			}

			return 0;
		}
	}

	// 3. FAST PATH: regional indicator flags.
	//
	// Regional indicators are grouped in pairs from the start of the RI run.
	// For a run of 2 RI code points, return the pair.
	// For a run of 3 RI code points, the last one is its own cluster.
	{
		int32_t last_ri_start;
		UChar32 last_ri;

		if (
			prev_codepoint(str, index, last_ri_start, last_ri) &&
			is_regional_indicator(last_ri)
		) {
			int32_t p = index;
			int32_t count = 0;
			int32_t starts[2] = { -1, -1 };

			while (true) {
				int32_t s;
				UChar32 cp;

				if (!prev_codepoint(str, p, s, cp)) {
					break;
				}

				if (!is_regional_indicator(cp)) {
					break;
				}

				if (count < 2) {
					starts[count] = s;
				}

				++count;
				p = s;
			}

			// Unicode grapheme pairing behavior:
			// even RI run length => final cluster is last 2 RIs
			// odd RI run length  => final cluster is last 1 RI
			if (count >= 2 && (count % 2) == 0) {
				return index - starts[1];
			}

			return index - starts[0];
		}
	}

	// 4. General emoji atom / ZWJ sequence.
	//
	// Works backwards over:
	//   emoji
	//   emoji + skin tone
	//   emoji + VS16
	//   emoji ZWJ emoji
	//   emoji ZWJ emoji ZWJ emoji ...
	{
		int32_t start = consume_emoji_atom_backward(str, index);

		if (start < 0) {
			return 0;
		}

		while (start > 0) {
			int32_t zwj_start;
			UChar32 zwj;

			if (!prev_codepoint(str, start, zwj_start, zwj) || zwj != 0x200D) {
				break;
			}

			const int32_t prev_atom_end = zwj_start;
			const int32_t prev_atom_start =
				consume_emoji_atom_backward(str, prev_atom_end);

			if (prev_atom_start < 0) {
				break;
			}

			start = prev_atom_start;
		}

		return index - start;
	}
}

void TextEdit::position(int x, int y, int w, int h) {
//#ifdef DEBUG
//	std::cout << "Position\n";
//#endif
	
	tabWidth = App::settings->getValue("tab_width", 4);
	
	int old_x = t_x;
	int old_y = t_y;
	int old_w = t_w;
	int old_h = t_h;
	
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	POS_FUNC(this);
	
	if (old_x != t_x || old_y != t_y || old_w != t_w || old_h != t_h) {
		DO_POSITION = true;
	}
	if (changed_during_update || scroll_vertical_change != 0 || scroll_horizontal_change != 0) {
		DO_POSITION = true;
	}
	
	max_scroll_vert = lines.size()-1;
	max_scroll_horz = max_line_len-1;
	
	if (tryingToEnsureCursorPos) {
		tryingToEnsureCursorPos = false;
		DO_POSITION = true;
		ensureCursorVisible(cursors[0]);
	}
	
	
	if (scroll_vertical_change != 0 || scroll_horizontal_change != 0) {
		double value = (App::settings->getValue("smooth_scroll", 0.2f) * App::settings->getValue("anim_speed", 1.0f));
		double changexby = scroll_horizontal_change*value;
		double changeyby = scroll_vertical_change*value;
		
		if (scroll_vertical_change < 0.1 && scroll_vertical_change > -0.1) {
			changeyby = scroll_vertical_change;
		}
		if (scroll_horizontal_change < 0.1 && scroll_horizontal_change > -0.1) {
			changexby = scroll_horizontal_change;
		}
		
		scrolled_to_horz += changexby;
		scrolled_to_vert += changeyby;
		
		scroll_horizontal_change -= changexby;
		scroll_vertical_change -= changeyby;
		
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
		
		App::time_till_regular = 2;
	}
	
	scrollbar_v->is_visible = scrollbar_vertical;
	scrollbar_h->is_visible = scrollbar_horizontal;
	Widget::position(t_x, t_y, t_w, t_h);
	
	// let's make sure vim mode is allowed...
	
	if (!App::settings->getValue("use_vim", false)) {
		mode = 'i';
	}
	
	// let's set the cursor info
	if (App::expectedCursorType == -1 && cursor_in_this) { // only set cursor if expected to be arrow right now
		App::expectedCursorType = 4; // ibar
	}
	
	bool active = App::activeLeafNode == this;
	if (WAS_ACTIVE != active) {
		DO_POSITION = true;
		WAS_ACTIVE = active;
	}
	
	if (!DO_POSITION) {
		wasmode = mode;
		changed_during_update = false;
		DID_POSITION = false;
		updateUndoHistory();
		return;
	}
	
	DID_POSITION = true;
	DO_POSITION = false;
	
	std::vector<UChar32> tabReplacementChars = {};
	for (int i = 0; i < tabWidth; i++) {
		tabReplacementChars.push_back(U' ');
	}
	
	draw_text.clear();
	draw_color.clear();
	draw_cursor.clear();
	draw_selection.clear();
	draw_diagnostics.clear();
	draw_mark.clear();

	int line_start = floor(scrolled_to_vert);
	int char_start = floor(scrolled_to_horz);

	start_x = -fmod(scrolled_to_horz, 1) * TextRenderer::get_text_width(1);
	start_y = -fmod(scrolled_to_vert, 1) * TextRenderer::get_text_height();

	if (char_start == 1) {
		char_start -= 1;
		start_x -= TextRenderer::get_text_width(1);
	}else if (char_start > 1) { // because of emojis we need to make sure they're all the way off screen before we stop rendering them
		char_start -= 2;
		start_x -= TextRenderer::get_text_width(2);
	}
	

	int end_line = line_start+ceil((float)t_h/(float)TextRenderer::get_text_height()) + 1;
	int end_char = char_start+ceil((float)t_w/(float)TextRenderer::get_text_width(1)) + 1;
	
	if (line_start > 0) {
		line_start -= 1;
		start_y -= TextRenderer::get_text_height();
	}

	int start_highlight = fmax(0, line_start-5);
	int end_highlight = fmin(lines.size(), end_line+5);
	Highlight(start_highlight, end_highlight);

	for (int ln_num = line_start; ln_num < end_line; ln_num ++) {
		if (ln_num < 0 || ln_num >= lines.size()) {
			continue;
		}

		icu::UnicodeString& lineText = lines[ln_num].line_text;

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

		// selection "✅"

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
			int end_sec = lineText.length();
			if (end_line == ln_num) {
				end_sec = end_char;
			}
			
			bool add_one_to_last_char = false;
			
			if (end_line > ln_num) {
				add_one_to_last_char = true;
			}
			
			selections.push_back({start_sec, end_sec, add_one_to_last_char});
			draw_selec.push_back({-1, -1});
		}

		// run through
		
		int cur_char = 0;
		int cur_length = 0;
		int skipping = 0; // a variable to handle emojis, because they take up multiple codepoints
		int cur_token_idx = 0;
		if (lines[ln_num].tokens.empty()) {
			cur_token_idx = -1;
		}
		
		for (int true_char_indx = 0; true_char_indx < lineText.length()+1; true_char_indx += 1) {
			if (cur_token_idx != -1 && lines[ln_num].tokens[cur_token_idx].end_byte <= true_char_indx) {
				cur_token_idx = -1;
				for (auto t = cur_token_idx+1; t < lines[ln_num].tokens.size(); t++) {
					if (lines[ln_num].tokens[t].end_byte > true_char_indx && lines[ln_num].tokens[t].start_byte <= true_char_indx) {
						cur_token_idx = t;
						break;
					}
				}
			}
			
			for (int i = 0; i < selections.size(); i++) {
				auto s = selections[i];
				if (s[0] <= true_char_indx && draw_selec[i][0] == -1) {
					draw_selec[i][0] = cur_length; // if there's no stated start, and we are now >= to the start position, record the start in relative characters.
				}
				
				if (s[0] <= true_char_indx && s[1] >= true_char_indx) {
					draw_selec[i][1] = cur_length; // if we're in the selection (start is left of here and end is right of here) then record the end to be here
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
			
			int newskip = skipping;
			if (skipping == 0) {
				newskip = get_emoji_sequence_length(lineText, true_char_indx);
			}
			
			
			for (auto c : cursors) {
				if (App::activeLeafNode == this && c.head_line == ln_num && c.head_char == true_char_indx && cur_char >= char_start) {
					CursorScreen dc = CursorScreen();
					dc.rel_line = draw_text.size();
					dc.rel_char = cur_length;
					if (newskip == 0) {
						dc.charUnder = lineText[true_char_indx];
					}
					draw_cursor.push_back(dc);
				}
			}
			
			// handling emojis
			if (skipping != 0) {
				skipping -= 1;
				if (skipping == 0) {
					if (cur_char >= char_start) {
						cur_length += 2;
					}else if (cur_char == char_start-1) { // because they're two long
						cur_length += 1;
						final_line.append('#');
						final_color.push_back(App::theme.main_text_color);
					}
					cur_char += 2;
				}
				continue;
			}else{
				skipping = newskip;
				if (skipping != 0) {
					if (cur_char >= char_start) {
						for (int i = 0; i < skipping; i++) {
							final_line.append(lineText.charAt(i+true_char_indx));
							final_color.push_back(App::theme.main_text_color);
						}
					}
					
					skipping -= 1;
					
					if (skipping == 0) {
						if (cur_char >= char_start) {
							cur_length += 2;
						}else if (cur_char == char_start-1) { // because they're two long
							cur_length += 1;
							final_line.append('#');
							final_color.push_back(App::theme.main_text_color);
						}
						cur_char += 2;
					}
					continue;
				}
			}
			
			// regular text
			
			UChar32 chr;
			if (true_char_indx == lineText.length()) {
				chr = U' '; // need this for selections to look right, inserting a not-real space for... things
			}else{
				chr = lineText.charAt(true_char_indx);
			}
			
			bool finished_line = false;

			std::vector<UChar32> tohandle = {};

			if (chr == U'\t') { // ensure tabs are not just 1 char wide
				tohandle = tabReplacementChars;
			}else{
				tohandle = {chr};
			}
			
			for (auto c : tohandle) {
				if (cur_char >= char_start) {
					final_line.append(c);
					cur_length += 1;
					
					if (!highlighter && !alreadyHighlighted) {
						final_color.push_back(App::theme.main_text_color);
					}else{
						Color* col = App::theme.main_text_color;
						if (cur_token_idx != -1){
							col = getColorFromToken(true_char_indx, lines[ln_num].tokens[cur_token_idx]);
						}
						final_color.push_back(col);
					}
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
			if (draw_selec[i][0] == -1) { // sometimes a selection on a given line isn't in visible range (ie to the left or right), so we don't want to render anything for those cases. To be clear
				continue;
			}
			
			if (draw_selec[i][1] == 0 && final_line.length() == 0) {
				continue;
			}
			
			if (selections[i][2] == 1) { // it's the little things like this that make me wonder what I was smoking when I made this (spoiler: I wasn't smoking anything. I'm just this creative)
				draw_selec[i][1] ++;     // oh wait I figured it out. It's so that if we wrap a line on the right, we draw a little thing on it, to clarify that we are selecting the newline. Clever me.
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

		if (thisdiag != "") { // this contains a "👍🏾" thumbs up emoji
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
		draw_mark.push_back(lines[ln_num].isMarked);
		draw_color.push_back(final_color);
	}
	
	wasmode = mode;
	
	changed_during_update = false;
	updateUndoHistory();
}

bool TextEdit::on_scroll_event(double xchange, double ychange) {
	if (glfwGetKey(App::window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(App::window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
		xchange = ychange;
		ychange = 0;
	}
	
	if (!cursor_in_this) {
		return false;
	}
	
	scroll_horizontal_change += xchange*6;
	scroll_vertical_change += ychange*6;
	DO_POSITION = true;
	
	double theoretical_vert = scrolled_to_vert + scroll_vertical_change;
	double theoretical_horz = scrolled_to_horz + scroll_horizontal_change;
	if ((theoretical_vert < max_scroll_vert && theoretical_vert > 0 && ychange != 0)) {
		return true; // these events we've scrolled, and we're still in bounds, so we handle it.
	}
	if ((theoretical_horz < max_scroll_horz && theoretical_horz > 0 && xchange != 0)) {
		return true;
	}
	
	return false;
}

Cursor TextEdit::getCursorForMousePosition(int mx, int my, bool* gottoit) {
	int first_line = floor(scrolled_to_vert);
	int first_char = floor(scrolled_to_horz);
	
	if (first_char == 1) { // this is for some reason correct... (I'm not planning on questioning it.)
		first_char -= 1;
	}else if (first_char > 1) { // okay these are because we need to render partial lines/chars - the -= 2 is because of emojis
		first_char -= 2;
	}
	
	if (first_line > 0) {
		first_line -= 1;
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
	
	if (is_visible && !cursor_in_this) {
		return false;
	}
	
	if (action == GLFW_PRESS && App::activeLeafNode != this) {
		App::setActiveLeafNode(this);
	}
	
	if (scrollbar_vertical && scrollbar_v->on_mouse_button_event(button, action, mods)) { return true; };
	if (scrollbar_horizontal && scrollbar_h->on_mouse_button_event(button, action, mods)) { return true; };
	if (contextmenu->is_visible_2 && contextmenu->is_visible_3 && contextmenu->on_mouse_button_event(button, action, mods)) { return true; }
	
	if (App::activeLeafNode != this) {
		return false;
	}
	
	if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_RIGHT){
		contextmenu->is_visible_2 = true;
		contextmenu->x_loc = mx;
		contextmenu->y_loc = my;
		
		contextmenu->position(t_x, t_y, t_w, t_h);
		
		bool updtit = false;
		int nx = mx;
		int ny = my;
		
		if (contextmenu->t_x + contextmenu->t_w > t_w+t_x) {
			updtit = true;
			nx -= contextmenu->t_w;
		}
		if (contextmenu->t_y + contextmenu->t_h > t_h+t_y) {
			updtit = true;
			ny -= contextmenu->t_h;
		}
		
		if (updtit) {
			contextmenu->x_loc = nx;
			contextmenu->y_loc = ny;
			contextmenu->position(nx, ny, t_w, t_h);
		}
		
		if (cursors[0].anchor_char != cursors[0].head_char || cursors[0].anchor_line != cursors[0].head_line) {
			return true; // only skip the cursor changing position if there's no selection
		}
	}else if (action == GLFW_PRESS){
		contextmenu->is_visible_2 = false;
	}
	
	if (action == GLFW_PRESS) {
		DO_POSITION = true;
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
}

bool TextEdit::on_mouse_move_event() {
	int mx = App::mouseX;
	int my = App::mouseY;
	 
	if (is_selecting_text_with_mouse) {
		int state = glfwGetMouseButton(App::window, GLFW_MOUSE_BUTTON_LEFT);

		if (state == GLFW_RELEASE) {
			is_selecting_text_with_mouse = false;
		}else{
			Cursor crsr = getCursorForMousePosition(mx, my);
			Cursor cur = cursors[0];
	
			cur.head_char = crsr.head_char; // because this is drag, we don't need to move the anchor
			cur.head_line = crsr.head_line;
			cur.preffered_collumn = crsr.preffered_collumn;
	
			cursors = { cur };
	
			tryingToEnsureCursorPos = true;
		}
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
	if (wordLength > 0) {
		blockText.extractBetween(wordStart, blockPos, word);
	}

	return word;
}