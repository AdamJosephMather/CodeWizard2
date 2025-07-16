#include "editor.h"
#include "text_renderer.h"
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
	
	tab_bar->erasing_tab = [&](TabInfo info){
		auto it = editors.find(info.id);
		if (it != editors.end()) {
			it->second->request_close([&](Widget* w){ // wait for it to delete itself
				App::RemoveWidgetFromParent(w);
				delete editors[it->first];
				editors.erase(it->first);
			});
		}
	};
	
	tabid = 0;
	
	createNew(nullptr);
}

void Editor::tabinfoclicked(TabInfo info) {
	for (auto it : editors) {
		if (it.first == info.id && it.second->parent != this){
			App::MoveWidget(it.second, this);
		}else if (it.first != info.id && it.second->parent == this) {
			App::RemoveWidgetFromParent(it.second);
		}
	}
	
	if (CodeEdit* ce = dynamic_cast<CodeEdit*>( editors[info.id] )){
		App::setActiveLeafNode(ce->textedit);
	}
	
	for (auto it : editors) {
		if (it.first == info.id) {
			it.second->show();
		}else {
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
			ce->file = fn;
			ce->openFile();
		}
		if (auto iv = dynamic_cast<ImageView*>(edtr)){
			iv->file = fn;
			iv->openFile();
		}
	}
}

void Editor::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.darker_background_color);
	
	auto txt = icu::UnicodeString::fromUTF8("Editor Panel");
	TextRenderer::draw_text(t_x+t_w/2-TextRenderer::get_text_width(txt.length())/2, t_y+10+tab_bar->t_h, txt, AllOneColor(App::theme.main_text_color, txt.length()));
	
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
	tab_bar->removeTab(file_id);
	
	editors[file_id]->request_close([&](Widget* w){ // wait for it to delete itself
		App::RemoveWidgetFromParent(w);
		delete editors[file_id];
		editors.erase(file_id);
	});
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
			f->ondisk = true;
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
		if (!ce->file || ce->file->ondisk == false) {
			if (ce->textedit->getFullText() == "") { // empty file only in memory - let's remove it.
				closeFile(tabbeforetab);
			}
		}
	}
	
	if (auto ce = dynamic_cast<CodeEdit*>(editors[tab_bar->selected_id])) { // we do this cast because not all widgets have FileInfo file; variables.
		App::setActiveLeafNode(ce->textedit);
	}
	
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
	
	if (this == App::activeEditor) {
		if (key == GLFW_KEY_O && action == GLFW_PRESS && holding_control) {
			fileOpenRequested(nullptr);
			return true;
		}else if (key == GLFW_KEY_N && action == GLFW_PRESS && holding_control) {
			createNew(nullptr);
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
	}
	return icu::UnicodeString::fromUTF8("");
}

Widget* Editor::fileOpen(std::string fname) { // this is a widget function to find an editor which has a filename already open (let's not re-open it, no reason to.)
	for (auto itm : tab_bar->tabs_list) {
		if (auto te = dynamic_cast<CodeEdit*>(editors[itm.id])) {
			if (te->file && areSameFile(te->file->filepath, fname)) {
				return this;
			}
		}
	}
	return nullptr;
}

Widget* Editor::getFirstEditor() {
	return this;
}