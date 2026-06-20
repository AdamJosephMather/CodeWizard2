#pragma once

#include "scrollbar.h"
#include "widget.h"
#include "helper_types.h"

class HexEditor : public Widget {
public:
	HexEditor(Widget* parent);
	
	void position(int x, int y, int width, int height) override;
	void render() override;
	bool on_mouse_button_event(int button, int action, int mods) override;
	bool on_scroll_event(double xchange, double ychange) override;
	
	void openFile(FileInfo* f);
	
	std::string toHex(int num);
	
	bool rounded = true;
	FileInfo* file = nullptr;
private:
	std::vector<std::uint8_t> bytes = {};
	
	double scroll_change = 0;
	double scroll = 0;
	int max_scroll_vert = 0;
	int lineCount = 0;
	
	bool DO_POSITION = true;
	
	Scrollbar* scrollbar;
};
