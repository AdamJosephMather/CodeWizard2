#pragma once

#include <GLFW/glfw3.h>
#include "widget.h"
#include "tabs.h"

class TerminalWidgetTabbed : public Widget {
public:
	TerminalWidgetTabbed(Widget* parent);
	
	Tabs* tab_bar;
	int tabid;
	
	std::unordered_map<int,Widget*> terminals;
	
	void position(int x, int y, int w, int h) override;
	void render() override;
	
	void tabinfoclicked(TabInfo info);
	void createNew();
	
	Widget* findTerminal() override;
private:
};