#include "terminalwidgettabbed.h"
#include "application.h"
#include "terminalwidget.h"
#include "text_renderer.h"

TerminalWidgetTabbed::TerminalWidgetTabbed(Widget* parent)  : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("Terminal Tabbed");
	
	tab_bar = new Tabs(this);
	
	tab_bar->tab_clicked_callback = [&](TabInfo info) {
		tabinfoclicked(info);
	};
	
	tab_bar->all_tabs_closed_callback = [&]() {
		createNew();
	};
	
	tab_bar->add_new_tab_callback = [&]() {
		createNew();
	};
	
	tab_bar->erasing_tab = [&](TabInfo info) {
		std::lock_guard<std::mutex> lock(App::canMakeChanges);
		
		auto it = terminals.find(info.id);
		
		if (it != terminals.end()) {
			auto todel = it->second;
			
			terminals.erase(info.id);
			App::deleteWidget(todel);
		}
	};
	
	tabid = 0;
	
	createNew();
}

Widget* TerminalWidgetTabbed::findTerminal() {
	return terminals[tab_bar->selected_id];
}

void TerminalWidgetTabbed::position(int x, int y, int w, int h) {
	t_x = x;
	t_y = y;
	t_w = w;
	t_h = h;
	
	tab_bar->position(x, y, w, h);
	terminals[tab_bar->selected_id]->position(x, y+tab_bar->t_h, w, h-tab_bar->t_h);
}

void TerminalWidgetTabbed::render() {
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.darker_background_color);
	App::runWithSKIZ(tab_bar->t_x, tab_bar->t_y, tab_bar->t_w, tab_bar->t_h, [&](){
		tab_bar->render();
	});
	
	auto t = terminals[tab_bar->selected_id];
	App::runWithSKIZ(t->t_x, t->t_y, t->t_w, t->t_h, [&](){
		t->render();
	});
}

void TerminalWidgetTabbed::createNew() {
	auto ti = TabInfo();
	
	ti.title = icu::UnicodeString::fromUTF8(App::settings->getValue("terminal_cmd", (std::string)"cmd.exe"));
	ti.id = tabid;
	tab_bar->addTab(ti);
	tabid ++;
	
	TerminalWidget* term = new TerminalWidget(this);
	terminals[ti.id] = term;
	
	for (auto it : terminals) {
		if (it.first == ti.id) {
			it.second->show();
		}else{
			it.second->hide();
		}
	}
	
	tab_bar->selected_id = ti.id;
	App::setActiveLeafNode(term);
	
	for (auto it : terminals) {
		if (it.first == ti.id && it.second->parent != this){
			App::MoveWidget(it.second, this);
		}else if (it.first != ti.id && it.second->parent == this) {
			App::RemoveWidgetFromParent(it.second);
		}
	}
}

/*void Editor::tabinfoclicked(TabInfo info) {
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
}*/

void TerminalWidgetTabbed::tabinfoclicked(TabInfo info) {
	for (auto it : terminals) {
		if (it.first == info.id && it.second->parent != this){
			App::MoveWidget(it.second, this);
		}else if (it.first != info.id && it.second->parent == this) {
			App::RemoveWidgetFromParent(it.second);
		}
	}
	
	App::setActiveLeafNode(terminals[info.id]);
	
	for (auto it : terminals) {
		if (it.first == info.id) {
			it.second->show();
		}else {
			it.second->hide();
		}
	}
}