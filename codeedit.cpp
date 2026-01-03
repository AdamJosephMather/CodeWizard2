#include "codeedit.h"
#include "brokenstatemenu.h"
#include "text_renderer.h"
#include "application.h"
#include "textedit.h"
#include "linenumbers.h"
#include "tinyfiledialogs.h"

#include <fstream>
#include <unicode/regex.h>
#include <unicode/stringoptions.h>
#include "editor.h"
#include "curler.h"
#include "modelxrunner.h"

std::set<UChar32> whitespace_before_comment = {U'\t', U' '};

CodeEdit::CodeEdit(Widget* parent, int tabid, App::PosFunction positioner, App::UpdateFInfoFunction fupdater) : Widget(parent) {
	before_self_close = [&](Widget* w) {
		if (App::lsp_client_map[lsp]) {
			for (int i = static_cast<int>(App::lsp_client_map[lsp]->connected_edits.size())-1; i >= 0; i--) {
				if (App::lsp_client_map[lsp]->connected_edits[i] == this) { 
					App::lsp_client_map[lsp]->connected_edits.erase(App::lsp_client_map[lsp]->connected_edits.begin()+i);
				}
			}
		}
	};
	
	TABID = tabid;
	POS_FUNC = positioner;
	FUPDATER = fupdater;
	
	id = icu::UnicodeString::fromUTF8("CodeEdit");
	
	broken_state_menu = new BrokenStateMenu(nullptr, "Reload", "Overwrite", "File changed on disk.");
	broken_state_menu->first_callback = [&](){
		reload_file();
	};
	broken_state_menu->second_callback = [&](){
		overwrite_file();
	};
	
	fixit_request_menu = new BrokenStateMenu(nullptr, "Yes", "No", "Detected space based indenting, Fixit?");
	fixit_request_menu->first_callback = [&](){
		App::RemoveWidgetFromParent(fixit_request_menu);
		run_fixit();
		REQUESTING_FIXIT = false;
	};
	fixit_request_menu->second_callback = [&](){
		App::RemoveWidgetFromParent(fixit_request_menu);
		REQUESTING_FIXIT = false;
	};
	
	renamecursor = Cursor();
	renamebox = new TextEdit(nullptr, [&](Widget* w){
		if (renamecursor.head_line > textedit->lines.size() && renamebox->parent == this) {
			App::RemoveWidgetFromParent(renamebox);
			App::setActiveLeafNode(textedit);
			return;
		}
		
		int col = textedit->_mapFromRealToVisual(renamecursor.head_line, renamecursor.head_char);
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (renamecursor.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/3;
		w->t_h = App::text_padding*2+TextRenderer::get_text_height();
	});
	renamebox->background_color = App::theme.extras_background_color;
	renamebox->border = true;
	
	completionbox = new ListBox(this, [&](Widget* w){
		Cursor c = textedit->cursors[0];
		int col = textedit->_mapFromRealToVisual(c.head_line, c.head_char);
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (c.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/3;
	});
	completionbox->is_visible_layered = false;
	completionbox->rounded = true;
	
	hoverbox = new TextEdit(nullptr, [&](Widget* w){
		int col = textedit->_mapFromRealToVisual(hoverCrsr.head_line, hoverCrsr.head_char);
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (hoverCrsr.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/2;
		w->t_h = w->t_w/2;
	});
	hoverbox->background_color = App::theme.extras_background_color;
	hoverbox->border = true;
	hoverbox->contextmenu->is_visible_3 = true;
	
	find_menu_open = false;
	
	allButton = new Button(nullptr, icu::UnicodeString("All"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = t_x+t_w-5-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-5;
	}, [&](Button* b){
		replaceAll(findTextEdit->getFullText(), replaceTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	allButton->rounded = true;
	
	nextReplButton = new Button(nullptr, icu::UnicodeString::fromUTF8("-→"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = allButton->t_x-5-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-5;
	}, [&](Button* b){
		activateReplace(true, findTextEdit->getFullText(), replaceTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	nextReplButton->rounded = true;
	
	nextButton = new Button(nullptr, icu::UnicodeString::fromUTF8("-→"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = t_x+t_w-5-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-findTextEdit->t_h-10;		
	}, [&](Button* b){
		activateFind(true, findTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	nextButton->rounded = true;
	
	prevButton = new Button(nullptr, icu::UnicodeString::fromUTF8("←-"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = nextButton->t_x-5-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-findTextEdit->t_h-10;
	}, [&](Button* b){
		activateFind(false, findTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	prevButton->rounded = true;
	
	replaceTextEdit = new TextEdit(nullptr, [&](Widget* t){
		int h = TextRenderer::get_text_height()*std::min((int)replaceTextEdit->lines.size(), 3)+10;
		
		replaceTextEdit->t_x = t_x+5;
		replaceTextEdit->t_w = (nextReplButton->t_x - replaceTextEdit->t_x)-5;
		replaceTextEdit->t_h = h;
		replaceTextEdit->t_y = t_y+t_h-h-5;
		
		allButton->t_h = h;
		allButton->t_y = replaceTextEdit->t_y;
		nextReplButton->t_h = h;
		nextReplButton->t_y = replaceTextEdit->t_y;
	});
	replaceTextEdit->rounded = true;
	
	findTextEdit = new TextEdit(nullptr, [&](Widget* t){
		int h = TextRenderer::get_text_height()*std::min((int)findTextEdit->lines.size(), 3)+10;
		
		findTextEdit->t_x = t_x+5;
		findTextEdit->t_h = h;
		findTextEdit->t_w = (prevButton->t_x - findTextEdit->t_x)-10-findTextEdit->t_h; // the space of the bar, and the width of the checkbox, and the padding for each
		findTextEdit->t_y = t_y+t_h-replaceTextEdit->t_h-h-10;
		
		nextButton->t_h = h;
		nextButton->t_y = findTextEdit->t_y;
		prevButton->t_h = h;
		prevButton->t_y = findTextEdit->t_y;
	});
	findTextEdit->rounded = true;
	
	caseSensitivity = new CheckBox(nullptr, [&](CheckBox* c, int,int,int,int){
		c->t_h = findTextEdit->t_h;
		c->t_w = c->t_h;
		c->t_x = findTextEdit->t_w + findTextEdit->t_x + 5;
		c->t_y = findTextEdit->t_y;
	}, nullptr);
	caseSensitivity->rounded = true;
	
	allButton->border = true;
	nextReplButton->border = true;
	nextButton->border = true;
	prevButton->border = true;
	replaceTextEdit->border = true;
	findTextEdit->border = true;
	caseSensitivity->border = true;
	
	line_numbers = new LineNumbers(this);
	
	textedit = new TextEdit(this, [&](Widget* t){
		textedit->t_x = t_x+line_numbers->t_w;
		textedit->t_y = t_y;
		textedit->t_w = t_w-line_numbers->t_w;
		if (find_menu_open) {
			textedit->t_h = findTextEdit->t_y - t_y - 5;
		}else{
			textedit->t_h = t_h;
		}
	});
	textedit->scrollbar_vertical = true;
	textedit->scrollbar_horizontal = true;
	textedit->contextmenu->is_visible_3 = true;
	
	textedit->contextmenu->addSeparaterToMenu();
	
	textedit->contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Goto Def (LSP)"),   [&](Widget* w){
		if (App::lsp_client_map[lsp] && file) {
			goto_id = App::lsp_client_map[lsp]->requestGotoDefinition(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
		}
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->addToMenu(icu::UnicodeString::fromUTF8("Rename Symbol (LSP)"),   [&](Widget* w){
		if (App::lsp_client_map[lsp] && file) {
			renamecursor = textedit->cursors[0];
			if (renamebox->parent != this){
				App::MoveWidget(renamebox, this);
				App::setActiveLeafNode(renamebox);
			}
			renamebox->wasmode = 'n';
			renamebox->mode = 'i';
			renamebox->setFullText(icu::UnicodeString());
		}
		
		textedit->contextmenu->is_visible_2 = false;
	});
	
	showErrorsButton = new Button(nullptr, icu::UnicodeString("Show/Hide Errors"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = textedit->t_x+textedit->t_w-10-tw;
		b->t_y = textedit->t_y+textedit->t_h-10-th;
	}, [&](Button* b){
		showhideerrors();
	});
	showErrorsButton->transparent = true;
	showErrorsButton->rounded = true;
	
	errorMenu = new ListBox(this, [&](Widget* w){
		w->t_w = textedit->t_w/3; // height is set by the listbox
		
		w->t_x = textedit->t_x+textedit->t_w-w->t_w-10;
		w->t_y = showErrorsButton->t_y-w->t_h-10;
	});
	errorMenu->rounded = true;
	errorMenu->is_visible_layered = false;
	errorMenu->toshow = 13;
	errorMenu->ONCLICK = [&](Widget* w, int sel){
		gotoerror(sel);
	};
	
	
	textedit->highlighter = nullptr;
	textedit->getblankhighlighting = nullptr;
	textedit->highlighterNotEqual = nullptr;
//	textedit->ontextchange = [&](Widget* w){
//		onTextChanged(w);
//	};
	textedit->onlinechange = [&](EditType typ, int lineindex){
		madeChangeBetweenSaves = true;
		
		if (hoverbox->parent == this) {
			App::RemoveWidgetFromParent(hoverbox);
			hoverCrsr = Cursor();
		}
		
		if (!App::lsp_client_map[lsp] || !file || file->filepath == "") {
			return;
		}
		
		if (!App::lsp_client_map[lsp]->supportsIncrementalChanges) {
			onTextChanged(textedit);
			return;
		}
		
		LineEditType t;
		
		if      (typ == EditType::InsertLine) t = LineEditType::InsertLine;
		else if (typ == EditType::ChangeLine) t = LineEditType::ChangeLine;
		else if (typ == EditType::DeleteLine) t = LineEditType::DeleteLine;
		
		Line l = textedit->lines[lineindex];
		std::string text;
		l.line_text.toUTF8String(text);
		
		App::lsp_client_map[lsp]->applyDocumentEdit(file->filepath, t, text, lineindex);
	};
	
	textedit->getIndentationLevelAfterLine = indentIdentifierAfterLine;
	
	line_numbers->setTextedit(textedit);
	
	hoverthread = std::thread([&]() {
		while (true){
			if (closing) {
				return;
			}
			
			if (textedit->contextmenu->is_visible_2 && hoverbox->parent == this) {
				App::RemoveWidgetFromParent(hoverbox);
			}
			
			if (timeuntil <= 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}else{
				timeuntil -= 20;
				if (timeuntil <= 0) {
					if (!App::lsp_client_map[lsp]) { continue; }
					
					int mx = App::mouseX;
					int my = App::mouseY;
					
					bool gottoit = false;
					Cursor crsr = textedit->getCursorForMousePosition(mx, my, &gottoit);
					
					if (!gottoit) { continue; }
					
					hoverCrsr.head_char = crsr.head_char;
					hoverCrsr.head_line = crsr.head_line;
					
					should_move_mouse_hover = false;
					hover_id = App::lsp_client_map[lsp]->requestHover(file->filepath, crsr.head_line, crsr.head_char);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	});
	
	chauffeurthread = std::thread([&]() {
		while (true){
			if (closing) {
				return;
			}
			
			if (timeuntilchauffeur <= 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}else{
				timeuntilchauffeur -= 20;
				if (timeuntilchauffeur <= 0) {
					
					Cursor prefix_c = textedit->cursors[0];
					Cursor suffix_c = textedit->cursors[0];
					
					prefix_c.anchor_line -= App::settings->getValue("chauffeur_prefix_lines", 10);
					prefix_c.anchor_char = 0;
					suffix_c.anchor_line += App::settings->getValue("chauffeur_suffix_lines", 7);
					
					prefix_c.anchor_line = std::max(0, prefix_c.anchor_line);
					suffix_c.anchor_line = std::min((int)textedit->lines.size()-1, suffix_c.anchor_line);
					
					suffix_c.anchor_char = textedit->lines[suffix_c.anchor_line].line_text.length();
					
					std::string prefix_s;
					std::string suffix_s;
					
					icu::UnicodeString prefix = textedit->getSelectedText(prefix_c);
					icu::UnicodeString suffix = textedit->getSelectedText(suffix_c);
					
					prefix.toUTF8String(prefix_s);
					suffix.toUTF8String(suffix_s);
					
					std::string insertion = ModelXRunner::generate(prefix_s, App::settings->getValue("chauffeur_max_continue", 6));
					
					if (insertion == "") {
						continue;
					}
					
					std::vector<icu::UnicodeString> compld = {icu::UnicodeString::fromUTF8(insertion)};
					
					completionbox->is_visible_layered = true;
					completionbox->setElements(compld);
					are_code_actions = false;
					is_chauffeur = true;
					completionbox->toshow = compld.size();
					App::time_till_regular = 2;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	});
}

void CodeEdit::showhideerrors() {
	errorMenu->is_visible_layered = !errorMenu->is_visible_layered;
}

void CodeEdit::gotoerror(int s) {
	if (s >= errorlines.size() || s < 0) {
		return;
	}
	
	int ln = errorlines[s];
	
	if (ln >= textedit->lines.size()) {
		return;
	}
	
	textedit->cursors = { { ln, 0, ln, 0, 0 } };
	textedit->tryingToEnsureCursorPos = true;
}

void CodeEdit::detectLanguage() {
	if (App::lsp_client_map[lsp]) {
		for (int i = static_cast<int>(App::lsp_client_map[lsp]->connected_edits.size())-1; i >= 0; i--) {
			if (App::lsp_client_map[lsp]->connected_edits[i] == this) { 
				App::lsp_client_map[lsp]->connected_edits.erase(App::lsp_client_map[lsp]->connected_edits.begin()+i);
			}
		}
	}
	
	textedit->highlighter = nullptr;
	textedit->getblankhighlighting = nullptr;
	textedit->highlighterNotEqual = nullptr;
	
	if (highlighter) {
		delete highlighter;
		highlighter = nullptr;
	}
	
	for (int i = 0; i < textedit->lines.size(); i++) { // forcefully rehighlight the document
		textedit->lines[i].highlightinguptodate = false;
		textedit->lines[i].diagnostics.clear();
	}
	
	std::string path = file->filepath;
	std::filesystem::path filesystempath(path);
	auto extension = filesystempath.extension().string();
	
	if (extension.length() > 1) {
		extension = extension.substr(1);
	}
	
	language = "";
	lsp = "";
	
	for (auto it : App::languagemap) {
		for (auto ext : it.second.filetypes) {
			if (ext == extension) {
				language = it.first;
				break;
			}
		}
		if (language != "") {
			break;
		}
	}
	
	if (language == "") {
		return;
	}
	
	auto textmatefile = App::languagemap[language].textmatefile;
	lsp = App::languagemap[language].lsp;
	
	std::cout << "Initial LSP offerings: " << lsp << "\n";
	
	std::string pos_lsp = App::settings->getProjectLSP(language);
	if (pos_lsp != "") {
		std::cout << "Found project specific lsp for "<<language<<": " << pos_lsp << " (switching to new option)\n";
		lsp = pos_lsp;
	}
	
	if (lsp != ""){
		App::readyLSP(lsp);
		
		if (App::lsp_client_map[lsp]) {
			bool foundit = false;
			for (auto c : App::lsp_client_map[lsp]->connected_edits) {
				if (c == this) {
					foundit = true;
					break;
				}
			}
			
			if (!foundit) {
				App::lsp_client_map[lsp]->connected_edits.push_back(this);
			}
		}
	}
	
	if (textmatefile == "") {
		return;
	}
	
	highlighter = new Highlighter();
	bool success = highlighter->loadGrammarFile(App::languagemap[language].textmatefile);
	
	if (!success) {
		return;
	}
	
	textedit->highlighter = [&](icu::UnicodeString line, TextMateInfo info) {
		return highlighter->highlightLine(line, info);
	};
	textedit->getblankhighlighting = [&](){
		return highlighter->getDefaultLineInfo();
	};
	textedit->highlighterNotEqual = [&](TextMateInfo* one, TextMateInfo* two){
		return one->contextStack.back().hash != two->contextStack.back().hash;
	};
}

void CodeEdit::executeAction(WidgetActionType typ) {
	if (typ == WidgetActionType::RESTART_LSP) {
		if (lsp != ""){
			App::readyLSP(lsp);
			
			if (App::lsp_client_map[lsp]) {
				bool foundit = false;
				for (auto c : App::lsp_client_map[lsp]->connected_edits) {
					if (c == this) {
						foundit = true;
						break;
					}
				}
				
				if (!foundit) {
					App::lsp_client_map[lsp]->connected_edits.push_back(this);
				}
				
				if (file) {
					std::string str;
					textedit->getFullText().toUTF8String(str);
					App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
				}
			}
		}
	}
	
	Widget::executeAction(typ);
}

void CodeEdit::run_fixit() {
	std::vector<icu::UnicodeString> lns;
	for (auto l : textedit->lines) {
		lns.push_back(l.line_text);
	}
	
	textedit->setFullText(run_fixit_on_lines(lns));
	onTextChanged(textedit);
}

int CodeEdit::analyzeForFixit_on_lines(const std::vector<Line>& lines) {
	std::vector<icu::UnicodeString> lines_new;
	
	for (auto l : lines) {
		lines_new.push_back(l.line_text);
	}
	
	return analyzeForFixit(lines_new);
}

void CodeEdit::openFile() {
	madeChangeBetweenSaves = false;
	was_in_a_file = true;
	std::unique_lock<std::mutex> lock(App::canMakeChanges, std::try_to_lock);
	std::unique_lock<std::mutex> lock2(saving_lock);
	
	REQUESTING_FIXIT = false;
	if (fixit_request_menu->parent == this) {
		App::RemoveWidgetFromParent(fixit_request_menu);
	}
	std::string path = file->filepath;
	
	detectLanguage();
	
	if (!fileExists(path)) {
		file = nullptr;
		textedit->setFullText(icu::UnicodeString::fromUTF8("File does not exist: "+path));
		onTextChanged(textedit);
		last_file_mod_time = {};
		return;
	}
	
	if (isBinaryFile(path)){
		file = nullptr;
		textedit->setFullText(icu::UnicodeString::fromUTF8("File detected as binary file: "+path+"\nDID NOT OPEN"));
		onTextChanged(textedit);
		last_file_mod_time = {};
		return;
	}
	
	bool worked = true;
	icu::UnicodeString text = App::readFileToUnicodeString(path, worked);
	
	if (worked) {
		last_file_mod_time = std::filesystem::last_write_time(path);
		
		textedit->setFullText(text);
		onTextChanged(textedit);
		madeChangeBetweenSaves = false;
		
		int indt = analyzeForFixit_on_lines(textedit->lines);
		if (indt != 0) {
			REQUESTING_FIXIT = true;
			App::MoveWidget(fixit_request_menu, this);
		}
		
		if (App::lsp_client_map[lsp]) {
			std::string str;
			text.toUTF8String(str);
			App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
		}
	}else {
		file = nullptr;
		textedit->setFullText(icu::UnicodeString::fromUTF8("Failed to open file: "+path+"\n\n")+text);
		onTextChanged(textedit);
		last_file_mod_time = {};
	}
	
	file->is_opening = false;
}

LineResult CodeEdit::highlightline(icu::UnicodeString line, TextMateInfo info) {
	return highlighter->highlightLine(line, info);
}

int CodeEdit::indentIdentifierAfterLine(icu::UnicodeString line, icu::UnicodeString nextline) {
	bool in_meat = false;
	int indent_levels = 0;
	int openers = 0;
	UChar32 lastChar = U' ';
	
	for (int i = 0; i < line.length(); i++) {
		UChar32 c = line.char32At(i);
		
		if (!in_meat) {
			if (c == U'\t') {
				indent_levels += 1;
			}else{
				in_meat = true;
			}
		}
		
		if (in_meat){
			if (c == U'(' || c == U'{' || c == U'['){
				openers += 1;
			}else if ((c == U')' || c == U'}' || c == U']') && openers > 0){
				openers -= 1;
			}
			lastChar = c;
		}
	}
	
	indent_levels += std::max(0, openers);
	
	if (lastChar == U':') {
		indent_levels ++;
	}
	
	return indent_levels;
}

void CodeEdit::render() {
	if (FILE_BROKEN_STATE) {
		// we don't need to run with skiz because this is already skizzed in that size
		broken_state_menu->render();
	}else if (REQUESTING_FIXIT){
		// we don't need to run with skiz because this is already skizzed in that size
		fixit_request_menu->render();
	}else{
		if (find_menu_open) {
			App::DrawRect(t_x, textedit->t_y + textedit->t_h, t_w, t_h-textedit->t_h, App::theme.extras_background_color);
			
			App::runWithSKIZ(allButton->t_x, allButton->t_y, allButton->t_w, allButton->t_h, [&](){
				allButton->render();
			});
			
			App::runWithSKIZ(nextReplButton->t_x, nextReplButton->t_y, nextReplButton->t_w, nextReplButton->t_h, [&](){
				nextReplButton->render();
			});
			
			App::runWithSKIZ(replaceTextEdit->t_x, replaceTextEdit->t_y, replaceTextEdit->t_w, replaceTextEdit->t_h, [&](){
				replaceTextEdit->render();
			});
			
			App::runWithSKIZ(nextButton->t_x, nextButton->t_y, nextButton->t_w, nextButton->t_h, [&](){
				nextButton->render();
			});
			
			App::runWithSKIZ(prevButton->t_x, prevButton->t_y, prevButton->t_w, prevButton->t_h, [&](){
				prevButton->render();
			});
			
			App::runWithSKIZ(findTextEdit->t_x, findTextEdit->t_y, findTextEdit->t_w, findTextEdit->t_h, [&](){
				findTextEdit->render();
			});
			App::runWithSKIZ(caseSensitivity->t_x, caseSensitivity->t_y, caseSensitivity->t_w, caseSensitivity->t_h, [&](){
				caseSensitivity->render();
			});
		}
		
		App::runWithSKIZ(line_numbers->t_x, line_numbers->t_y, line_numbers->t_w, textedit->t_h, [&](){
			line_numbers->render();
		});
		
		App::runWithSKIZ(textedit->t_x, textedit->t_y, textedit->t_w, textedit->t_h, [&](){
			textedit->render();
			
			App::runWithSKIZ(completionbox->t_x, completionbox->t_y, completionbox->t_w, completionbox->t_h, [&](){
				completionbox->render();
			});
			App::runWithSKIZ(errorMenu->t_x, errorMenu->t_y, errorMenu->t_w, errorMenu->t_h, [&](){
				errorMenu->render();
			});
			if (showErrorsButton->parent == this) {
				App::runWithSKIZ(showErrorsButton->t_x, showErrorsButton->t_y, showErrorsButton->t_w, showErrorsButton->t_h, [&](){
					showErrorsButton->render();
				});
			}
			if (hoverbox->parent == this) {
				App::runWithSKIZ(hoverbox->t_x, hoverbox->t_y, hoverbox->t_w, hoverbox->t_h, [&](){
					hoverbox->render();
				});
			}
			
			if (renamebox->parent == this) {
				App::runWithSKIZ(renamebox->t_x, renamebox->t_y, renamebox->t_w, renamebox->t_h, [&](){
					renamebox->render();
				});
			}
		});
	}
	
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
}

void CodeEdit::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	POS_FUNC(this);
	
	if (FILE_BROKEN_STATE) {
		// ensure that the broken state dialog is shown
		if (broken_state_menu->parent == nullptr) {
			App::MoveWidget(broken_state_menu, this);
		}
		broken_state_menu->position(t_x, t_y, t_w, t_h);
		return;
	}else if (REQUESTING_FIXIT) {
		if (fixit_request_menu->parent == nullptr) {
			App::MoveWidget(fixit_request_menu, this);
		}
		fixit_request_menu->position(t_x, t_y, t_w, t_h);
		return;
	}
	
	if (find_menu_open) {
		allButton->position(t_x, t_y, t_w, t_h);
		nextReplButton->position(t_x, t_y, t_w, t_h);
		replaceTextEdit->position(t_x, t_y, t_w, t_h);
		nextButton->position(t_x, t_y, t_w, t_h);
		prevButton->position(t_x, t_y, t_w, t_h);
		findTextEdit->position(t_x, t_y, t_w, t_h);
		caseSensitivity->position(t_x, t_y, t_w, t_h);
	}
	
	line_numbers->position(t_x, t_y, t_w, t_h);
	textedit->position(t_x, t_y, t_w, t_h);
	if (showErrorsButton->parent == this) {
		showErrorsButton->position(t_x, t_y, t_w, t_h);
	}
	if (hoverbox->parent == this) {
		hoverbox->position(t_x, t_y, t_w, t_h);
	}
	completionbox->position(t_x, t_y, t_w, t_h);
	errorMenu->position(t_x, t_y, t_w, t_h);
	
	if (App::activeLeafNode != renamebox && renamebox->parent == this) {
		App::RemoveWidgetFromParent(renamebox);
		App::setActiveLeafNode(textedit);
	}
	if (renamebox->parent == this) {
		renamebox->position(t_x, t_y, t_w, t_h);
	}
}

void CodeEdit::triggerSaveAs() {
	std::string default_path = "";
	if (file) {
		default_path = file->filepath;
	}
	
	const char * fp = tinyfd_saveFileDialog(
		"Save as?", // dialog title
		default_path.c_str(), // default path and filename
		0, NULL, // filter count and filters
		0 // allow multiple selections (0 = no)
	);
	
	if (fp) {
		std::string filePath(fp);
		
		std::filesystem::path fullPath = filePath;
		std::string filename = fullPath.filename().string();
		
		FileInfo *f = new FileInfo();
		f->filepath = filePath;
		f->filename = filename;
		
		FUPDATER(this, f);
		
		file = f;
		
		std::error_code ec;
		last_file_mod_time = std::filesystem::last_write_time(file->filepath, ec); // a hack to think we edited the file instead of whatever happened to it.
		
		detectLanguage();
		
		was_in_a_file = false;
		f->is_opening = false;
		madeChangeBetweenSaves = true;
		save();
		
		if (App::lsp_client_map[lsp]){
			std::string str;
			textedit->getFullText().toUTF8String(str);
			App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
		}
	}
	
	App::commandUnfocused();
}

void CodeEdit::overwrite_file() {
	FILE_BROKEN_STATE = false;
	App::RemoveWidgetFromParent(broken_state_menu);
	
	std::error_code ec;
	last_file_mod_time = std::filesystem::last_write_time(file->filepath, ec); // a hack to think we edited the file instead of whatever happened to it.
	madeChangeBetweenSaves = true;
	was_in_a_file = false;
	save();
}

void CodeEdit::reload_file() {
	FILE_BROKEN_STATE = false;
	App::RemoveWidgetFromParent(broken_state_menu);
	
	openFile();
}

void CodeEdit::save() {
	std::lock_guard<std::mutex> lock(saving_lock);
	
	if (file && !file->is_opening) {
		std::string filepath = file->filepath;
		
		std::error_code ec;
		auto current = std::filesystem::last_write_time(file->filepath, ec);
		
		if (!ec) {
			was_in_a_file = true;
			
			if (current != last_file_mod_time) { // some other process edited the file since we touched it
				FILE_BROKEN_STATE = true;
				return;
			}else if (!madeChangeBetweenSaves) { // we did not make any changes to the file - return early
				FILE_BROKEN_STATE = false;
				if (broken_state_menu->parent == this) {
					App::RemoveWidgetFromParent(broken_state_menu);
				}
				return;
			}
		}else if (was_in_a_file) { // there's an error and we were in the file - file must have been deleted. Clever me.
			// the file did exist, but no longer does
			FILE_BROKEN_STATE = true;
			return;
		}
		
		FILE_BROKEN_STATE = false;
		if (broken_state_menu->parent == this) {
			App::RemoveWidgetFromParent(broken_state_menu);
		}
		
		icu::UnicodeString content = textedit->getFullText();
		std::string str;
		content.toUTF8String(str);
		
		{
			std::string err;
			std::filesystem::path toPath = filepath; // ensure includes <filesystem>
		
			if (!atomicWriteReplace(toPath, str, &err)) {
				std::cerr << "Atomic save failed for " << toPath << ": " << err << "\n";
				return;
			}
			last_file_mod_time = std::filesystem::last_write_time(file->filepath, ec);
			madeChangeBetweenSaves = false;
		}
		
		was_in_a_file = true;
		
		
		if (App::lsp_client_map[lsp]) {
			App::lsp_client_map[lsp]->documentSaved(file->filepath, str);
		}
	}
	
	Widget::save();
}

void CodeEdit::replaceAll(icu::UnicodeString tofind, icu::UnicodeString toreplace, bool case_sensitive) {
	if (tofind == "") {
		return;
	}
	
	if (!case_sensitive) {
		tofind = tofind.toLower();
	}
	
	for (int l = 0; l < textedit->lines.size(); l ++) {
		auto line_searcher = textedit->lines[l].line_text;
		auto true_line = textedit->lines[l].line_text;
		
		int intitiallen = true_line.length();
		
		int start = line_searcher.length();
		
		if (!case_sensitive) {
			line_searcher = line_searcher.toLower();
		}
		
		bool changed = false;
		while (true) {
			int index = line_searcher.lastIndexOf(tofind, 0, start);
			
			if (index == -1) {
				break;
			}
			changed = true;
			
			line_searcher.replaceBetween(index, index+tofind.length(), toreplace); // we must keep them in sync
			true_line.replaceBetween(index, index+tofind.length(), toreplace);
			
			start = index;
		}
		
		if (changed) {
			Cursor c = Cursor();
			c.head_char = 0;
			c.head_line = l;
			c.anchor_char = intitiallen;
			c.anchor_line = l;
			textedit->insertTextAtCursor(c, true_line);
		}
	}
}

void CodeEdit::activateReplace(bool forwards, icu::UnicodeString tofind, icu::UnicodeString toreplace, bool case_sensitive) {
	if (tofind == "") {
		return;
	}
	
	auto has = textedit->getSelectedText(textedit->cursors[0]);
	if (!case_sensitive){
		tofind = tofind.toLower();
		has = has.toLower();
	}
	
	if (has == tofind) {
		auto slelscec = textedit->_getCursSelec(textedit->cursors[0]);
		int start_char = slelscec.second.first;
		int start_line = slelscec.first.first;
		
		textedit->cursors = {textedit->cursors[0]}; // eliminate all other cursors
		textedit->applyInsertToAllCursors(toreplace);
		
		
		textedit->cursors[0].anchor_char = start_char;
		textedit->cursors[0].anchor_line = start_line;
	}
	
	activateFind(forwards, tofind, case_sensitive);
}

void CodeEdit::activateFind(bool forwards, icu::UnicodeString tofind, bool case_sensitive) {
	if (tofind == "") {
		return;
	}
	
	if (!case_sensitive) {
		tofind = tofind.toLower();
	}
	
	int cur_line;
	auto slelscec = textedit->_getCursSelec(textedit->cursors[0]);
	
	auto lines = splitByChar(tofind, '\n');
	
	if (lines.size() > 1) {
		cur_line = slelscec.first.first;
		
		for (int _ = 0; _ < textedit->lines.size()+1; _++) {
			if (forwards) {
				cur_line++;
				if (cur_line >= textedit->lines.size()) {
					cur_line = 0;
				}
			}else{
				cur_line--;
				if (cur_line < 0) {
					cur_line = textedit->lines.size()-1;
				}
			}
			
			auto text = textedit->lines[cur_line].line_text;
			if (!case_sensitive) {
				text = text.toLower();
			}
			
			if (text.endsWith(lines[0])) {
				bool matches = true;
				for (int li = 1; li < lines.size()-1; li++) {
					int idx = cur_line+li;
					if (idx >= textedit->lines.size()) {
						matches = false;
						break;
					}
					
					auto t2 = textedit->lines[idx].line_text;
					if (!case_sensitive) {
						t2 = t2.toLower();
					}
					
					if (t2 != lines[li]) {
						matches = false;
						break;
					}
				}
				if (matches) {
					int idx = cur_line + lines.size() - 1;
					
					auto t2 = textedit->lines[idx].line_text;
					if (!case_sensitive) {
						t2 = t2.toLower();
					}
					
					if (t2.startsWith(lines[lines.size()-1])) {
						// we've found a full match
						Cursor c;
						c.anchor_line = cur_line;
						c.anchor_char = text.length()-lines[0].length();
						c.head_line = idx;
						c.head_char = lines[lines.size()-1].length();
						c.preffered_collumn = c.head_char;
						textedit->cursors = {c};
						textedit->ensureCursorVisible(textedit->cursors[0]);
						return;
					}
				}
			}
		}
	}else{
		int start_char;
		
		if (forwards){
			cur_line = slelscec.first.second;
			start_char = slelscec.second.second;
		}else{
			cur_line = slelscec.first.first;
			start_char = slelscec.second.first;
		}
		
		for (int _ = 0; _ < textedit->lines.size()+1; _++) {
			int index;
			
			auto text = textedit->lines[cur_line].line_text;
			if (!case_sensitive) {
				text = text.toLower();
			}
			
			if (forwards) {
				index = text.indexOf(tofind, start_char);
			}else{
				index = text.lastIndexOf(tofind, 0, start_char);
			}
			
			if ( index != -1 ) {
				Cursor c;
				c.anchor_line = cur_line;
				c.anchor_char = index;
				c.head_line = cur_line;
				c.head_char = index+tofind.length();
				c.preffered_collumn = c.head_char;
				textedit->cursors = {c};
				textedit->ensureCursorVisible(textedit->cursors[0]);
				return;
			}
			
			if (forwards) {
				start_char = 0;
				
				cur_line++;
				if (cur_line >= textedit->lines.size()) {
					cur_line = 0;
				}
			}else{
				cur_line--;
				if (cur_line < 0) {
					cur_line = textedit->lines.size()-1;
				}
				
				start_char = textedit->lines[cur_line].line_text.length();
			}
		}
	}
}

bool CodeEdit::on_char_event(unsigned int keycode) {
	if (!is_visible) {
		return false;
	}
	
	if (FILE_BROKEN_STATE) { return broken_state_menu->on_char_event(keycode); }
	if (REQUESTING_FIXIT) { return fixit_request_menu->on_char_event(keycode); }
	
	if (App::activeLeafNode != hoverbox) {
		if (hoverbox->parent == this) {
			App::RemoveWidgetFromParent(hoverbox);
		}
	}
	
	if (Widget::on_char_event(keycode)) {
		if (textedit == App::activeLeafNode && textedit->wasmode == 'i' && textedit->cursors.size() == 1) {
			char utf8[5] = {};
			int len = std::snprintf(utf8, sizeof(utf8), "%c", keycode);
			if (len > 0) { // there is something printable
				if (App::lsp_client_map[lsp] && file) { // lsp completion, much faster
					completion_id = App::lsp_client_map[lsp]->requestCompletion(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
				}
				
				if (App::settings->getValue("use_chauffeur", false)) {
					timeuntilchauffeur = App::settings->getValue("chauffeur_time", 1000);
				}
			}
		}
		return true;
	}
	
	return false;
}

std::string CodeEdit::augmentBuildCommand(std::string inital) {
	std::filesystem::path path(file->filepath);
	auto fileloc = path.parent_path();
	// replace all instances of %FILE_LOCATION% with fileloc
	size_t pos = inital.find("%FILE_LOCATION%");
	while (pos != std::string::npos) {
		inital.replace(pos, 15, fileloc.string());
		pos = inital.find("%FILE_LOCATION%");
	}
	
	auto filename = path.filename();
	// replace all instances of %FILE_NAME% with filename
	size_t pos_1 = inital.find("%FILE_NAME%");
	while (pos_1 != std::string::npos) {
		inital.replace(pos_1, 11, filename.string());
		pos_1 = inital.find("%FILE_NAME%");
	}
	
	auto filename_no_ext = path.stem();
	// replace all instances of %FILE_NAME_NO_EXT% with filename without ext
	size_t pos_2 = inital.find("%FILE_NAME_NO_EXT%");
	while (pos_2 != std::string::npos) {
		inital.replace(pos_2, 18, filename_no_ext.string());
		pos_2 = inital.find("%FILE_NAME_NO_EXT%");
	}
	
	return inital;
}

bool CodeEdit::on_key_event(int key, int scancode, int action, int mods) {
	if (FILE_BROKEN_STATE) { return broken_state_menu->on_key_event(key, scancode, action, mods); }
	
	bool shift_held = (mods & GLFW_MOD_SHIFT) != 0;
	bool control_held = (mods & GLFW_MOD_CONTROL) != 0;
	bool alt_held = (mods & GLFW_MOD_ALT) != 0;
	
	bool is_press = (action == GLFW_PRESS || action == GLFW_REPEAT);
	
	Cursor svdCrsr = textedit->cursors[0];
	
	if (parent == App::activeEditor) {
		if (action == GLFW_PRESS && timeuntilchauffeur != App::settings->getValue("chauffeur_time", 1000)) {
			timeuntilchauffeur = 0;
		}
		
		if (key == GLFW_KEY_F5 && is_press && language != "" && file) {
			// at this point we've already tried project build commands
			auto l = App::languagemap[language];
			
			if (l.build_command != "") {
				App::launchCommandNonBlocking(augmentBuildCommand(l.build_command));
				return true;
			}
		}else if (key == GLFW_KEY_A && is_press && alt_held) {
			new std::thread([&](){
				int contextsize = App::settings->getValue("lm_studio_context_lines", 30);
				
				Cursor cur = textedit->cursors[0];
				
				Cursor start = Cursor();
				start.anchor_char = cur.head_char;
				start.anchor_line = cur.head_line;
				start.head_char = 0;
				start.head_line = std::max(0, cur.head_line-contextsize);
				auto t1 = textedit->getSelectedText(start);
				
				std::string befr;
				t1.toUTF8String(befr);
				
				Cursor end = Cursor();
				end.anchor_char = cur.head_char;
				end.anchor_line = cur.head_line;
				end.head_line = std::min((int)textedit->lines.size()-1, cur.head_line+contextsize);
				end.head_char = textedit->lines[end.head_line].line_text.length();
				auto t2 = textedit->getSelectedText(end);
				
				std::string aftr;
				t2.toUTF8String(aftr);
				
				App::displayToast(icu::UnicodeString::fromUTF8("Contacting AI Provider"));
				std::string insertion = Curler::StreamInsertion(befr, aftr, [this](std::string s){
					textedit->insertTextAtCursor(textedit->cursors[0], icu::UnicodeString::fromUTF8(s));
					App::time_till_regular += 2;
				});
			});
			return true;
		}
		
		if (App::activeLeafNode == renamebox && renamebox->parent == this) {
			if (key == GLFW_KEY_ESCAPE) {
				App::RemoveWidgetFromParent(renamebox);
				App::setActiveLeafNode(textedit);
				return true;
			}else if (key == GLFW_KEY_ENTER){
				App::RemoveWidgetFromParent(renamebox);
				if (App::lsp_client_map[lsp]) {
					std::string rename;
					renamebox->getFullText().toUTF8String(rename);
					rename_id = App::lsp_client_map[lsp]->requestRename(file->filepath, renamecursor.head_line, renamecursor.head_char, rename);
				}
				App::setActiveLeafNode(textedit);
				return true;
			}
		}else if (renamebox->parent == this){
			App::RemoveWidgetFromParent(renamebox);
		}
		
		if (App::activeLeafNode != hoverbox) {
			if (hoverbox->parent == this && is_press && key != GLFW_KEY_LEFT_SHIFT && key != GLFW_KEY_RIGHT_SHIFT) {
				App::RemoveWidgetFromParent(hoverbox);
			}
		}
		
		if (App::activeLeafNode == textedit) {
			if (key == GLFW_KEY_ENTER && alt_held && is_press) {
				if (App::lsp_client_map[lsp]) {
					auto slelscec = textedit->_getCursSelec(textedit->cursors[0]);
					code_actions_id = App::lsp_client_map[lsp]->requestActions(file->filepath, slelscec.first.first, slelscec.second.first, slelscec.first.second, slelscec.second.second);
				}
				return true;
			}else if (key == GLFW_KEY_R && (control_held || textedit->mode == 'n') && is_press) {
				if (App::lsp_client_map[lsp]) {
					renamecursor = textedit->cursors[0];
					if (renamebox->parent != this){
						App::MoveWidget(renamebox, this);
						App::setActiveLeafNode(renamebox);
					}
					renamebox->wasmode = 'n';
					renamebox->mode = 'i';
					renamebox->setFullText(icu::UnicodeString());
				}
				return true;
			}
			
			if (completionbox->is_visible_layered && is_press) {
				if ((textedit->mode == 'n' && GLFW_KEY_J == key) || GLFW_KEY_DOWN == key) {
					completionbox->moveDown();
					return true;
				}else if ((textedit->mode == 'n' && GLFW_KEY_K == key) || GLFW_KEY_UP == key) {
					completionbox->moveUp();
					return true;
				}else if (GLFW_KEY_TAB == key && action == GLFW_PRESS) {
					activateCompletion();
					return true;
				}else if (GLFW_KEY_ESCAPE == key && action == GLFW_PRESS && (textedit->mode == 'n' || !App::settings->getValue("use_vim", false))) {
					completionbox->is_visible_layered = false;
					return true;
				}
			}
			
			if (file && key == GLFW_KEY_G && (control_held || (textedit->mode == 'n' && textedit->vim_repeater == 0)) && is_press) {
				if (App::lsp_client_map[lsp]) {
					goto_id = App::lsp_client_map[lsp]->requestGotoDefinition(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
				}
			}if (file && key == GLFW_KEY_P && (control_held || textedit->mode == 'n') && is_press) {
				if (App::lsp_client_map[lsp]){
					hoverCrsr = textedit->cursors[0];
					should_move_mouse_hover = true;
					hover_id = App::lsp_client_map[lsp]->requestHover(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
				}
			}if (key == GLFW_KEY_3 && alt_held && is_press && language != "" && App::languagemap[language].line_comment != "") {
				setComments();
				return true;
			}else if (key == GLFW_KEY_4 && alt_held && is_press && language != "" && App::languagemap[language].line_comment != "") {
				removeComments();
				return true;
			}else if (key == GLFW_KEY_SLASH && control_held && is_press && language != "" && App::languagemap[language].line_comment != "") {
				auto find = App::languagemap[language].line_comment;
	
				Cursor c = textedit->cursors[0];
				auto slelscec = textedit->_getCursSelec(c);
				int l = slelscec.first.first;
				auto line_text = textedit->lines[l].line_text;
				int indx = line_text.indexOf(find);
				if (indx == -1) {
					setComments();
					return true;
				}
				
				for (int z = 0; z < indx; z++) {
					if (!whitespace_before_comment.count(line_text.char32At(z))){
						setComments();
						return true;
					}
				}
				
				removeComments();
				return true;
			}
		}
		
		if (key == GLFW_KEY_S && is_press && shift_held && control_held){
			triggerSaveAs();
			return true;
		}else if (key == GLFW_KEY_S && is_press && !shift_held && control_held && !file){
			triggerSaveAs();
			return true;
		}else if (key == GLFW_KEY_F && is_press && (control_held || (App::activeLeafNode == textedit && textedit->mode == 'n'))) {
			// open find menu and whatnot
			find_menu_open = true;
			
			App::MoveWidget(replaceTextEdit, this); // move them back to be children.
			App::MoveWidget(findTextEdit, this);
			App::MoveWidget(caseSensitivity, this);
			App::MoveWidget(prevButton, this);
			App::MoveWidget(nextButton, this);
			App::MoveWidget(nextReplButton, this);
			App::MoveWidget(allButton, this);
			
			auto t = textedit->getSelectedText(textedit->cursors[0]);
			if (t != "") {
				findTextEdit->setFullText(t);
			}
			
			Cursor newcursor; // select all text
			newcursor.anchor_char = 0;
			newcursor.anchor_line = 0;
			newcursor.head_line = findTextEdit->lines.size()-1;
			newcursor.head_char = findTextEdit->lines[newcursor.head_line].line_text.length();
			newcursor.preffered_collumn = newcursor.head_char;
			
			findTextEdit->cursors = { newcursor };
			findTextEdit->wasmode = 'n'; // this is a dirty hack because otherwise we sometimes end up sending a 'f' key to the findtextedit. This way we can ignore it in the char callback
			findTextEdit->mode = 'i';
			replaceTextEdit->mode = 'i';
			
			App::setActiveLeafNode(findTextEdit);
			return true;
		}
		
		if (findTextEdit == App::activeLeafNode || replaceTextEdit == App::activeLeafNode) {
			bool in_normal = false;
			if (auto te = dynamic_cast<TextEdit*>(App::activeLeafNode)) {
				in_normal = (te->mode == 'n');
			}
			
			if (key == GLFW_KEY_ENTER && is_press && !control_held) {
				bool forwards = !shift_held;
				bool case_sensitive = caseSensitivity->is_checked;
				
				if (findTextEdit == App::activeLeafNode) {
					activateFind(forwards, findTextEdit->getFullText(), case_sensitive);
				}else{ // replace
					activateReplace(forwards, findTextEdit->getFullText(), replaceTextEdit->getFullText(), case_sensitive);
				}
				
				return true;
			}else if (key == GLFW_KEY_TAB && is_press && shift_held) {
				if (findTextEdit == App::activeLeafNode) {
					App::setActiveLeafNode(replaceTextEdit);
				}else{
					App::setActiveLeafNode(findTextEdit);
				}
				return true;
			}
			
			if (key == GLFW_KEY_ESCAPE && is_press && in_normal) {
				find_menu_open = false;
				App::RemoveWidgetFromParent(replaceTextEdit);
				App::RemoveWidgetFromParent(findTextEdit);
				App::RemoveWidgetFromParent(caseSensitivity);
				App::RemoveWidgetFromParent(prevButton);
				App::RemoveWidgetFromParent(nextButton);
				App::RemoveWidgetFromParent(nextReplButton);
				App::RemoveWidgetFromParent(allButton);
				
				App::setActiveLeafNode(textedit);
				return true;
			}
		}
	}
	
	if (FILE_BROKEN_STATE){
		return broken_state_menu->on_key_event(key, scancode, action, mods);
	}else if (REQUESTING_FIXIT){
		return fixit_request_menu->on_key_event(key, scancode, action, mods);
	}else{
		bool wrkd = Widget::on_key_event(key, scancode, action, mods);
		
		Cursor c = textedit->cursors[0];
		
		if (textedit->cursors.size() > 1 || (svdCrsr.head_char != c.head_char || svdCrsr.head_line != c.head_line || svdCrsr.anchor_char != c.anchor_char || svdCrsr.anchor_line != c.anchor_line)) {
			completionbox->is_visible_layered = false;
		}
		
		return wrkd;
	}
}

void CodeEdit::setComments() {
	for (auto c : textedit->cursors) {
		auto slelscec = textedit->_getCursSelec(c);
		
		for (auto l = slelscec.first.first; l < slelscec.first.second+1; l++) {
			Cursor comc = Cursor();
			comc.head_char = 0;
			comc.anchor_char = 0;
			comc.head_line = l;
			comc.anchor_line = l;
			textedit->insertTextAtCursor(comc, icu::UnicodeString( App::languagemap[language].line_comment ));
		}
	}
	textedit->tryingToEnsureCursorPos = true;
}

void CodeEdit::removeComments() {
	auto remove = App::languagemap[language].line_comment;
	
	for (auto c : textedit->cursors) {
		auto slelscec = textedit->_getCursSelec(c);
		
		for (auto l = slelscec.first.first; l < slelscec.first.second+1; l++) {
			auto line_text = textedit->lines[l].line_text;
			
			int indx = line_text.indexOf(remove);
			if (indx == -1) {
				continue;
			}
			
			bool works = true;
			
			for (int z = 0; z < indx; z++) {
				if (!whitespace_before_comment.count(line_text.char32At(z))){
					works = false;
					break;
				}
			}
			
			if (!works) {
				continue;
			}
			
			Cursor comc = Cursor();
			comc.head_char = indx;
			comc.anchor_char = indx+remove.length();
			
			if (indx+remove.length() < line_text.length()) {
				if (line_text.char32At(indx+remove.length()) == U' ') {
					comc.anchor_char ++;
				}
			}
			
			comc.head_line = l;
			comc.anchor_line = l;
			textedit->deleteTextAtCursor(comc, GLFW_KEY_BACKSPACE, false);
		}
	}
	textedit->tryingToEnsureCursorPos = true;
}

bool CodeEdit::on_mouse_button_event(int button, int action, int mods) {
	if (FILE_BROKEN_STATE) {
		return broken_state_menu->on_mouse_button_event(button, action, mods);
	}else if (REQUESTING_FIXIT) {
		return fixit_request_menu->on_mouse_button_event(button, action, mods);
	}else{
		int mx = App::mouseX;
		int my = App::mouseY;
		
		if (hoveringHoverbox(mx, my)) {
			completionbox->is_visible_layered = false;
			return hoverbox->on_mouse_button_event(button, action, mods);
		}
		if (hoveringCompletionBox(mx, my)) {
			return completionbox->on_mouse_button_event(button, action, mods);
		}else{
			completionbox->is_visible_layered = false;
		}
		
		if (showErrorsButton->on_mouse_button_event(button, action, mods)) {return true;} // this doesn't get first dibs because it's after the textedit in the children list
		if (errorMenu->is_visible_layered && errorMenu->on_mouse_button_event(button, action, mods)) {return true;} // this doesn't get first dibs because it's after the textedit in the children list
		
		return Widget::on_mouse_button_event(button, action, mods);
	}
}

bool CodeEdit::hoveringHoverbox(int mx, int my, int padding) {
	if (hoverbox->parent == this) {
		if (mx >= hoverbox->t_x-padding && mx <= hoverbox->t_x+hoverbox->t_w+padding && my <= hoverbox->t_y+hoverbox->t_h+padding && my >= hoverbox->t_y-padding) {
			return true;
		}
	}
	return false;
}

bool CodeEdit::hoveringCompletionBox(int mx, int my, int padding) {
	if (completionbox->parent == this) {
		if (mx >= completionbox->t_x-padding && mx <= completionbox->t_x+completionbox->t_w+padding && my <= completionbox->t_y+completionbox->t_h+padding && my >= completionbox->t_y-padding) {
			return true;
		}
	}
	return false;
}

bool CodeEdit::on_mouse_move_event() {
	if (FILE_BROKEN_STATE) {
		return broken_state_menu->on_mouse_move_event();
	}else if (REQUESTING_FIXIT) {
		return fixit_request_menu->on_mouse_move_event();
	}else{
		int mx = App::mouseX;
		int my = App::mouseY;
		
		bool gottoit = false;
		Cursor crsr = textedit->getCursorForMousePosition(mx, my, &gottoit);
		
		if (hoverbox->parent == this && hoveringHoverbox(mx, my)) {
			return hoverbox->on_mouse_move_event();
		}
		
		if (textedit->contextmenu->is_visible_2) {
			gottoit = false;
		}
		
		if (gottoit) {
			for (auto d : textedit->lines[crsr.head_line].diagnostics) {
				if (d.sc-1 <= crsr.head_char && d.ec+1 >= crsr.head_char) { // introduce some leeway (or however it's spelt. Sound it out)
					if (hoverbox->parent != this) {
						App::MoveWidget(hoverbox, this);
					}
					
					hoverbox->setFullText(d.message);
					hoverbox->scrolled_to_vert = 0;
					hoverbox->scrolled_to_horz = 0;
					hoverCrsr = crsr;
					
					for (auto w : children) {
						if (w != hoverbox && w->on_mouse_move_event()) {
							return true;
						}
					}
					
					return true;
				}
			}
			
			last_mouse_x = mx;
			last_mouse_y = my;
			
			timeuntil = 400;
		}
		
		if (hoverbox->parent == this && !hoveringHoverbox(mx, my, TextRenderer::get_text_height())) {
			App::RemoveWidgetFromParent(hoverbox);
		}
		
		for (auto w : children) {
			if (w != hoverbox && w->on_mouse_move_event()) {
				return true;
			}
		}
		return false;
	}
}

bool CodeEdit::on_scroll_event(double xchange, double ychange) {
	int mx = App::mouseX;
	int my = App::mouseY;
	
	if (FILE_BROKEN_STATE) {
		return broken_state_menu->on_scroll_event(xchange, ychange);
	}else{
		if (hoveringHoverbox(mx, my)) {
			return hoverbox->on_scroll_event(xchange, ychange);
		}else if (errorMenu->is_visible_layered && errorMenu->t_x <= mx && errorMenu->t_x+errorMenu->t_w >= mx && errorMenu->t_y <= my && errorMenu->t_y+errorMenu->t_h >= my){
			return errorMenu->on_scroll_event(xchange, ychange);
		}else{
			return Widget::on_scroll_event(xchange, ychange);
		}
	}
}

void CodeEdit::actionsReceived(int id, json resp) {
	if (id != code_actions_id) {
		return;
	}
	
	code_actions = resp;
	
	std::vector<icu::UnicodeString> els;
	
	for (auto j : code_actions) {
		std::string tstr = j["title"];
		icu::UnicodeString title = icu::UnicodeString::fromUTF8(tstr);
		els.push_back(title);
	}
	
	if (els.empty()) {
		completionbox->is_visible_layered = false;
		return;
	}
	
	completionbox->is_visible_layered = true;
	completionbox->setElements(els);
	if (els.size() >= 7) {
		completionbox->toshow = 7;
	}else if (els.size() > 0){
		completionbox->toshow = els.size();
	}
	App::time_till_regular = 2;
	
	are_code_actions = true;
}

void CodeEdit::completionRecieved(std::vector<std::string> completions, int rec_id) {
	std::lock_guard<std::mutex> lock(App::canMakeChanges);
	
	if (rec_id != completion_id) { return; }
	
	if (textedit->cursors.size() > 1) {
		completionbox->is_visible_layered = false;
		return;
	}
	
	icu::UnicodeString wrd = textedit->getCurrentWord(textedit->lines[textedit->cursors[0].head_line].line_text, textedit->cursors[0].head_char);
	wrd.toLower();
	
	std::vector<icu::UnicodeString> compld;
	for (auto c : completions) {
		auto icustr = icu::UnicodeString::fromUTF8(c);
		auto lwrd = icustr;
		lwrd.toLower();
		
		if (lwrd.startsWith(wrd)){
			compld.push_back(icustr);
		}
	}
	
	if (compld.size() == 0) {
		completionbox->is_visible_layered = false;
		return;
	}
	
	completionbox->is_visible_layered = true;
	completionbox->setElements(compld);
	are_code_actions = false;
	is_chauffeur = false;
	
	if (compld.size() >= 7) {
		completionbox->toshow = 7;
	}else if (compld.size() > 0){
		completionbox->toshow = compld.size();
	}
	
	App::time_till_regular = 2;
}

void CodeEdit::renameReceived(int id, json resp) {
	if (id != rename_id) {
		return;
	}
	
	auto edits = parseCommandArguments(resp);
	
	applyOtherFileEdits(edits, file->filepath);
	
	auto sections = gatherCurrentFileSections(edits, file->filepath);
	
	for (auto& s : sections) {
		Cursor c = Cursor();
		c.anchor_char = s.startChar;
		c.anchor_line = s.startLine;
		c.head_char = s.endChar;
		c.head_line = s.endLine;
		
		if (c.head_line > textedit->lines.size()-1 || c.anchor_line > textedit->lines.size()-1) {
			std::cerr << "Had to skip one outside of line bounds\n";
			continue;
		}
		
		std::cout << "Replacing [" << s.startLine << ":" << s.startChar << " → " << s.endLine   << ":" << s.endChar << "] with: " << s.newText << "\n";
		
		textedit->insertTextAtCursor(c, icu::UnicodeString::fromUTF8(s.newText));
	}
	
	Cursor c = textedit->cursors[0];
	if (c.head_line >= textedit->lines.size()) {
		c.head_line = textedit->lines.size()-1;
	}
	if (c.head_char > textedit->lines[c.head_line].line_text.length()) {
		c.head_char = textedit->lines[c.head_line].line_text.length();
	}
	c.preffered_collumn = textedit->_mapFromRealToVisual(c.head_line, c.head_char);
	c.anchor_char = c.head_char;
	c.anchor_line = c.head_line;
	
	textedit->cursors = {c};
	
	return;
}

void CodeEdit::activateCompletion() {
	if (completionbox->elements.size() == 0 || textedit->cursors.size() > 1) {
		return;
	}
	
	icu::UnicodeString selected = completionbox->elements[completionbox->selected_id];
	
	if (are_code_actions) {
		auto edits = parseCodeAction(code_actions[completionbox->selected_id]);
		
		applyOtherFileEdits(edits, file->filepath);
		
		auto sections = gatherCurrentFileSections(edits, file->filepath);
		
		for (auto& s : sections) {
			Cursor c = Cursor();
			c.anchor_char = s.startChar;
			c.anchor_line = s.startLine;
			c.head_char = s.endChar;
			c.head_line = s.endLine;
			
			std::cerr << "Replacing [" << s.startLine << ":" << s.startChar << " → " << s.endLine   << ":" << s.endChar << "] with: " << s.newText << "\n";
			
			textedit->insertTextAtCursor(c, icu::UnicodeString::fromUTF8(s.newText));
		}
		
		Cursor c = textedit->cursors[0];
		if (c.head_line >= textedit->lines.size()) {
			c.head_line = textedit->lines.size()-1;
		}
		if (c.head_char > textedit->lines[c.head_line].line_text.length()) {
			c.head_char = textedit->lines[c.head_line].line_text.length();
		}
		c.preffered_collumn = textedit->_mapFromRealToVisual(c.head_line, c.head_char);
		c.anchor_char = c.head_char;
		c.anchor_line = c.head_line;
		
		textedit->cursors = {c};
		
		return;
	}else if (is_chauffeur) {
		textedit->applyInsertToAllCursors(selected);
		timeuntilchauffeur = 1; // triggers a new chauffeur run
		return;
	}
	
	auto wrd = textedit->getCurrentWord(textedit->lines[textedit->cursors[0].head_line].line_text, textedit->cursors[0].head_char);
	
	textedit->cursors[0].anchor_char = textedit->cursors[0].head_char-wrd.length();
	textedit->cursors[0].anchor_line = textedit->cursors[0].head_line;
	
	int startchar = textedit->cursors[0].anchor_char;
	
	UErrorCode status = U_ZERO_ERROR;
	icu::RegexPattern* pattern = icu::RegexPattern::compile(uR"(\$(?:\d+|\{\d+:[^}]*\}))", 0, status);
	if (U_FAILURE(status)) {
		std::cerr << "Pattern compilation failed: " << u_errorName(status) << std::endl;
		return;
	}

	// Create a matcher
	icu::RegexMatcher* matcher = pattern->matcher(selected, status);
	if (U_FAILURE(status)) {
		std::cerr << "Matcher creation failed: " << u_errorName(status) << std::endl;
		delete pattern;
	}
	
	 // Try to find the first match
	int32_t firstIndex = -1;
	if (matcher->find()) {
		firstIndex = matcher->start(status);
		if (U_FAILURE(status)) {
			std::cerr << "Error getting match start: " << u_errorName(status) << std::endl;
			firstIndex = -1;
		}
	}

	// Reset matcher to start again from the beginning
	matcher->reset();
	
	selected = matcher->replaceAll(u"", status);
	
	textedit->applyInsertToAllCursors(selected);
	
	if (firstIndex != -1) {
		textedit->cursors[0].head_char = startchar+firstIndex;
		textedit->cursors[0].anchor_char = startchar+firstIndex;
		textedit->cursors[0].preffered_collumn = startchar+firstIndex;
	}
}

void CodeEdit::publishDiagnostics(std::string filename, std::vector<std::string> messages, std::vector<int> startC, std::vector<int> startL, std::vector<int> endC, std::vector<int> endL, std::vector<int> severities) {
	std::lock_guard<std::mutex> lock(App::canMakeChanges);
	
	if (!App::lsp_client_map[lsp] || !file || !URISEqual(filename, App::lsp_client_map[lsp]->fromLocalFile(file->filepath))) {
		return;
	}
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		textedit->lines[i].diagnostics.clear();
	}
	
	std::vector<icu::UnicodeString> errorsAsUnicode;
	errorlines.clear();
	
	double max_line = textedit->lines.size() + textedit->t_h / TextRenderer::get_text_height();
	std::vector<double> red;
	std::vector<double> orange;
	std::vector<double> blue;
	
	for (int i = 0; i < messages.size(); i++) {
		int sl = startL[i];
		int sc = startC[i];
		int el = endL[i];
		int ec = endC[i];
		int sev = severities[i]-1; // so glad I included comments last time... yeah right
		
		if (sl > textedit->lines.size()-1) { // this can happen, because we are always putting a \n on each line we send to the LSP. It is **always** wrong. And I don't care enough to fix it right.
			continue;
		}
		
		if (sev == 3) { sev = 2; }
		
		if (sc == ec && sl == el) {
			ec += 1;
		}
		
		if (sev == 0) {
			red.push_back(sl/max_line);
		}else if (sev == 1) {
			orange.push_back(sl/max_line);
		}else if (sev == 2) {
			blue.push_back(sl/max_line);
		}
		
		icu::UnicodeString mes = icu::UnicodeString::fromUTF8(messages[i]);
		errorsAsUnicode.push_back(icu::UnicodeString::fromUTF8(std::to_string(sl+1)+" - ")+mes);
		errorlines.push_back(sl);
		
		for (int l = sl; l < el+1; l++){
			if (l > textedit->lines.size()-1) {
				break; // again this can happen
			}
			
			int srt = 0;
			int end = textedit->lines[l].line_text.length()-1;
			
			if (l == sl) {
				srt = sc;
			}
			if (l == el) {
				end = ec;
			}
			
			textedit->lines[l].diagnostics.push_back({ mes, srt, end, sev });
		}
	}
	
	textedit->scrollbar_v->setErrors(red, orange, blue);
	
	// error box bottom right
	
	errorMenu->setElements(errorsAsUnicode);
	
	if (errorsAsUnicode.empty()){
		errorMenu->is_visible_layered = false;
		
		if (showErrorsButton->parent == this) {
			App::RemoveWidgetFromParent(showErrorsButton);
		}
	}else{
		if (showErrorsButton->parent != this) {
			App::MoveWidget(showErrorsButton, this);
		}
		
		if (errorsAsUnicode.size() >= 13) {
			errorMenu->toshow = 13;
		}else {
			errorMenu->toshow = errorsAsUnicode.size();
		}
	}
	
	// ensure rendering even w/ no user input
	
	App::time_till_regular = 2;
}

void CodeEdit::gotoDef(int id, int line1, int character1, int line, int character, std::string locURI) {
	std::lock_guard<std::mutex> lock(App::canMakeChanges);
	
	if (goto_id != id) {
		return;
	}
	
	std::string pointing_to = fileUriToPath(locURI);
	
	if (file && areSameFile(pointing_to, file->filepath)) {
		if (textedit->lines.size() <= line || textedit->lines.size() <= line1) {
			return;
		}
		
		if (textedit->lines[line].line_text.length() < character || textedit->lines[line1].line_text.length() < character1) {
			return;
		}
		
		textedit->cursors = { { line1, character1, line, character, character } };
		textedit->tryingToEnsureCursorPos = true;
	}else {
		if (auto edtr = dynamic_cast<Editor*>(parent)) {
			std::filesystem::path fullPath = pointing_to;
			std::string filename = fullPath.filename().string();
			
			FileInfo* finfo = new FileInfo();
			finfo->filepath = pointing_to;
			finfo->filename = filename;
			
			edtr->fileOpenRequested(finfo, line1, character1, line, character);
		}
	}
	
	App::time_till_regular = 5; // just to really drive the point home
}

void CodeEdit::hoverRecieved(std::string content, std::string type, int id) {
	if (id != hover_id || textedit->contextmenu->is_visible_2) {
		return;
	}
	
	std::lock_guard<std::mutex> lock(App::canMakeChanges);
	
	if (hoverbox->parent != this) {
		App::MoveWidget(hoverbox, this);
	}
	
	hoverbox->setFullText(icu::UnicodeString::fromUTF8(content));
	hoverbox->scrolled_to_vert = 0;
	hoverbox->scrolled_to_horz = 0;
	
	if (should_move_mouse_hover){
		hoverbox->position(t_x, t_y, t_w, t_h);
		App::moveMouse(hoverbox->t_x+hoverbox->t_w/2, hoverbox->t_y+hoverbox->t_h/2);
	}
}

void CodeEdit::onTextChanged(Widget* w) {
	madeChangeBetweenSaves = true;
	
	auto te = dynamic_cast<TextEdit*>(w);
	if (!te) {return;}
	
	if (hoverbox->parent == this) {
		App::RemoveWidgetFromParent(hoverbox);
		hoverCrsr = Cursor();
	}
	
	if (!App::lsp_client_map[lsp] || !file || file->filepath == "") {
		return;
	}
	
	std::string text;
	te->getFullText().toUTF8String(text);
	
	App::lsp_client_map[lsp]->updateDocument(file->filepath, text);
}

std::vector<FileEdit> CodeEdit::parseCodeAction(const json& action) {
	// If it came back as a “command” with arguments… 
	if (action.contains("arguments") && action["arguments"].is_array() && !action["arguments"].empty() && action["arguments"][0].contains("changes")) {
		return parseCommandArguments(action["arguments"][0]);
	}
	// Otherwise if it has an embedded WorkspaceEdit…
	if (action.contains("edit") && action["edit"].contains("documentChanges")){
		return parseWorkspaceEdits(action["edit"]["documentChanges"]);
	}
	return {}; 
}

std::vector<FileEdit> CodeEdit::parseCommandArguments(const json& changesObj) {
	std::vector<FileEdit> out;
	
	if (changesObj.contains("changes")){
		auto tochange = changesObj["changes"];
		for (auto& [uri, editsArr] : tochange.items()) {
			FileEdit fe;
			fe.uri = uri;
			for (auto& ev : editsArr) {
				EditDoc te;
				auto r = ev["range"];
				te.range.start = { r["start"]["line"], r["start"]["character"] };
				te.range.end   = { r["end"  ]["line"], r["end"  ]["character"] };
				te.newText     = ev.value("newText", "");
				fe.edits.push_back(std::move(te));
			}
			out.push_back(std::move(fe));
		}
	}else{
		auto tochange = changesObj["documentChanges"];
		
		for (auto& change : tochange) {
			std::string uri = change["textDocument"]["uri"];
			
			FileEdit fe;
			fe.uri = uri;
			
			for (auto edit : change["edits"]) {
				EditDoc te;
				auto r = edit["range"];
				te.range.start = { r["start"]["line"], r["start"]["character"] };
				te.range.end   = { r["end"  ]["line"], r["end"  ]["character"] };
				te.newText     = edit.value("newText", "");
				fe.edits.push_back(std::move(te));
			}
			
			out.push_back(std::move(fe));
		}
	}
	
	return out;
}

std::vector<FileEdit> CodeEdit::parseWorkspaceEdits(const json& docChangesArr) {
	std::vector<FileEdit> out;
	for (auto& dc : docChangesArr) {
		FileEdit fe;
		fe.uri = dc["textDocument"]["uri"];
		for (auto& ev : dc["edits"]) {
			EditDoc te;
			auto r = ev["range"];
			te.range.start = { r["start"]["line"], r["start"]["character"] };
			te.range.end   = { r["end"  ]["line"], r["end"  ]["character"] };
			te.newText     = ev.value("newText", "");
			fe.edits.push_back(std::move(te));
		}
		out.push_back(std::move(fe));
	}
	return out;
}

std::vector<std::string> CodeEdit::splitLines(const std::string& s) {
	std::vector<std::string> out;
	std::string line;
	for (char c : s) {
		if (c == '\n') {
			out.push_back(line);
			line.clear();
		} else {
			line.push_back(c);
		}
	}
	out.push_back(line);
	return out;
}

void CodeEdit::applyEditToLines(std::vector<std::string>& lines, const EditDoc& te) {
	auto& st = te.range.start;
	auto& en = te.range.end;

	if (st.line == en.line) {
		// single‐line replace
		auto& L = lines[st.line];
		L.replace(st.character,
				  en.character - st.character,
				  te.newText);
	} else {
		// multi‐line: grab prefix, suffix, splice in newText lines
		std::string prefix = lines[st.line].substr(0, st.character);
		std::string suffix = lines[en.line].substr(en.character);
		auto newLines = splitLines(te.newText);

		// build merged block
		std::vector<std::string> block;
		block.reserve(newLines.size());
		block.push_back(prefix + newLines.front());
		for (size_t i = 1; i+1 < newLines.size(); ++i)
			block.push_back(newLines[i]);
		block.push_back(newLines.back() + suffix);

		// erase old lines and insert block
		lines.erase(lines.begin() + st.line,
					lines.begin() + en.line + 1);
		lines.insert(lines.begin() + st.line,
					 block.begin(), block.end());
	}
}

void CodeEdit::applyOtherFileEdits(const std::vector<FileEdit>& edits, const std::string& currentFilePath) {
	for (auto& fe : edits) {
		if (URISEqual(fe.uri, App::lsp_client_map[lsp]->fromLocalFile(currentFilePath)))
			continue;
		
		std::filesystem::path p = std::filesystem::path(fileUriToPath(fe.uri));
		
		// read into lines
		std::ifstream in(p);
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(in, line))
			lines.push_back(line);
		in.close();
	
		// sort edits *descending* so earlier edits don’t shift later ones
		std::vector<EditDoc> sorted = fe.edits;
		std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b){
			if (a.range.end.line != b.range.end.line) return a.range.end.line > b.range.end.line;
			return a.range.end.character > b.range.end.character;
		});
		
		// apply each
		for (auto& te : sorted)
			applyEditToLines(lines, te);
		
		// write back
		std::ofstream out(p, std::ios::trunc);
		for (auto& L : lines)
			out << L << "\n";
	}
}

std::vector<EditSection> CodeEdit::gatherCurrentFileSections(const std::vector<FileEdit>& edits, const std::string& currentFilePath) {
	std::vector<EditSection> sections;
	for (auto& fe : edits) {
		// convert URI to path
		
		if (!URISEqual(fe.uri, App::lsp_client_map[lsp]->fromLocalFile(currentFilePath)))
			continue;

		for (auto& te : fe.edits) {
			sections.push_back(EditSection{
				te.range.start.line,
				te.range.start.character,
				te.range.end.line,
				te.range.end.character,
				te.newText
			});
		}
	}
	std::sort(sections.begin(), sections.end(), [](auto const& a, auto const& b) {
			if (a.endLine != b.endLine) return a.endLine > b.endLine;
			return a.endChar  > b.endChar;
	});
	
	return sections;
}

void CodeEdit::request_close(close_callback_type callback) {
	save();
	
	App::MoveWidget(replaceTextEdit, this); // ensure all of these will be deleted
	App::MoveWidget(findTextEdit, this);
	App::MoveWidget(caseSensitivity, this);
	App::MoveWidget(prevButton, this);
	App::MoveWidget(nextButton, this);
	App::MoveWidget(nextReplButton, this);
	App::MoveWidget(allButton, this);
	App::MoveWidget(broken_state_menu, this);
	App::MoveWidget(renamebox, this);
	App::MoveWidget(completionbox, this);
	App::MoveWidget(errorMenu, this);
	App::MoveWidget(showErrorsButton, this);
	App::MoveWidget(hoverbox, this);
	
	delete highlighter;
	
	closing = true;
	if (hoverthread.joinable()) {
		hoverthread.join();
	}
	if (chauffeurthread.joinable()) {
		chauffeurthread.join();
	}
	closing = false;
	
	Widget::request_close(callback);
}