#pragma once

#include "widget.h"
#include "helper_types.h"

class ImageView : public Widget {
public:
	ImageView(Widget* parent);
	void position(int x, int y, int width, int height) override;
	
	void render() override;
	bool on_mouse_button_event(int button, int action, int mods) override;
	void openFile(FileInfo* f);
	
	bool rounded = true;
	
	FileInfo* file = nullptr;

private:
	GLuint texID = 0;
	int imgW = 0, imgH = 0;
	bool hasTexture = false;
};
