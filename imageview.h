#pragma once

#include "widget.h"
#include "helper_types.h"

class ImageView : public Widget {
public:
	ImageView(Widget* parent);
	void position(int x, int y, int width, int height) override;
	void render() override;
	void openFile();

	// the file to load (set by caller before openFile())
	FileInfo* file = nullptr;

private:
	// GL texture handle
	GLuint texID = 0;
	int imgW = 0, imgH = 0;
	bool hasTexture = false;
};
