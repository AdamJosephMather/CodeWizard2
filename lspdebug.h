#pragma once

#include <GLFW/glfw3.h>
#include "textedit.h"
#include "widget.h"

class LspDebug : public Widget {
public:
	LspDebug(Widget* parent);
	
	void lspmessage(std::string& from, std::string& message);
	
	bool rounded = true;
	void render();
private:
	TextEdit* viewbox = nullptr;
};