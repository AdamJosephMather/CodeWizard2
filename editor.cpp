#include "editor.h"
#include "application.h"
#include "codeedit.h"
#include "tinyfiledialogs.h"
#include "imageview.h"

Editor::Editor(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Editor");
	
	tab_bar = new Tabs(this);
	
	tab_bar->tab_clicked_callback = [&](TabInfo info){
		tabinfoclicked(info);
	};
	
	tab_bar->all_tabs_closed_callback = [&](){
		createNew(nullptr); // let's fix that...
	};
	
	tab_bar->add_new_tab_callback = [&](){
		createNew(nullptr); // can do...
	};
	
	tab_bar->erasing_tab = [&](TabInfo info) {
		std::lock_guard<std::mutex> lock(App::canMakeChanges);
		
		auto it = editors.find(info.id);
		if (it != editors.end()) {
			Widget* w = it->second;
			
			App::RemoveWidgetFromParent(w);
			editors.erase(info.id);
			
			App::deleteWidget(w);
		}
	};
	
	tabid = 0;
	
	createNew(nullptr);
}

void Editor::executeAction(WidgetActionType typ) {
	for (auto it : editors) { // must send this to all of them even if they're not in our children list.
		it.second->executeAction(typ);
	}
	
	tab_bar->executeAction(typ);
}

void Editor::tabinfoclicked(TabInfo info) {
	for (auto it : editors) {
		if (it.first == info.id){
			if (it.second->parent != this){
				App::MoveWidget(it.second, this);
			}
			it.second->show();
			
			if (auto ce = dynamic_cast<CodeEdit*>(it.second)) {
				App::setActiveLeafNode(ce->textedit);
			}else if (auto iv = dynamic_cast<ImageView*>(it.second)) {
				App::setActiveLeafNode(iv);
			}
		}else if (it.first != info.id && it.second->parent == this) {
			App::RemoveWidgetFromParent(it.second);
			it.second->hide();
		}
	}
}

void Editor::save() {
	for (auto it : editors) { // not all of these are explicitly children so the default doesn't get it.
		it.second->save();
	}
}

bool Editor::is_image(std::string path) {
	return std::string(path).find(".png") != std::string::npos || 
	   std::string(path).find(".jpg") != std::string::npos || 
	   std::string(path).find(".jpeg") != std::string::npos || 
	   std::string(path).find(".bmp") != std::string::npos || 
	   std::string(path).find(".gif") != std::string::npos;
}

void Editor::createNew(FileInfo* fn) {
	auto ti = TabInfo();
	
	if (fn) {
		ti.title = icu::UnicodeString::fromUTF8(fn->filename);
	}else{
		ti.title = icu::UnicodeString::fromUTF8("Untitled");
	}
	
	ti.id = tabid;
	
	tab_bar->addTab(ti);
	tabid ++;
	
	if (fn && is_image(fn->filepath)) {
		ImageView* imgv = new ImageView(this);
		editors[ti.id] = imgv;
	}else{
		CodeEdit* edtr = new CodeEdit(this, ti.id, [&](Widget* edtr){
			if (App::settings->getValue("use_tabs", true)) {
				edtr->t_y = t_y+tab_bar->t_h;
				edtr->t_h = t_h-tab_bar->t_h;
			}else{
				edtr->t_y = t_y;
				edtr->t_h = t_h;
			}
			
			edtr->t_x = t_x;
			edtr->t_w = t_w;
		}, [&](Widget* widget, FileInfo* info) {
			TabInfo tab;
			CodeEdit* edtr = dynamic_cast<CodeEdit*>(widget);
			tab.id = edtr->TABID;
			tab.title = icu::UnicodeString::fromUTF8(info->filename);
			tab_bar->updateTab(tab);
		});
		
		editors[ti.id] = edtr;
	}
	
	auto edtr = editors[ti.id];
	
	for (auto it : editors) {
		if (it.first == ti.id) {
			it.second->show();
		}else{
			it.second->hide();
		}
	}
	
	tab_bar->selected_id = ti.id;
	
	if (auto ce = dynamic_cast<CodeEdit*>(edtr)){
		App::setActiveLeafNode(ce->textedit);
	}else{
		App::setActiveLeafNode(edtr);
	}
	
	for (auto it : editors) {
		if (it.first == ti.id && it.second->parent != this){
			App::MoveWidget(it.second, this);
		}else if (it.first != ti.id && it.second->parent == this) {
			App::RemoveWidgetFromParent(it.second);
		}
	}
	
	if (fn) {
		if (auto ce = dynamic_cast<CodeEdit*>(edtr)){
			ce->openFile(fn);
		}else if (auto iv = dynamic_cast<ImageView*>(edtr)){
			iv->openFile(fn);
		}
	}
}

void Editor::render() {
	auto e = editors[tab_bar->selected_id];
	
	if (App::settings->getValue("use_tabs", true)){
		App::runWithSKIZ(tab_bar->t_x, tab_bar->t_y, tab_bar->t_w, tab_bar->t_h, [&](){
			tab_bar->render();
		});
	}
	
	App::runWithSKIZ(e->t_x, e->t_y, e->t_w, e->t_h, [&](){
		e->render();
	});
}

void Editor::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	if (App::settings->getValue("use_tabs", true)) {
		if (!tab_bar->is_visible) {
			tab_bar->show();
		}
		tab_bar->position(x, y, w, h);
		
		editors[tab_bar->selected_id]->position(x, y+tab_bar->t_h, w, h-tab_bar->t_h);
	}else{
		if (tab_bar->is_visible) {
			tab_bar->hide();
		}
		
		editors[tab_bar->selected_id]->position(x, y, w, h);
	}
}

void Editor::closeFile(int file_id) {
	tab_bar->removeTab(file_id); // this triggers a thing in the tabbar which calls back to here to delete it (:
}

void Editor::fileOpenRequested(FileInfo* f, int lns, int chrs, int ln, int chr) {
	if (!f) {
		const char * fp = tinyfd_openFileDialog(
			"Select a file",    // dialog title
			"",                 // default path and filename
			0, NULL, NULL,      // filter count and filters
			0                   // allow multiple selections (0 = no)
		);
		
		if (fp) {
			std::string filePath(fp);
			
			std::filesystem::path fullPath = filePath;
			std::string filename = fullPath.filename().string();
			
			f = new FileInfo();
			f->filepath = filePath;
			f->filename = filename;
		}else{
			App::commandUnfocused();
			return;
		}
	}
	
	for (auto itm : tab_bar->tabs_list) {
		if (auto te = dynamic_cast<CodeEdit*>(editors[itm.id])) {
			if (te->file && areSameFile(te->file->filepath, f->filepath)) {
				tab_bar->selected_id = itm.id;
				tabinfoclicked(itm);
				moveto(lns, chrs, ln, chr);
				App::commandUnfocused();
				return;
			}
		}else if (auto iv = dynamic_cast<ImageView*>(editors[itm.id])) {
			if (iv->file && areSameFile(iv->file->filepath, f->filepath)) {
				tab_bar->selected_id = itm.id;
				tabinfoclicked(itm);
				moveto(lns, chrs, ln, chr);
				App::commandUnfocused();
				return;
			}
		}
	}
	
	// here we know it's not here
	
	if (auto othereditor = dynamic_cast<Editor*>(App::rootelement->fileOpen(f->filepath))) {
		othereditor->fileOpenRequested(f, lns, chrs, ln, chr);
		return;
	}
	
	// here we know no other editors have the file either.
	
	int tabbeforetab = tab_bar->selected_id;
	
	createNew(f);
	
	if (auto ce = dynamic_cast<CodeEdit*>(editors[tabbeforetab])) { // we do this cast because not all widgets have FileInfo file; variables.
		if (!ce->file) {
			if (ce->textedit->getFullText() == "") { // empty file only in memory - let's remove it.
				closeFile(tabbeforetab);
			}
		}
	}
	
//	if (auto ce = dynamic_cast<CodeEdit*>(editors[tab_bar->selected_id])) { // we do this cast because not all widgets have FileInfo file; variables.
//		App::setActiveLeafNode(ce->textedit);
//	}
	
	moveto(lns, chrs, ln, chr);
	App::commandUnfocused();
}

void Editor::moveto(int lns, int chrs, int ln, int chr) {
	if (auto ce = dynamic_cast<CodeEdit*>(editors[tab_bar->selected_id])) {
		if (ln < 0 || ln >= ce->textedit->lines.size()) {
			return;
		}
		
		if (lns < 0 || lns >= ce->textedit->lines.size()) {
			return;
		}
		
		if (chr < 0 || ce->textedit->lines[ln].line_text.length() < chr) {
			return;
		}
		
		if (chrs < 0 || ce->textedit->lines[lns].line_text.length() < chrs) {
			return;
		}
		
		ce->textedit->cursors = { { lns, chrs, ln, chr, chr } };
		ce->textedit->tryingToEnsureCursorPos = true;
	}
}

bool Editor::on_key_event(int key, int scancode, int action, int mods) {
	// here we will detect ctrl+o, maybe hotkeys for tabs? ctrl+q?
	
	bool holding_control = (mods & GLFW_MOD_CONTROL) != 0;
	bool holding_shift   = (mods & GLFW_MOD_SHIFT) != 0;
	
	if (this == App::activeEditor) {
		if (key == GLFW_KEY_O && action == GLFW_PRESS && holding_control) {
			fileOpenRequested(nullptr);
			return true;
		}else if (key == GLFW_KEY_N && action == GLFW_PRESS && holding_control) {
			createNew(nullptr);
			return true;
		}else if (key == GLFW_KEY_W && action == GLFW_PRESS && holding_control) {
			closeFile(tab_bar->selected_id);
			return true;
		}else if (key == GLFW_KEY_TAB && action == GLFW_PRESS && holding_control && holding_shift) {
			tab_bar->nextTab();
			return true;
		}
	}
	
	return Widget::on_key_event(key, scancode, action, mods);
}

icu::UnicodeString Editor::getPaletteName() {
	if (auto ce = dynamic_cast<CodeEdit*>(editors[tab_bar->selected_id])) {
		if (!ce->file || ce->file->filename == "") {
			return icu::UnicodeString::fromUTF8("Untitled");
		}
		return icu::UnicodeString::fromUTF8(ce->file->filename);
	}else if (auto iv = dynamic_cast<ImageView*>(editors[tab_bar->selected_id])) {
		if (!iv->file || iv->file->filename == "") {
			return icu::UnicodeString::fromUTF8("Untitled Image");
		}
		return icu::UnicodeString::fromUTF8(iv->file->filename);
	}
	return icu::UnicodeString::fromUTF8("");
}

Widget* Editor::fileOpen(std::string fname) { // this is a widget function to find an editor which has a filename already open (let's not re-open it, no reason to.)
	for (auto itm : tab_bar->tabs_list) {
		if (auto te = dynamic_cast<CodeEdit*>(editors[itm.id])) {
			if (te->file && areSameFile(te->file->filepath, fname)) {
				return this;
			}
		}else if (auto iv = dynamic_cast<ImageView*>(editors[itm.id])) {
			if (iv->file && areSameFile(iv->file->filepath, fname)) {
				return this;
			}
		}
	}
	return nullptr;
}

Widget* Editor::getFirstEditor() {
	return this;
}

std::vector<std::vector<std::string>> Editor::getOpenFiles(bool includeText) {
	std::vector<std::vector<std::string>> out = {};
	
	for (auto itm : tab_bar->tabs_list) {
		if (auto te = dynamic_cast<CodeEdit*>(editors[itm.id])) {
			std::vector<std::string> res;
			if (te->file) {
				res = {te->file->filename, te->file->filepath, "TEXT"};
			}else{
				res = {"Untitled", "", "TEXT"};
			}
			
			if (includeText) { // defaults to false
				icu::UnicodeString fulltext = te->textedit->getFullText();
				std::string text;
				fulltext.toUTF8String(text);
				res.push_back(text);
			}
			
			out.push_back(res);
		}else if(auto te = dynamic_cast<ImageView*>(editors[itm.id])){
			if (te->file) {
				out.push_back({te->file->filename, te->file->filepath, "IMAGE"});
			}else{
				out.push_back({"Untitled Image", "", "IMAGE"});
			}
		}
	}
	
	return out;
}

int Editor::openUnnamedFile(int count) {
	for (auto itm : tab_bar->tabs_list) {
		if (auto te = dynamic_cast<CodeEdit*>(editors[itm.id])) {
			if (!te->file) {
				if (count == 0) {
					tab_bar->selected_id = itm.id;
					tabinfoclicked(itm);
					App::commandUnfocused();
					return count - 1;
				}
				count --;
			}
		}else if(auto te = dynamic_cast<ImageView*>(editors[itm.id])){
			if (!te->file) {
				if (count == 0) {
					tab_bar->selected_id = itm.id;
					tabinfoclicked(itm);
					App::commandUnfocused();
					return count - 1;
				}
				count --;
			}
		}
	}
	return count;
}