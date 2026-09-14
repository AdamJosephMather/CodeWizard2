#include "codeedit.h"
#include "brokenstatemenu.h"
#include "text_renderer.h"
#include "application.h"
#include "textedit.h"
#include "linenumbers.h"
#include "tinyfiledialogs.h"

#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <set>
#include "editor.h"
#include "curler.h"
//#include "modelxrunner.h"

std::set<MST::u32> whitespace_before_comment = {U'\t', U' '};

CodeEdit::CodeEdit(Widget* parent, int tabid, App::PosFunction positioner, App::UpdateFInfoFunction fupdater) : Widget(parent) {
	before_self_close = [&]() {
		save();
		
		closing = true;
		if (hoverthread.joinable()) {
			hoverthread.join();
		}
		
		if (App::lsp_client_map[lsp]) {
			if (file) {
				App::lsp_client_map[lsp]->closeDocument(file->filepath);
			}
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
	
	fpsTime = glfwGetTime();
	
	id = MST::toMonoString("CodeEdit");
	
	broken_state_menu = new BrokenStateMenu(nullptr, "Reload", "Overwrite", "File changed on disk.");
	broken_state_menu->first_callback = [&](){
		reload_file();
	};
	broken_state_menu->second_callback = [&](){
		overwrite_file();
	};
	broken_state_menu->const_parent = this;
	
	fixit_request_menu = new BrokenStateMenu(nullptr, "Yes", "No", "Detected space based indenting, Fixit?");
	fixit_request_menu->first_callback = [&](){
		App::RemoveWidgetFromParent(fixit_request_menu);
		run_fixit();
		REQUESTING_FIXIT = false;
		DO_RENDER = 3;
		App::setActiveLeafNode(textedit);
	};
	fixit_request_menu->second_callback = [&](){
		App::RemoveWidgetFromParent(fixit_request_menu);
		REQUESTING_FIXIT = false;
		DO_RENDER = 3;
		App::setActiveLeafNode(textedit);
	};
	fixit_request_menu->const_parent = this;
	
	renamecursor = Cursor();
	renamebox = new TextEdit(nullptr, [&](Widget* w){
		if (renamecursor.head_line > textedit->lines.size() && renamebox->parent == this) {
			App::RemoveWidgetFromParent(renamebox);
			App::setActiveLeafNode(textedit);
			DO_RENDER = 3;
			return;
		}
		
		int col = renamecursor.head_char;
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (renamecursor.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/3;
		w->t_h = App::text_padding*2+TextRenderer::get_text_height();
	});
	renamebox->id = MST::toMonoString("renamebox");
	renamebox->background_color = App::theme.extras_background_color;
	renamebox->const_parent = this;
	
	completionbox = new ListBox(this, [&](Widget* w){
		Cursor c = textedit->cursors[0];
		int col = c.head_char;
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (c.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/3;
	});
	completionbox->ONCLICK = [&](Widget*, int){
		activateCompletion();
	};
	completionbox->is_visible_layered = false;
	completionbox->rounded = true;
	completionbox->id = MST::toMonoString("completionbox");
	completionbox->const_parent = this;
	
	hoverbox = new TextEdit(nullptr, [&](Widget* w){
		int col = hoverCrsr.head_char;
		
		int x = (col-textedit->scrolled_to_horz)*TextRenderer::get_text_width(1)+textedit->t_x+App::text_padding;
		int y = (hoverCrsr.head_line+1-textedit->scrolled_to_vert)*TextRenderer::get_text_height()+textedit->t_y+App::text_padding;
		
		w->t_x = x;
		w->t_y = y;
		w->t_w = textedit->t_w/2;
		w->t_h = w->t_w/2;
		
		if (w->t_y+w->t_h > textedit->t_y+textedit->t_h) {
			w->t_y -= (w->t_h + TextRenderer::get_text_height());
		}
		if (w->t_x+w->t_w > textedit->t_x+textedit->t_w) {
			w->t_x -= w->t_w;
		}
	});
	hoverbox->const_parent = this;
	hoverbox->background_color = App::theme.extras_background_color;
	hoverbox->rounded = true;
	hoverbox->contextmenu->is_visible_3 = true;
	hoverbox->id = MST::toMonoString("hoverbox");
	
	find_menu_open = false;
	
	allButton = new Button(nullptr, MST::toMonoString("All"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = t_x+t_w-App::text_padding-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-App::text_padding;
	}, [&](Button* b){
		replaceAll(findTextEdit->getFullText(), replaceTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	allButton->rounded = true;
	allButton->const_parent = this;
	
	nextReplButton = new Button(nullptr, MST::toMonoString("-→"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = allButton->t_x-App::text_padding-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-App::text_padding;
	}, [&](Button* b){
		activateReplace(true, findTextEdit->getFullText(), replaceTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	nextReplButton->rounded = true;
	nextReplButton->const_parent = this;
	
	nextButton = new Button(nullptr, MST::toMonoString("-→"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = t_x+t_w-App::text_padding-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-findTextEdit->t_h-App::text_padding*2;
	}, [&](Button* b){
		activateFind(true, findTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	nextButton->rounded = true;
	nextButton->const_parent = this;
	
	prevButton = new Button(nullptr, MST::toMonoString("←-"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = nextButton->t_x-App::text_padding-tw;
		b->t_y = t_y+t_h-replaceTextEdit->t_h-findTextEdit->t_h-App::text_padding*2;
	}, [&](Button* b){
		activateFind(false, findTextEdit->getFullText(), caseSensitivity->is_checked);
	});
	prevButton->rounded = true;
	prevButton->const_parent = this;
	
	replaceTextEdit = new TextEdit(nullptr, [&](Widget* t){
		int h = TextRenderer::get_text_height()*std::min((int)replaceTextEdit->lines.size(), 3)+App::text_padding*2;
		
		replaceTextEdit->t_x = t_x+App::text_padding;
		replaceTextEdit->t_w = (nextReplButton->t_x - replaceTextEdit->t_x)-App::text_padding;
		replaceTextEdit->t_h = h;
		replaceTextEdit->t_y = t_y+t_h-h-App::text_padding;
		
		allButton->t_h = h;
		allButton->t_y = replaceTextEdit->t_y;
		nextReplButton->t_h = h;
		nextReplButton->t_y = replaceTextEdit->t_y;
	});
	replaceTextEdit->rounded = true;
	replaceTextEdit->const_parent = this;
	
	findTextEdit = new TextEdit(nullptr, [&](Widget* t){
		int h = TextRenderer::get_text_height()*std::min((int)findTextEdit->lines.size(), 3)+App::text_padding*2;
		
		findTextEdit->t_x = t_x+App::text_padding;
		findTextEdit->t_h = h;
		findTextEdit->t_w = (prevButton->t_x - findTextEdit->t_x)-App::text_padding*2-findTextEdit->t_h; // the space of the bar, and the width of the checkbox, and the padding for each
		findTextEdit->t_y = t_y+t_h-replaceTextEdit->t_h-h-App::text_padding*2;
		
		nextButton->t_h = h;
		nextButton->t_y = findTextEdit->t_y;
		prevButton->t_h = h;
		prevButton->t_y = findTextEdit->t_y;
	});
	findTextEdit->rounded = true;
	findTextEdit->const_parent = this;
	
	caseSensitivity = new CheckBox(nullptr, [&](CheckBox* c, int,int,int,int){
		c->t_h = findTextEdit->t_h;
		c->t_w = c->t_h;
		c->t_x = findTextEdit->t_w + findTextEdit->t_x + App::text_padding;
		c->t_y = findTextEdit->t_y;
	}, nullptr);
	caseSensitivity->rounded = true;
	caseSensitivity->const_parent = this;
	
	line_numbers = new LineNumbers(this);
	line_numbers->border = true;
	
	textedit = new TextEdit(this, [&](Widget* t){
		textedit->t_x = t_x+line_numbers->t_w;
		textedit->t_y = t_y;
		textedit->t_w = t_w-line_numbers->t_w;
		if (find_menu_open) {
			textedit->t_h = findTextEdit->t_y - t_y - App::text_padding;
		}else{
			textedit->t_h = t_h;
		}
	});
	textedit->scrollbar_vertical = true;
	textedit->scrollbar_horizontal = true;
	textedit->contextmenu->is_visible_3 = true;
	textedit->borderColor = nullptr;
	textedit->activeBorderColor = nullptr;
	textedit->id = MST::toMonoString("CodeEdit TextEdit");
	
	textedit->contextmenu->addSeparaterToMenu();
	
	textedit->contextmenu->addToMenu(MST::toMonoString("Comment Out Lines\t(Alt+3)"),   [&](Widget* w){
		setComments();
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->addToMenu(MST::toMonoString("Uncomment Lines\t(Alt+4)"),   [&](Widget* w){
		removeComments();
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->addSeparaterToMenu();
	
	textedit->contextmenu->addToMenu(MST::toMonoString("Goto Def\t(LSP)"),   [&](Widget* w){
		if (App::lsp_client_map[lsp] && file) {
			goto_id = App::lsp_client_map[lsp]->requestGotoDefinition(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
		}
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->addToMenu(MST::toMonoString("Rename Symbol\t(LSP)"),   [&](Widget* w){
		if (App::lsp_client_map[lsp] && file) {
			renamecursor = textedit->cursors[0];
			if (renamebox->parent != this){
				App::MoveWidget(renamebox, this);
				App::setActiveLeafNode(renamebox);
				DO_RENDER = 3;
			}
			renamebox->wasmode = 'n';
			renamebox->mode = 'i';
			renamebox->setFullText(MST::MonoString());
		}
		
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->addToMenu(MST::toMonoString("Bureaucracy Check\t(Python)"),   [&](Widget* w){
		bureaucracyCheckPython();
		textedit->contextmenu->is_visible_2 = false;
	});
	
	textedit->contextmenu->recalcButtonTexts();
	
	showErrorsButton = new Button(nullptr, MST::toMonoString("Show/Hide Errors"), [&](Button* b, int x, int y, int w, int h, int tw, int th){
		b->t_x = textedit->t_x+textedit->t_w-App::text_padding*2-tw;
		b->t_y = textedit->t_y+textedit->t_h-App::text_padding*2-th;
	}, [&](Button* b){
		showhideerrors();
	});
	showErrorsButton->rounded = true;
	showErrorsButton->const_parent = this;
	showErrorsButton->border_color = nullptr;
	
	errorMenu = new ListBox(this, [&](Widget* w){
		w->t_w = textedit->t_w/3; // height is set by the listbox
		
		w->t_x = textedit->t_x+textedit->t_w-w->t_w-App::text_padding*2;
		w->t_y = showErrorsButton->t_y-w->t_h-App::text_padding*2;
	});
	errorMenu->rounded = true;
	errorMenu->is_visible_layered = false;
	errorMenu->toshow = 13;
	errorMenu->ONCLICK = [&](Widget* w, int sel){
		gotoerror(sel);
	};
	errorMenu->const_parent = this;
	
	
	textedit->highlighter = nullptr;
	textedit->highlighter_initial_state.reset(cw_syntect_initial_state(highlighter));
	
	textedit->ontextchange = [&](Widget*) {
		madeChangeBetweenSaves = true;
		
		if (hoverbox->parent == this) {
			App::RemoveWidgetFromParent(hoverbox);
			hoverCrsr = Cursor();
			DO_RENDER = 3;
		}
		
		if (!App::lsp_client_map[lsp] || !file || file->filepath == "") {
			return;
		}
		
		if (!App::lsp_client_map[lsp]->supportsIncrementalChanges) {
			onTextChanged(textedit);
		}
	};
	
	textedit->onlinechange = [&](EditType typ, int lineindex){
		madeChangeBetweenSaves = true;
		
		if (hoverbox->parent == this) {
			App::RemoveWidgetFromParent(hoverbox);
			hoverCrsr = Cursor();
			DO_RENDER = 3;
		}
		
		if (!App::lsp_client_map[lsp] || !file || file->filepath == "") {
			return;
		}
		
		if (!App::lsp_client_map[lsp]->supportsIncrementalChanges) {
//			onTextChanged(textedit); - now handled in ontextchanged
			return;
		}
		
		LineEditType t;
		
		if      (typ == EditType::InsertLine) t = LineEditType::InsertLine;
		else if (typ == EditType::ChangeLine) t = LineEditType::ChangeLine;
		else if (typ == EditType::DeleteLine) t = LineEditType::DeleteLine;
		
		Line& l = textedit->lines[lineindex];
		std::string text = MST::toBastardizedStringUtf16Aligned(l.line_text);
		
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
				DO_RENDER = 3;
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
	
//	chauffeurthread = std::thread([&]() {
//		while (true){
//			if (closing) {
//				return;
//			}
//			
//			if (timeuntilchauffeur <= 0) {
//				std::this_thread::sleep_for(std::chrono::milliseconds(50));
//			}else{
//				timeuntilchauffeur -= 20;
//				if (timeuntilchauffeur <= 0) {
//					
//					Cursor prefix_c = textedit->cursors[0];
//					Cursor suffix_c = textedit->cursors[0];
//					
//					prefix_c.anchor_line -= App::settings->getValue("chauffeur_prefix_lines", 10);
//					prefix_c.anchor_char = 0;
//					suffix_c.anchor_line += App::settings->getValue("chauffeur_suffix_lines", 7);
//					
//					prefix_c.anchor_line = std::max(0, prefix_c.anchor_line);
//					suffix_c.anchor_line = std::min((int)textedit->lines.size()-1, suffix_c.anchor_line);
//					
//					suffix_c.anchor_char = textedit->lines[suffix_c.anchor_line].line_text.length();
//					
//					std::string prefix_s;
//					std::string suffix_s;
//					
//					MST::MonoString prefix = textedit->getSelectedText(prefix_c);
//					MST::MonoString suffix = textedit->getSelectedText(suffix_c);
//					
//					prefix.toUTF8String(prefix_s);
//					suffix.toUTF8String(suffix_s);
//					
//					std::string insertion;
//					
//					if (App::settings->getValue("use_fim", true)) {
//						insertion = ModelXRunner::generate_fim(prefix_s, suffix_s, App::settings->getValue("chauffeur_max_continue", 6));
//					}else{
//						insertion = ModelXRunner::generate(prefix_s, App::settings->getValue("chauffeur_max_continue", 6));
//					}
//					
//					if (insertion == "") {
//						continue;
//					}
//					
//					std::vector<MST::MonoString> compld = {MST::toMonoString(insertion)};
//					
//					completionbox->is_visible_layered = true;
//					completionbox->setElements(compld);
//					are_code_actions = false;
//					is_chauffeur = true;
//					completionbox->toshow = compld.size();
//					App::time_till_regular = 2;
//				}
//				std::this_thread::sleep_for(std::chrono::milliseconds(20));
//			}
//		}
//	});
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
		if (file) {
			App::lsp_client_map[lsp]->closeDocument(file->filepath);
		}
		for (int i = static_cast<int>(App::lsp_client_map[lsp]->connected_edits.size())-1; i >= 0; i--) {
			if (App::lsp_client_map[lsp]->connected_edits[i] == this) { 
				App::lsp_client_map[lsp]->connected_edits.erase(App::lsp_client_map[lsp]->connected_edits.begin()+i);
			}
		}
	}
	
	textedit->highlighter = nullptr;
	textedit->highlighter_initial_state = nullptr;
	
	if (highlighter) { // don't delete highlighter - may be used by other codeedits
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
	
	App::setTextEditHighlighter(textedit, extension);
	
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
					App::lsp_client_map[lsp]->closeDocument(file->filepath);
					std::string str = MST::toBastardizedStringUtf16Aligned(textedit->getFullText());
					App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
				}
			}
		}
	}
	
	Widget::executeAction(typ);
}

void CodeEdit::run_fixit() {
	std::vector<MST::MonoString> lns;
	for (auto& l : textedit->lines) {
		lns.push_back(l.line_text);
	}
	
	textedit->setFullLines(run_fixit_on_lines(lns));
	onTextChanged(textedit);
}

void CodeEdit::undo_fixit() {
	std::vector<MST::MonoString> lns;
	for (auto& l : textedit->lines) {
		lns.push_back(l.line_text);
	}
	
	textedit->setFullLines(undo_fixit_on_lines(lns));
	onTextChanged(textedit);
}

int CodeEdit::analyzeForFixit_on_lines(const std::vector<Line>& lines) {
	std::vector<MST::MonoString> lines_new;
	
	for (auto& l : lines) {
		lines_new.push_back(l.line_text);
	}
	
	return analyzeForFixit(lines_new);
}

void CodeEdit::openFile(FileInfo* f) {
	madeChangeBetweenSaves = false;
	was_in_a_file = true;
	std::unique_lock<std::mutex> lock(App::canMakeChanges, std::try_to_lock); // the lsp client will block up if we don't try because of goto def
	std::unique_lock<std::mutex> lock2(saving_lock);
	
	file = f; // set it after the locks are in place
	if (file && !file->backend) file->backend = FileBackends::current();
	
	REQUESTING_FIXIT = false;
	DO_RENDER = 3;
	if (fixit_request_menu->parent == this) {
		App::RemoveWidgetFromParent(fixit_request_menu);
	}
	std::string path = file->filepath;
	App::setActiveLeafNode(textedit);
	
	detectLanguage();
	
	bool path_exists = false;
	std::string backend_error;
	if (!file->backend->exists(path, path_exists, backend_error) || !path_exists) {
		file->is_opening = false;
		file = nullptr;
		textedit->setFullText(MST::toMonoString("File does not exist: "+path));
		onTextChanged(textedit);
		last_file_mod_time = {};
		return;
	}
	
	bool path_is_binary = true;
	if (!file->backend->isBinary(path, path_is_binary, backend_error) || path_is_binary){
		file->is_opening = false;
		file = nullptr;
		textedit->setFullText(MST::toMonoString("File detected as binary file: "+path+"\nDID NOT OPEN"));
		onTextChanged(textedit);
		last_file_mod_time = {};
		return;
	}
	
	bool worked = true;
	MST::MonoString text = App::readFileToMonoString(path, worked, file->backend);
	
	if (worked) {
		std::string metadata_error;
		file->backend->modificationTime(path, last_file_mod_time, metadata_error);
		
		textedit->setFullText(text);
		onTextChanged(textedit);
		madeChangeBetweenSaves = false;
		
		int indt = analyzeForFixit_on_lines(textedit->lines);
		if (indt != 0) {
			REQUESTING_FIXIT = true;
			DO_RENDER = 3;
			App::MoveWidget(fixit_request_menu, this);
			App::setActiveLeafNode(fixit_request_menu);
		}
		
		if (App::lsp_client_map[lsp]) {
			std::string str = MST::toBastardizedStringUtf16Aligned(text);
			App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
		}
	}else {
		file->is_opening = false;
		file = nullptr;
		textedit->setFullText(MST::toMonoString("Failed to open file: "+path+"\n\n")+text);
		onTextChanged(textedit);
		last_file_mod_time = {};
	}
	
	if (file) file->is_opening = false;
}

int CodeEdit::indentIdentifierAfterLine(MST::MonoString line, MST::MonoString nextline) {
	bool in_meat = false;
	int indent_levels = 0;
	int openers = 0;
	MST::u32 lastChar = U' ';
	
	for (int i = 0; i < line.length; i++) {
		if (MST::skipIdx(line, i)) {
			continue;
		}
		
		MST::u32 c = MST::char32At(line, i);
		
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

void CodeEdit::renderExtras() {
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
}

void CodeEdit::renderFindBox() {
	if (find_menu_open) {
		App::DrawRect(t_x, textedit->t_y + textedit->t_h + 1, t_w, t_h-textedit->t_h - 1, App::theme.extras_background_color);
		App::DrawRect(t_x, textedit->t_y + textedit->t_h, t_w, 1, App::theme.border);
		
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
}

bool CodeEdit::renderTheSplashIfNeeded() {
	if (!file && textedit->lines.size() == 1 && textedit->lines[0].line_text.length == 0) {
		if (splash_transparency != 1) {
			splash_transparency += 0.1 * App::settings->getValue("anim_speed", 1.0f);
			App::time_till_regular = 2;
			DO_RENDER = 3;
			
			if (splash_transparency > 1) {
				splash_transparency = 1;
			}
		}
	}else if (splash_transparency != 0) {
		splash_transparency -= 0.1 * App::settings->getValue("anim_speed", 1.0f);
		App::time_till_regular = 2;
		DO_RENDER = 3;
		
		if (splash_transparency < 0) {
			splash_transparency = 0;
		}
	}
	
	if (splash_transparency != 0) {
		int sW = t_w*.6;
		int sH = sW;
		
		if (sH > t_h*.6) {
			double change = (double)(t_h*.6) / sH;
			sW *= change;
			sH *= change;
		}
		
		int sx = t_x + (t_w-sW)/2;
		int sy = t_y + (t_h-sH)/2;
		
		
		Color* to_use = App::MakeTransparentColor(App::theme.hover_background_color, splash_transparency); // we don't own the pointer here, it gets reused by everybody. Seems insane, it's actually genius.
		App::DrawSVG(App::splashTexture, sx, sy, sW, sH, to_use);
		return true;
	}
	
	return false;
}

void CodeEdit::render() {
	if (textedit->DID_POSITION || findTextEdit->DID_POSITION || replaceTextEdit->DID_POSITION || renamebox->DID_POSITION || hoverbox->DID_POSITION || FILE_BROKEN_STATE || REQUESTING_FIXIT) {
		DO_RENDER = 3;
	}
	
	Color* borderC = App::theme.border;
	if (textedit == App::activeLeafNode) {
		borderC = App::theme.active_color;
	}
	
	if (DO_RENDER == 0 && App::reclear == 0) {
		renderTheSplashIfNeeded();
		
		renderFindBox();
		
		// needs to happen because otherwise we don't get hovering when mouse moves this is so scuffed
		textedit->scrollbar_h->render();
		textedit->scrollbar_v->render();
		textedit->contextmenu->render();
		
		renderExtras();
		
		if (rounded) {
			App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
			App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, App::text_padding);
		}else{
			App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
		}
		return;
	}
	DO_RENDER -= 1;
	
	if (FILE_BROKEN_STATE) {
		// we don't need to run with skiz because this is already skizzed in that size
		broken_state_menu->render();
	}else if (REQUESTING_FIXIT){
		// we don't need to run with skiz because this is already skizzed in that size
		fixit_request_menu->render();
	}else {
		renderFindBox();
		
		App::runWithSKIZ(line_numbers->t_x, line_numbers->t_y, line_numbers->t_w, textedit->t_h, [&](){
			line_numbers->render();
		});
		
		App::runWithSKIZ(textedit->t_x, textedit->t_y, textedit->t_w, textedit->t_h, [&](){
			textedit->render();
			
			renderExtras();
		});
		
		if (renderTheSplashIfNeeded()) {
			textedit->contextmenu->render(); // again after textedit because of the splash
		}
	}
	
	double currentTime = glfwGetTime();
	drawn_frames ++;
	
	if (currentTime - fpsTime >= 2.0) {
		float fps = (double)drawn_frames/(currentTime-fpsTime);
		FPS = doubleToMonoString_pretty(fps);
		fpsTime = currentTime;
		drawn_frames = 0;
	}
	
	if (App::settings->getValue("show_fps", false)) {
		int x = t_x+t_w-TextRenderer::get_text_width(FPS.length+2)-App::text_padding*2;
		int y = t_y+TextRenderer::get_text_width(2);
		App::DrawRoundedRect(x, y, TextRenderer::get_text_width(FPS.length)+App::text_padding*2, TextRenderer::get_text_height()+App::text_padding*2, App::text_padding, App::theme.extras_background_color, true);
		TextRenderer::draw_text(x+App::text_padding, y+App::text_padding, FPS, App::theme.main_text_color);
	}
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
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
			App::setActiveLeafNode(broken_state_menu);
		}
		broken_state_menu->position(t_x, t_y, t_w, t_h);
		return;
	}else if (REQUESTING_FIXIT) {
		if (fixit_request_menu->parent == nullptr) {
			App::MoveWidget(fixit_request_menu, this);
			App::setActiveLeafNode(fixit_request_menu);
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
	line_numbers->t_h = textedit->t_h;
	line_numbers->t_y = textedit->t_y;
	
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
		DO_RENDER = 3;
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

	if (FileBackends::isRemote()) {
		App::requestString("Remote save path?", default_path, [this](MST::MonoString selected) {
			const std::string filePath = MST::toString(selected);
			if (filePath.empty()) {
				App::commandUnfocused();
				return;
			}
			FileInfo *f = new FileInfo();
			f->filepath = filePath;
			f->filename = FileBackends::current()->filename(filePath);
			f->backend = FileBackends::current();
			std::unique_lock<std::mutex> lock(saving_lock);
			FUPDATER(this, f);
			file = f;
			std::string metadata_error;
			file->backend->modificationTime(file->filepath, last_file_mod_time, metadata_error);
			detectLanguage();
			App::reclear = 3;
			textedit->DO_POSITION = true;
			DO_RENDER = 3;
			was_in_a_file = false;
			f->is_opening = false;
			madeChangeBetweenSaves = true;
			lock.unlock();
			save();
			if (App::lsp_client_map[lsp]){
				std::string str = MST::toBastardizedStringUtf16Aligned(textedit->getFullText());
				App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
			}
			App::commandUnfocused();
		});
		return;
	}

	const char* default_path_ptr = default_path.empty() ? NULL : default_path.c_str();
	
	const char * fp = tinyfd_saveFileDialog(
		"Save as?",
		default_path_ptr,
		0,
		NULL,
		NULL
	);
	
	if (fp) {
		std::string filePath(fp);
		
		std::filesystem::path fullPath = filePath;
		std::string filename = fullPath.filename().string();
		
		FileInfo *f = new FileInfo();
		f->filepath = filePath;
		f->filename = filename;
		f->backend = FileBackends::current();
		
		std::unique_lock<std::mutex> lock(saving_lock);
		
		FUPDATER(this, f);
		
		file = f;
		
		std::string metadata_error;
		file->backend->modificationTime(file->filepath, last_file_mod_time, metadata_error);
		
		detectLanguage();
		
		App::reclear = 3;
		textedit->DO_POSITION = true;
		DO_RENDER = 3;
		
		was_in_a_file = false;
		f->is_opening = false;
		madeChangeBetweenSaves = true;
		
		lock.unlock();
		
		save();
		
		if (App::lsp_client_map[lsp]){
			std::string str = MST::toBastardizedStringUtf16Aligned(textedit->getFullText());
			App::lsp_client_map[lsp]->openDocument(file->filepath, App::languagemap[language].name, str);
		}
	}
	
	App::commandUnfocused();
}

void CodeEdit::overwrite_file() {
	FILE_BROKEN_STATE = false;
	DO_RENDER = 3;
	App::RemoveWidgetFromParent(broken_state_menu);
	
	std::string metadata_error;
	file->backend->modificationTime(file->filepath, last_file_mod_time, metadata_error);
	madeChangeBetweenSaves = true;
	was_in_a_file = false;
	save();
	
	App::setActiveLeafNode(textedit);
}

void CodeEdit::reload_file() {
	FILE_BROKEN_STATE = false;
	DO_RENDER = 3;
	App::RemoveWidgetFromParent(broken_state_menu);
	
	openFile(file);
}

void CodeEdit::save() {
	std::lock_guard<std::mutex> lock(saving_lock);
	
	if (file && !file->is_opening) {
		std::string filepath = file->filepath;
		
		std::string metadata_error;
		std::int64_t current = 0;
		const bool have_mtime = file->backend->modificationTime(file->filepath, current, metadata_error);
		
		if (have_mtime) {
			was_in_a_file = true;
			
			if (current != last_file_mod_time) { // some other process edited the file since we touched it
				FILE_BROKEN_STATE = true;
				DO_RENDER = 3;
				return;
			}else if (!madeChangeBetweenSaves) { // we did not make any changes to the file - return early
				FILE_BROKEN_STATE = false;
				DO_RENDER = 3;
				if (broken_state_menu->parent == this) {
					App::RemoveWidgetFromParent(broken_state_menu);
				}
				return;
			}
		}else if (was_in_a_file) { // there's an error and we were in the file - file must have been deleted. Clever me.
			// the file did exist, but no longer does
			FILE_BROKEN_STATE = true;
			DO_RENDER = 3;
			return;
		}
		
		FILE_BROKEN_STATE = false;
		DO_RENDER = 3;
		if (broken_state_menu->parent == this) {
			App::RemoveWidgetFromParent(broken_state_menu);
		}
		
		MST::MonoString content = textedit->getFullText();
		std::string str = MST::toString(content);
		
		{
			std::string err;
			std::vector<std::uint8_t> bytes(str.begin(), str.end());
			if (!file->backend->writeFile(filepath, bytes, err)) {
				std::cerr << "Atomic save failed for " << filepath << ": " << err << "\n";
				return;
			}
			file->backend->modificationTime(file->filepath, last_file_mod_time, err);
			madeChangeBetweenSaves = false;
		}
		
		was_in_a_file = true;
		
		
		if (App::lsp_client_map[lsp]) {
			std::string str_lsp = MST::toBastardizedStringUtf16Aligned(content);
			App::lsp_client_map[lsp]->documentSaved(file->filepath, str_lsp);
		}
	}
	
	Widget::save();
}

void CodeEdit::replaceAll(const MST::MonoString& tofind, const MST::MonoString& toreplace, bool case_sensitive) {
	if (tofind.length == 0) {
		return;
	}
	
	for (int l = 0; l < textedit->lines.size(); l ++) {
		const MST::MonoString& true_line = textedit->lines[l].line_text;
		int initiallen = true_line.length;
		
		size_t index = MST::index(true_line, 0, tofind, !case_sensitive);
		
		if (index != MST::NOT_FOUND) {
			const MST::MonoString newline = MST::replaceAll(true_line, tofind, toreplace, !case_sensitive);
			
			Cursor c = Cursor();
			c.head_char = 0;
			c.head_line = l;
			c.anchor_char = initiallen;
			c.anchor_line = l;
			textedit->insertTextAtCursor(c, newline);
		}
	}
}

void CodeEdit::activateReplace(bool forwards, MST::MonoString tofind, const MST::MonoString& toreplace, bool case_sensitive) {
	if (tofind.length == 0) {
		return;
	}
	
	auto has = textedit->getSelectedText(textedit->cursors[0]);
	if (!case_sensitive){
		tofind = MST::toLower(tofind);
		has = MST::toLower(has);
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

void CodeEdit::activateFind(bool forwards, const MST::MonoString& tofind, bool case_sensitive) {
	auto location = textedit->findText(forwards, tofind, case_sensitive, textedit->cursors[0]);
	
	std::cout << location.head_line << "," << location.head_char << "\n";
	std::cout << "  " << location.anchor_line << "," << location.anchor_char << "\n";
	
	if (location.head_char != -1) {
		textedit->cursors = { location };
		textedit->ensureCursorVisible(textedit->cursors[0]);
	}
}

bool CodeEdit::on_char_event(unsigned int keycode) {
	if (!is_visible) {
		return false;
	}
	
	if (FILE_BROKEN_STATE) { return broken_state_menu->on_char_event(keycode); }
	if (REQUESTING_FIXIT) { return fixit_request_menu->on_char_event(keycode); }
	
	std::lock_guard<std::mutex> lock(saving_lock);
	
	if (App::activeLeafNode != hoverbox) {
		if (hoverbox->parent == this) {
			App::RemoveWidgetFromParent(hoverbox);
			DO_RENDER = 3;
		}
	}
	
	if (Widget::on_char_event(keycode)) {
		if (textedit == App::activeLeafNode && textedit->wasmode == 'i' && textedit->cursors.size() == 1) {
			char utf8[5] = {};
			int len = std::snprintf(utf8, sizeof(utf8), "%c", keycode);
			if (len > 0) { // there is something printable
				if (App::lsp_client_map[lsp] && file) { // lsp completion, much faster (faster than what?)
					completion_id = App::lsp_client_map[lsp]->requestCompletion(file->filepath, textedit->cursors[0].head_line, textedit->cursors[0].head_char);
				}
				
//				if (App::settings->getValue("use_chauffeur", false)) {
//					timeuntilchauffeur = App::settings->getValue("chauffeur_time", 1000);
//				}
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
	std::unique_lock<std::mutex> lock(saving_lock);
	
	if (FILE_BROKEN_STATE) { return broken_state_menu->on_key_event(key, scancode, action, mods); }
	
	bool shift_held = (mods & GLFW_MOD_SHIFT) != 0;
	bool control_held = (mods & GLFW_MOD_CONTROL) != 0;
	bool alt_held = (mods & GLFW_MOD_ALT) != 0;
	
	bool is_press = (action == GLFW_PRESS || action == GLFW_REPEAT);
	
	Cursor svdCrsr = textedit->cursors[0];
	
	if (parent == App::activeEditor) {
//		if (is_press && timeuntilchauffeur != App::settings->getValue("chauffeur_time", 1000)) {
//			if (key == GLFW_KEY_ESCAPE || timeuntilchauffeur <= 0) {
//				timeuntilchauffeur = 0;
//			}else{
//				timeuntilchauffeur = App::settings->getValue("chauffeur_time", 1000);
//			}
//		
//		}
		
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
				
				std::string befr = MST::toString(t1);
				
				Cursor end = Cursor();
				end.anchor_char = cur.head_char;
				end.anchor_line = cur.head_line;
				end.head_line = std::min((int)textedit->lines.size()-1, cur.head_line+contextsize);
				end.head_char = textedit->lines[end.head_line].line_text.length;
				auto t2 = textedit->getSelectedText(end);
				
				std::string aftr = MST::toString(t2);
				
				App::displayToast(MST::toMonoString("Contacting AI Provider"));
				std::string insertion = Curler::StreamInsertion(befr, aftr, [this](std::string s){
					textedit->insertTextAtCursor(textedit->cursors[0], MST::toMonoString(s));
					App::time_till_regular = 2;
				});
			});
			return true;
		}
		
		if (App::activeLeafNode == renamebox && renamebox->parent == this) {
			if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE && (renamebox->mode == 'n' || !App::settings->getValue("use_vim", false))) {
				App::RemoveWidgetFromParent(renamebox);
				App::setActiveLeafNode(textedit);
				DO_RENDER = 3;
				return true;
			}else if (action == GLFW_PRESS && key == GLFW_KEY_ENTER){
				App::RemoveWidgetFromParent(renamebox);
				DO_RENDER = 3;
				if (App::lsp_client_map[lsp]) {
					std::string rename = MST::toBastardizedStringUtf16Aligned(renamebox->getFullText());
					rename_id = App::lsp_client_map[lsp]->requestRename(file->filepath, renamecursor.head_line, renamecursor.head_char, rename);
				}
				App::setActiveLeafNode(textedit);
				return true;
			}
		}else if (renamebox->parent == this){
			App::RemoveWidgetFromParent(renamebox);
			DO_RENDER = 3;
		}
		
		if (App::activeLeafNode != hoverbox) {
			if (hoverbox->parent == this && is_press && key != GLFW_KEY_LEFT_SHIFT && key != GLFW_KEY_RIGHT_SHIFT) {
				App::RemoveWidgetFromParent(hoverbox);
				DO_RENDER = 3;
			}
		}else if (is_press && App::activeLeafNode == hoverbox && key == GLFW_KEY_ESCAPE && (!App::settings->getValue("use_vim", false) || hoverbox->mode == 'n')) {
			App::setActiveLeafNode(textedit);
			App::RemoveWidgetFromParent(hoverbox);
			DO_RENDER = 3;
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
						DO_RENDER = 3;
					}
					renamebox->wasmode = 'n';
					renamebox->mode = 'i';
					renamebox->setFullText(MST::MonoString());
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
					DO_RENDER = 3;
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
			}if (key == GLFW_KEY_3 && alt_held && is_press && language != "" && App::languagemap[language].line_comment.length != 0) {
				setComments();
				return true;
			}else if (key == GLFW_KEY_4 && alt_held && is_press && language != "" && App::languagemap[language].line_comment.length != 0) {
				removeComments();
				return true;
			}else if (key == GLFW_KEY_SLASH && control_held && is_press && language != "" && App::languagemap[language].line_comment.length != 0) {
				auto find = App::languagemap[language].line_comment;
	
				Cursor c = textedit->cursors[0];
				auto slelscec = textedit->_getCursSelec(c);
				int l = slelscec.first.first;
				auto line_text = textedit->lines[l].line_text;
				int indx = MST::index(line_text, 0, find);
				if (indx == -1) {
					setComments();
					return true;
				}
				
				for (int z = 0; z < indx; z++) {
					if (!whitespace_before_comment.count(MST::char32At(line_text, z))){
						setComments();
						return true;
					}
				}
				
				removeComments();
				return true;
			}
		}
		
		if (key == GLFW_KEY_S && is_press && shift_held && control_held){
			lock.unlock();
			triggerSaveAs();
			lock.lock();
			return true;
		}else if (key == GLFW_KEY_S && is_press && !shift_held && control_held && !file){
			lock.unlock();
			triggerSaveAs();
			lock.lock();
			return true;
		}else if (key == GLFW_KEY_F && is_press && (control_held || (App::activeLeafNode == textedit && textedit->mode == 'n')) && !alt_held) {
			// open find menu and whatnot
			find_menu_open = true;
			DO_RENDER = 3;
			
			App::MoveWidget(replaceTextEdit, this); // move them back to be children.
			App::MoveWidget(findTextEdit, this);
			App::MoveWidget(caseSensitivity, this);
			App::MoveWidget(prevButton, this);
			App::MoveWidget(nextButton, this);
			App::MoveWidget(nextReplButton, this);
			App::MoveWidget(allButton, this);
			
			auto t = textedit->getSelectedText(textedit->cursors[0]);
			if (t.length != 0) {
				findTextEdit->setFullText(t);
			}
			
			Cursor newcursor; // select all text
			newcursor.anchor_char = 0;
			newcursor.anchor_line = 0;
			newcursor.head_line = findTextEdit->lines.size()-1;
			newcursor.head_char = findTextEdit->lines[newcursor.head_line].line_text.length;
			newcursor.preffered_collumn = newcursor.head_char;
			
			findTextEdit->cursors = { newcursor };
			findTextEdit->wasmode = 'n'; // this is a dirty hack because otherwise we sometimes end up sending a 'f' key to the findtextedit. This way we can ignore it in the char callback
			findTextEdit->mode = 'i';
			findTextEdit->tryingToEnsureCursorPos = true;
			replaceTextEdit->mode = 'i';
			replaceTextEdit->tryingToEnsureCursorPos = true;
			
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
			
			if (key == GLFW_KEY_ESCAPE && is_press && (in_normal || !App::settings->getValue("use_vim", false))) {
				find_menu_open = false;
				DO_RENDER = 3;
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
			DO_RENDER = 3;
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
			textedit->insertTextAtCursor(comc, MST::MonoString( App::languagemap[language].line_comment ));
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
			
			int indx = MST::index(line_text, 0, remove);
			if (indx == -1) {
				continue;
			}
			
			bool works = true;
			
			for (int z = 0; z < indx; z++) {
				if (!whitespace_before_comment.count(MST::char32At(line_text, z))){
					works = false;
					break;
				}
			}
			
			if (!works) {
				continue;
			}
			
			Cursor comc = Cursor();
			comc.head_char = indx;
			comc.anchor_char = indx+remove.length;
			
			if (indx+remove.length < line_text.length) {
				if (MST::char32At(line_text, indx+remove.length) == U' ') {
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
			DO_RENDER = 3;
			return hoverbox->on_mouse_button_event(button, action, mods);
		}
		if (hoveringCompletionBox(mx, my)) {
			return completionbox->on_mouse_button_event(button, action, mods);
		}else{
			completionbox->is_visible_layered = false;
			DO_RENDER = 3;
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
	if (completionbox->parent == this && completionbox->is_visible_layered && completionbox->is_visible) {
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
	}else {
		int mx = App::mouseX;
		int my = App::mouseY;
		
		bool gottoit = false;
		Cursor crsr = textedit->getCursorForMousePosition(mx, my, &gottoit);
		
		if (hoverbox->parent == this && hoveringHoverbox(mx, my)) {
			timeuntil = -1; // prevents us from requesting a hover on text beneath the hover box.
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
						DO_RENDER = 3;
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
			DO_RENDER = 3;
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
	
	std::vector<MST::MonoString> els;
	
	for (auto j : code_actions) {
		std::string tstr = j["title"];
		MST::MonoString title = MST::toMonoString(tstr);
		els.push_back(title);
	}
	
	if (els.empty()) {
		completionbox->is_visible_layered = false;
		DO_RENDER = 3;
		return;
	}
	
	completionbox->is_visible_layered = true;
	DO_RENDER = 3;
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
	
	if (textedit->cursors.size() != 1) {
		completionbox->is_visible_layered = false;
		DO_RENDER = 3;
		return;
	}
	
	MST::MonoString wrd = textedit->getCurrentWord(textedit->lines[textedit->cursors[0].head_line].line_text, textedit->cursors[0].head_char);
	
	std::vector<MST::MonoString> compld;
	for (auto c : completions) {
		auto icustr = MST::toMonoString(c);
		auto lwrd = icustr;
		
		if (MST::startsWith(lwrd, wrd, true)){
			compld.push_back(icustr);
		}
	}
	
	if (compld.size() == 0) {
		completionbox->is_visible_layered = false;
		DO_RENDER = 3;
		return;
	}
	
	completionbox->is_visible_layered = true;
	DO_RENDER = 3;
	completionbox->setElements(compld);
	are_code_actions = false;
//	is_chauffeur = false;
	
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
	
	applyEditsToTextedit(textedit, sections);
	
	return;
}

void CodeEdit::activateCompletion() {
	if (completionbox->elements.size() == 0 || textedit->cursors.size() != 1) {
		return;
	}

	MST::MonoString selected = completionbox->elements[completionbox->selected_id];

	if (are_code_actions) {
		auto edits = parseCodeAction(code_actions[completionbox->selected_id]);

		applyOtherFileEdits(edits, file->filepath);

		auto sections = gatherCurrentFileSections(edits, file->filepath);

		applyEditsToTextedit(textedit, sections);

		return;
	}

	MST::MonoString wrd = textedit->getCurrentWord(
		textedit->lines[textedit->cursors[0].head_line].line_text,
		textedit->cursors[0].head_char
	);

	textedit->cursors[0].anchor_char =
		textedit->cursors[0].head_char - static_cast<int>(wrd.length);

	textedit->cursors[0].anchor_line = textedit->cursors[0].head_line;

	int startchar = textedit->cursors[0].anchor_char;
	int startline = textedit->cursors[0].anchor_line;

	int firstChar = -1;
	int firstLine = -1;
	selected = MST::removeSnippetPlaceholders(selected, firstLine, firstChar);
	
	textedit->applyInsertToAllCursors(selected);

	if (firstChar != -1 && firstLine != -1) {
		if (firstLine == 0) {
			textedit->cursors[0].head_char = startchar + firstChar;
		}else {
			textedit->cursors[0].head_char = firstChar;
		}
		
		textedit->cursors[0].head_line = startline + firstLine;
		
		textedit->cursors[0].anchor_char = textedit->cursors[0].head_char;
		textedit->cursors[0].preffered_collumn = textedit->cursors[0].head_char;
		
		textedit->cursors[0].anchor_line = textedit->cursors[0].head_line;
	}
}

void CodeEdit::publishDiagnostics(std::string filename, std::vector<std::string> messages, std::vector<int> startC, std::vector<int> startL, std::vector<int> endC, std::vector<int> endL, std::vector<int> severities) {
	std::lock_guard<std::mutex> lock(App::canMakeChanges);
	
	if (!App::lsp_client_map[lsp] || !file || !URISEqual(filename, App::lsp_client_map[lsp]->fromLocalFile(file->filepath))) {
		return;
	}
	
	DO_RENDER = 3;
	textedit->DO_POSITION = true;
	
	for (int i = 0; i < textedit->lines.size(); i++) {
		textedit->lines[i].diagnostics.clear();
	}
	
	std::vector<MST::MonoString> errorsAsUnicode;
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
		
		MST::MonoString mes = MST::toMonoString(messages[i]);
		errorsAsUnicode.push_back(MST::toMonoString(std::to_string(sl+1)+" - ")+mes);
		errorlines.push_back(sl);
		
		for (int l = sl; l < el+1; l++){
			if (l > textedit->lines.size()-1) {
				break; // again this can happen
			}
			
			int srt = 0;
			int end = textedit->lines[l].line_text.length-1;
			
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
		
		if (textedit->lines[line].line_text.length < character || textedit->lines[line1].line_text.length < character1) {
			return;
		}
		
		textedit->cursors = { { line1, character1, line, character, character } };
		textedit->tryingToEnsureCursorPos = true;
	}else {
		if (auto edtr = dynamic_cast<Editor*>(parent)) {
			FileInfo* finfo = new FileInfo();
			finfo->filepath = pointing_to;
			finfo->filename = FileBackends::current()->filename(pointing_to);
			finfo->backend = FileBackends::current();
			
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
		DO_RENDER = 3;
	}
	
	hoverbox->setFullText(MST::toMonoString(content));
	hoverbox->scrolled_to_vert = 0;
	hoverbox->scrolled_to_horz = 0;
	
	if (should_move_mouse_hover){
		hoverbox->position(t_x, t_y, t_w, t_h);
		App::moveMouse(hoverbox->t_x+hoverbox->t_w/2, hoverbox->t_y+hoverbox->t_h/2);
		App::setActiveLeafNode(hoverbox);
		DO_RENDER = 3;
	}
}

void CodeEdit::onTextChanged(Widget* w) {
	madeChangeBetweenSaves = true;
	
	auto te = dynamic_cast<TextEdit*>(w);
	if (!te) {return;}
	
	if (hoverbox->parent == this) {
		App::RemoveWidgetFromParent(hoverbox);
		hoverCrsr = Cursor();
		DO_RENDER = 3;
	}
	
	if (!App::lsp_client_map[lsp] || !file || file->filepath == "") {
		return;
	}
	
	std::string text = MST::toBastardizedStringUtf16Aligned(te->getFullText());
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
		
		std::string targetPath = fileUriToPath(fe.uri);
		
		// collect EditSections for this file
		std::vector<EditSection> sections;
		for (auto& te : fe.edits) {
			sections.push_back(EditSection{
				te.range.start.line,
				te.range.start.character,
				te.range.end.line,
				te.range.end.character,
				te.newText
			});
		}
		
		// check if the file is already open in an editor
		if (auto editorWidget = App::rootelement->fileOpen(targetPath)) {
			if (auto edtr = dynamic_cast<Editor*>(editorWidget)) {
				for (auto itm : edtr->tab_bar->tabs_list) {
					if (auto ce = dynamic_cast<CodeEdit*>(edtr->editors[itm.id])) {
						if (ce->file && areSameFile(ce->file->filepath, targetPath)) {
							applyEditsToTextedit(ce->textedit, sections);
							break;
						}
					}
				}
			}
			continue;
		}
		
		// file not open — apply edits directly on disk
		std::vector<std::uint8_t> file_bytes;
		std::string backend_error;
		if (!FileBackends::current()->readFile(targetPath, file_bytes, backend_error)) {
			App::displayToast(MST::toMonoString("LSP edit read failed: " + backend_error));
			continue;
		}
		std::vector<std::string> lines = splitLines(std::string(file_bytes.begin(), file_bytes.end()));
	
		std::vector<EditDoc> sorted = fe.edits;
		std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b){
			if (a.range.end.line != b.range.end.line) return a.range.end.line > b.range.end.line;
			return a.range.end.character > b.range.end.character;
		});
		
		for (auto& te : sorted)
			applyEditToLines(lines, te);
		
		std::string updated;
		for (std::size_t i = 0; i < lines.size(); ++i) {
			if (i != 0) updated.push_back('\n');
			updated += lines[i];
		}
		std::vector<std::uint8_t> updated_bytes(updated.begin(), updated.end());
		if (!FileBackends::current()->writeFile(targetPath, updated_bytes, backend_error)) {
			App::displayToast(MST::toMonoString("LSP edit write failed: " + backend_error));
		}
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

void CodeEdit::applyEditsToTextedit(TextEdit* te, const std::vector<EditSection>& sections) {
	std::vector<EditSection> sorted = sections;
	std::sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) {
		if (a.endLine != b.endLine) return a.endLine > b.endLine;
		return a.endChar  > b.endChar;
	});

	for (auto& s : sorted) {
		Cursor c = Cursor();
		c.anchor_char = s.startChar;
		c.anchor_line = s.startLine;
		c.head_char = s.endChar;
		c.head_line = s.endLine;
		
		std::cout << "Replacing [" << s.startLine << ":" << s.startChar << " -> " << s.endLine   << ":" << s.endChar << "] with: " << s.newText << "\n";
		std::cout << "'" << MST::toString(te->lines[c.head_line].line_text) << "'\n";
		
		if (c.head_line > te->lines.size()-1 || c.anchor_line > te->lines.size()-1) {
			std::cerr << "Had to skip one outside of line bounds\n";
			continue;
		}
		
		te->insertTextAtCursor(c, MST::toMonoString(s.newText));
	}
}

void CodeEdit::bureaucracyCheckPython() {
	if (!textedit) {
		return;
	}
	
	textedit->DO_POSITION = true;
	
	// Rename this one line if your vector has a different member name.
	auto& lines = textedit->lines;

	if (lines.empty()) {
		return;
	}

	// -------------------------------------------------------------------------
	// Marking
	// -------------------------------------------------------------------------

	// For now the reason is deliberately discarded, but every mark has one.
	//
	// Later this can become something like:
	//
	// lines[lineIndex].diagnostics.push_back(
	//     LineDiagnostic{ ..., reason }
	// );
	//
	auto markLine = [&](size_t lineIndex, const std::string& reason) {
		if (lineIndex >= lines.size()) {
			return;
		}

		lines[lineIndex].isMarked = true;
		lines[lineIndex].markComment = MST::toMonoString(reason);
	};

	textedit->clearMarks();
	
	std::vector<std::string> source;
	source.reserve(lines.size());
	
	for (const Line& line : lines) {
		source.push_back(MST::toString(line.line_text));
	}
	
	// -------------------------------------------------------------------------
	// Small string helpers
	// -------------------------------------------------------------------------

	auto ltrim = [](const std::string& s) -> std::string {
		size_t start = 0;

		while (
			start < s.size() &&
			std::isspace(static_cast<unsigned char>(s[start]))
		) {
			start++;
		}

		return s.substr(start);
	};

	auto rtrim = [](const std::string& s) -> std::string {
		size_t end = s.size();

		while (
			end > 0 &&
			std::isspace(static_cast<unsigned char>(s[end - 1]))
		) {
			end--;
		}

		return s.substr(0, end);
	};

	auto trim = [&](const std::string& s) -> std::string {
		return rtrim(ltrim(s));
	};

	auto indentation = [](const std::string& s) -> int {
		int indent = 0;

		for (char c : s) {
			if (c == ' ') {
				indent++;
			}
			else if (c == '\t') {
				// Doesn't have to exactly match Python's interpretation.
				// This is only being used to compare indentation levels.
				indent += 4;
			}
			else {
				break;
			}
		}

		return indent;
	};

	auto startsWith = [](const std::string& s, const std::string& prefix) -> bool {
		return
			s.size() >= prefix.size() &&
			s.compare(0, prefix.size(), prefix) == 0;
	};

	auto startsWithDocstring = [&](const std::string& s) -> bool {
		std::string t = ltrim(s);

		return
			startsWith(t, "\"\"\"") ||
			startsWith(t, "'''");
	};

	// Finds a # which is actually a comment, rather than a # inside
	// "a string like this #".
	auto commentPosition = [](const std::string& s) -> size_t {
		char quote = '\0';
		bool escaped = false;

		for (size_t i = 0; i < s.size(); i++) {
			char c = s[i];

			if (quote != '\0') {
				if (escaped) {
					escaped = false;
					continue;
				}

				if (c == '\\') {
					escaped = true;
					continue;
				}

				if (c == quote) {
					quote = '\0';
				}

				continue;
			}

			if (c == '\'' || c == '"') {
				quote = c;
				continue;
			}

			if (c == '#') {
				return i;
			}
		}

		return std::string::npos;
	};

	auto removeComment = [&](const std::string& s) -> std::string {
		size_t pos = commentPosition(s);

		if (pos == std::string::npos) {
			return s;
		}

		return s.substr(0, pos);
	};

	auto isBlankOrComment = [&](const std::string& s) -> bool {
		std::string t = trim(s);

		return t.empty() || startsWith(t, "#");
	};

	auto isAllCapsIdentifier = [](const std::string& name) -> bool {
		if (name.empty()) {
			return false;
		}

		bool hasLetter = false;

		for (char c : name) {
			unsigned char uc = static_cast<unsigned char>(c);

			if (std::isalpha(uc)) {
				hasLetter = true;

				if (!std::isupper(uc)) {
					return false;
				}
			}
			else if (!std::isdigit(uc) && c != '_') {
				return false;
			}
		}

		return hasLetter;
	};

	auto looksLikeCamelCaseClass = [](const std::string& name) -> bool {
		if (name.empty()) {
			return false;
		}

		if (!std::isupper(static_cast<unsigned char>(name[0]))) {
			return false;
		}

		// Course requirement calls for CamelCase class names.
		if (name.find('_') != std::string::npos) {
			return false;
		}

		return true;
	};

	// Functions
	
	struct ParsedFunctionDefinition {
		bool valid = false;
		size_t endLine = 0;
		std::string name;
		std::string arguments;
	};
	
	auto parseFunctionDefinition = [&](size_t startLine) -> ParsedFunctionDefinition {
		ParsedFunctionDefinition result;
	
		if (startLine >= source.size()) {
			return result;
		}
	
		std::string first = trim(removeComment(source[startLine]));
	
		// Must begin with "def ".
		if (!startsWith(first, "def ")) {
			return result;
		}
	
		// Extract function name.
		size_t nameStart = 4;
		size_t parenPos = first.find('(', nameStart);
	
		if (parenPos == std::string::npos) {
			return result;
		}
	
		std::string name = trim(
			first.substr(nameStart, parenPos - nameStart)
		);
	
		if (name.empty()) {
			return result;
		}
	
		// Basic identifier check.
		if (
			!(
				std::isalpha(static_cast<unsigned char>(name[0])) ||
				name[0] == '_'
			)
		) {
			return result;
		}
	
		for (char c : name) {
			if (
				!std::isalnum(static_cast<unsigned char>(c)) &&
				c != '_'
			) {
				return result;
			}
		}
	
		std::string arguments;
	
		int parenDepth = 0;
		bool foundOpeningParen = false;
		bool finishedArguments = false;
	
		char quote = '\0';
		bool escaped = false;
	
		for (size_t lineIndex = startLine; lineIndex < source.size(); lineIndex++) {
			std::string code = removeComment(source[lineIndex]);
	
			size_t charStart = 0;
	
			if (lineIndex == startLine) {
				charStart = code.find('(');
	
				if (charStart == std::string::npos) {
					return result;
				}
			}
	
			for (size_t j = charStart; j < code.size(); j++) {
				char c = code[j];
	
				if (quote != '\0') {
					if (escaped) {
						escaped = false;
					}
					else if (c == '\\') {
						escaped = true;
					}
					else if (c == quote) {
						quote = '\0';
					}
	
					if (foundOpeningParen && parenDepth > 0) {
						arguments += c;
					}
	
					continue;
				}
	
				if (c == '\'' || c == '"') {
					quote = c;
	
					if (foundOpeningParen && parenDepth > 0) {
						arguments += c;
					}
	
					continue;
				}
	
				if (c == '(') {
					if (!foundOpeningParen) {
						foundOpeningParen = true;
						parenDepth = 1;
						continue;
					}
	
					parenDepth++;
					arguments += c;
					continue;
				}
	
				if (c == ')') {
					if (!foundOpeningParen) {
						continue;
					}
	
					parenDepth--;
	
					if (parenDepth == 0) {
						finishedArguments = true;
	
						result.valid = true;
						result.endLine = lineIndex;
						result.name = name;
						result.arguments = trim(arguments);
	
						return result;
					}
	
					arguments += c;
					continue;
				}
	
				if (foundOpeningParen && parenDepth > 0) {
					arguments += c;
				}
			}
	
			// Preserve separation between lines so things don't accidentally
			// become one token.
			if (foundOpeningParen && !finishedArguments) {
				arguments += " ";
			}
		}
	
		return result;
	};
	
	// -------------------------------------------------------------------------
	// Regexes for the very simple structural cases
	// -------------------------------------------------------------------------

	const std::regex classRegex(
		R"(^class\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s*\([^)]*\))?\s*:\s*$)"
	);

	const std::regex assignmentRegex(
		R"(^([A-Za-z_][A-Za-z0-9_]*)\s*(?::[^=]+)?\s*=(?:[^=].*|$))"
	);

	struct Definition {
		size_t line = 0;
		size_t headerEndLine = 0;
	
		int indent = 0;
		std::string name;
		std::string arguments;
		bool isClass = false;
	};

	std::vector<Definition> functions;
	std::vector<Definition> classes;

	// -------------------------------------------------------------------------
	// Argument counting
	// -------------------------------------------------------------------------

	// Counts:
	//
	//     a, b, thing=(1, 2), another={"x": 4}
	//
	// as four arguments instead of six.
	auto countArguments = [&](const std::string& arguments) -> int {
		std::string args = trim(arguments);

		if (args.empty()) {
			return 0;
		}

		int count = 1;
		int nesting = 0;

		char quote = '\0';
		bool escaped = false;

		for (size_t i = 0; i < args.size(); i++) {
			char c = args[i];

			if (quote != '\0') {
				if (escaped) {
					escaped = false;
				}
				else if (c == '\\') {
					escaped = true;
				}
				else if (c == quote) {
					quote = '\0';
				}

				continue;
			}

			if (c == '\'' || c == '"') {
				quote = c;
				continue;
			}

			if (c == '(' || c == '[' || c == '{') {
				nesting++;
			}
			else if (c == ')' || c == ']' || c == '}') {
				nesting--;
			}
			else if (c == ',' && nesting == 0) {
				count++;
			}
		}

		return count;
	};

	// -------------------------------------------------------------------------
	// Pass 1: comments and obvious line-level problems
	// -------------------------------------------------------------------------

	for (size_t i = 0; i < source.size(); i++) {
		const std::string& raw = source[i];
		std::string t = trim(raw);

		if (t.empty()) {
			continue;
		}

		size_t commentPos = commentPosition(raw);

		if (commentPos != std::string::npos) {
			std::string before = raw.substr(0, commentPos);

			if (trim(before).empty()) {
				// Full-line/block comments are supposed to be "# blah".
				//
				// Ignore common shebang and encoding forms.
				std::string comment = ltrim(raw);

				if (
					comment != "#" &&
					!startsWith(comment, "# ") &&
					!startsWith(comment, "#!") &&
					!startsWith(comment, "# -*-")
				) {
					markLine(
						i,
						"Block comments should normally begin with '# '."
					);
				}
			}
			else {
				// Inline comments are supposed to have >= 2 spaces before #.
				size_t spaces = 0;
				size_t p = commentPos;

				while (p > 0 && raw[p - 1] == ' ') {
					spaces++;
					p--;
				}

				if (spaces < 2) {
					markLine(
						i,
						"Inline comments should be separated from code by at least two spaces."
					);
				}
			}
		}
	}

	// -------------------------------------------------------------------------
	// Pass 2: definitions, imports, globals, class names, nested functions
	// -------------------------------------------------------------------------

	enum class ScopeType {
		Function,
		Class
	};

	struct Scope {
		int indent;
		ScopeType type;
	};

	std::vector<Scope> scopeStack;

	bool hasParameterlessMain = false;

	for (size_t i = 0; i < source.size(); i++) {
		std::string code = trim(removeComment(source[i]));

		if (code.empty()) {
			continue;
		}

		int indent = indentation(source[i]);

		while (
			!scopeStack.empty() &&
			indent <= scopeStack.back().indent
		) {
			scopeStack.pop_back();
		}

		// Imports must be global.
		if (
			startsWith(code, "import ") ||
			startsWith(code, "from ")
		) {
			if (indent != 0) {
				markLine(
					i,
					"Import statements should be defined globally."
				);
			}
		}

		std::smatch match;
		
		if (std::regex_match(code, match, classRegex)) {
			std::string className = match[1].str();
		
			classes.push_back({
				i,
				i,
				indent,
				className,
				"",
				true
			});
		
			if (!looksLikeCamelCaseClass(className)) {
				markLine(
					i,
					"Class names should use CamelCaseNaming."
				);
			}
		
			scopeStack.push_back({
				indent,
				ScopeType::Class
			});
		
			continue;
		}
		
		// ---------------------------------------------------------------------
		// Function definition -- supports multiline definitions
		// ---------------------------------------------------------------------
		
		ParsedFunctionDefinition parsedFunction =
			parseFunctionDefinition(i);
		
		if (parsedFunction.valid) {
			std::string functionName = parsedFunction.name;
			std::string arguments = parsedFunction.arguments;
		
			bool insideFunction =
				!scopeStack.empty() &&
				scopeStack.back().type == ScopeType::Function;
		
			if (insideFunction) {
				markLine(
					i,
					"User-defined functions should not be nested inside other functions."
				);
			}
		
			int argumentCount = countArguments(arguments);
		
			if (argumentCount > 5) {
				markLine(
					i,
					"User-defined functions should have five or fewer arguments."
				);
			}
		
			if (
				indent == 0 &&
				functionName == "main" &&
				trim(arguments).empty()
			) {
				hasParameterlessMain = true;
			}
		
			functions.push_back({
				i,
				parsedFunction.endLine,
				indent,
				functionName,
				arguments,
				false
			});
		
			scopeStack.push_back({
				indent,
				ScopeType::Function
			});
		
			// Skip the remaining physical lines making up the declaration.
			//
			// For:
			//
			// def thing(
			//     a,
			//     b,
			// ):
			//
			// we don't want "a", "b", or ")" processed as ordinary code.
			i = parsedFunction.endLine;
		
			continue;
		}

		// Simple top-level assignment:
		//
		//     thing = ...
		//
		// is considered a suspicious global unless THING is uppercase.
		if (indent == 0 && std::regex_search(code, match, assignmentRegex)) {
			std::string identifier = match[1].str();

			if (!isAllCapsIdentifier(identifier)) {
				markLine(
					i,
					"Non-constant global identifiers should be initialized inside a function."
				);
			}
		}
	}

	// -------------------------------------------------------------------------
	// Missing parameterless main()
	// -------------------------------------------------------------------------

	if (!hasParameterlessMain) {
		// There is no definition line to mark, so mark the first meaningful line.
		for (size_t i = 0; i < source.size(); i++) {
			if (!isBlankOrComment(source[i])) {
				markLine(
					i,
					"Program does not appear to contain a parameterless main() function."
				);
				break;
			}
		}
	}

	// -------------------------------------------------------------------------
	// Module/program docstring
	// -------------------------------------------------------------------------

	for (size_t i = 0; i < source.size(); i++) {
		if (isBlankOrComment(source[i])) {
			continue;
		}

		if (!startsWithDocstring(source[i])) {
			markLine(
				i,
				"Program should begin with a program docstring."
			);
		}

		break;
	}

	// -------------------------------------------------------------------------
	// Function/class docstrings
	// -------------------------------------------------------------------------

	auto checkDefinitionDocstring = [&](const Definition& def) {
		for (size_t j = def.headerEndLine + 1; j < source.size(); j++) {
			if (trim(source[j]).empty()) {
				continue;
			}

			std::string t = trim(source[j]);

			// Allow comments before the docstring for purposes of this heuristic.
			if (startsWith(t, "#")) {
				continue;
			}

			int childIndent = indentation(source[j]);

			if (childIndent <= def.indent) {
				markLine(
					def.line,
					def.isClass
						? "Class appears to be missing a class docstring."
						: "Function appears to be missing a function docstring."
				);

				return;
			}

			if (!startsWithDocstring(source[j])) {
				markLine(
					def.line,
					def.isClass
						? "Class appears to be missing a class docstring."
						: "Function appears to be missing a function docstring."
				);
			}

			return;
		}

		// Definition at EOF with no body/docstring.
		markLine(
			def.line,
			def.isClass
				? "Class appears to be missing a class docstring."
				: "Function appears to be missing a function docstring."
		);
	};

	for (const Definition& def : functions) {
		checkDefinitionDocstring(def);
	}

	for (const Definition& def : classes) {
		checkDefinitionDocstring(def);
	}

	// -------------------------------------------------------------------------
	// Function length -- approximate the course's "12 code lines" rule
	// -------------------------------------------------------------------------

	for (const Definition& function : functions) {
		int statementLines = 0;

		bool foundFirstBodyStatement = false;
		bool inLeadingDocstring = false;
		std::string docstringDelimiter;

		for (
			size_t j = function.headerEndLine + 1;
			j < source.size();
			j++
		) {
			std::string rawTrimmed = trim(source[j]);

			if (rawTrimmed.empty()) {
				continue;
			}

			int currentIndent = indentation(source[j]);

			// Once indentation returns to the definition's level, the
			// function is over.
			if (
				!inLeadingDocstring &&
				currentIndent <= function.indent
			) {
				break;
			}

			if (inLeadingDocstring) {
				size_t closing =
					source[j].find(docstringDelimiter);

				if (closing != std::string::npos) {
					inLeadingDocstring = false;
				}

				continue;
			}

			if (startsWith(rawTrimmed, "#")) {
				continue;
			}

			if (!foundFirstBodyStatement) {
				foundFirstBodyStatement = true;

				if (startsWith(rawTrimmed, "\"\"\"")) {
					docstringDelimiter = "\"\"\"";

					if (
						rawTrimmed.find(
							"\"\"\"",
							3
						) == std::string::npos
					) {
						inLeadingDocstring = true;
					}

					continue;
				}

				if (startsWith(rawTrimmed, "'''")) {
					docstringDelimiter = "'''";

					if (
						rawTrimmed.find(
							"'''",
							3
						) == std::string::npos
					) {
						inLeadingDocstring = true;
					}

					continue;
				}
			}

			// This intentionally counts physical code lines rather than
			// constructing a Python AST, which is reasonably close to the
			// rubric's wording.
			statementLines++;
		}

		if (statementLines > 12) {
			markLine(
				function.line,
				"Function appears to contain more than twelve code statements/lines."
			);
		}
	}

	// -------------------------------------------------------------------------
	// Adjacent duplicate lines
	// -------------------------------------------------------------------------

	std::string previousCode;
	size_t previousLine = 0;
	bool havePrevious = false;

	for (size_t i = 0; i < source.size(); i++) {
		std::string code = trim(removeComment(source[i]));

		if (code.empty()) {
			havePrevious = false;
			continue;
		}

		if (havePrevious && code == previousCode) {
			// Mark both, since either may be the line the user wants to inspect.
			markLine(
				previousLine,
				"Adjacent duplicate code may be replaceable by repetition."
			);

			markLine(
				i,
				"Adjacent duplicate code may be replaceable by repetition."
			);
		}

		previousCode = code;
		previousLine = i;
		havePrevious = true;
	}

	// -------------------------------------------------------------------------
	// Repeated numeric literals
	//
	// The rubric exempts:
	//     0, 1, 2, -1, 0.0
	//
	// Rather than attempting to fully tokenize Python, we recognize ordinary
	// decimal numeric literals while ignoring strings and comments.
	// -------------------------------------------------------------------------

	std::unordered_map<std::string, std::vector<size_t>> numericLiteralLines;

	bool insideTripleString = false;
	char tripleQuoteCharacter = '\0';

	for (size_t lineIndex = 0; lineIndex < source.size(); lineIndex++) {
		const std::string& s = source[lineIndex];

		char quote = '\0';
		bool escaped = false;

		for (size_t i = 0; i < s.size();) {
			// We entered a """ or ''' on an earlier line.
			if (insideTripleString) {
				if (
					i + 2 < s.size() &&
					s[i] == tripleQuoteCharacter &&
					s[i + 1] == tripleQuoteCharacter &&
					s[i + 2] == tripleQuoteCharacter
				) {
					insideTripleString = false;
					tripleQuoteCharacter = '\0';
					i += 3;
				}
				else {
					i++;
				}

				continue;
			}

			char c = s[i];

			if (quote != '\0') {
				if (escaped) {
					escaped = false;
					i++;
					continue;
				}

				if (c == '\\') {
					escaped = true;
					i++;
					continue;
				}

				if (c == quote) {
					quote = '\0';
				}

				i++;
				continue;
			}

			if (c == '#') {
				break;
			}

			if (
				(c == '\'' || c == '"') &&
				i + 2 < s.size() &&
				s[i + 1] == c &&
				s[i + 2] == c
			) {
				insideTripleString = true;
				tripleQuoteCharacter = c;
				i += 3;
				continue;
			}

			if (c == '\'' || c == '"') {
				quote = c;
				i++;
				continue;
			}

			bool startsNumber =
				std::isdigit(static_cast<unsigned char>(c));

			if (
				c == '-' &&
				i + 1 < s.size() &&
				std::isdigit(
					static_cast<unsigned char>(s[i + 1])
				)
			) {
				startsNumber = true;
			}

			if (!startsNumber) {
				i++;
				continue;
			}

			// Don't treat "foo2" as the literal 2.
			if (i > 0) {
				char previous = s[i - 1];

				if (
					std::isalnum(
						static_cast<unsigned char>(previous)
					) ||
					previous == '_'
				) {
					i++;
					continue;
				}
			}

			size_t start = i;

			if (s[i] == '-') {
				i++;
			}

			while (
				i < s.size() &&
				std::isdigit(static_cast<unsigned char>(s[i]))
			) {
				i++;
			}

			if (
				i < s.size() &&
				s[i] == '.'
			) {
				i++;

				while (
					i < s.size() &&
					std::isdigit(
						static_cast<unsigned char>(s[i])
					)
				) {
					i++;
				}
			}

			if (
				i < s.size() &&
				(s[i] == 'e' || s[i] == 'E')
			) {
				size_t exponentStart = i;
				i++;

				if (
					i < s.size() &&
					(s[i] == '+' || s[i] == '-')
				) {
					i++;
				}

				size_t digitStart = i;

				while (
					i < s.size() &&
					std::isdigit(
						static_cast<unsigned char>(s[i])
					)
				) {
					i++;
				}

				if (digitStart == i) {
					// Invalid exponent; leave the e/E alone.
					i = exponentStart;
				}
			}

			std::string literal = s.substr(start, i - start);

			if (
				literal == "0" ||
				literal == "1" ||
				literal == "2" ||
				literal == "-1" ||
				literal == "0.0"
			) {
				continue;
			}

			numericLiteralLines[literal].push_back(lineIndex);
		}
	}

	for (const auto& [literal, occurrences] : numericLiteralLines) {
		if (occurrences.size() <= 1) {
			continue;
		}

		std::unordered_set<size_t> markedLines;

		for (size_t lineIndex : occurrences) {
			if (!markedLines.insert(lineIndex).second) {
				continue;
			}

			markLine(
				lineIndex,
				"Numeric literal '" + literal +
				"' appears more than once; consider assigning it to a named value."
			);
		}
	}
}