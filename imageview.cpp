#include "imageview.h"
#include "application.h"
#include "text_renderer.h"

#include <stb_image.h>

ImageView::ImageView(Widget* parent) : Widget(parent) {
	id = MST::toMonoString("ImageView");
}

void ImageView::position(int x, int y, int width, int height) {
	t_x = x;
	t_y = y;
	t_w = width;
	t_h = height;
}

void ImageView::openFile(FileInfo* f) {
	file = f;
	
	App::setActiveLeafNode(this);
	
	// Clean up any previous texture
	if (hasTexture) {
		glDeleteTextures(1, &texID);
		hasTexture = false;
	}

	if (!file) {
		return;
	}
	if (!file->backend) file->backend = FileBackends::current();
	
	auto info = App::prepareTexture(file->filepath.c_str(), file->backend);
	if (info.tex == (GLuint)-1) {
		App::displayToast(MST::toMonoString("Could not load texture."));
		hasTexture = false;
		return;
	}
	
	hasTexture = true;
	texID = info.tex;
	imgW = info.imgW;
	imgH = info.imgH;
}

void ImageView::render() {
	if (!is_visible) return;
	
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.darker_background_color);

	Widget::render();

	if (hasTexture) {
		// Calculate geometry
		float scaleX = (float)t_w / (float)imgW;
		float scaleY = (float)t_h / (float)imgH;
		float scale = std::min(1.0f, std::min(scaleX, scaleY));
		float dispW = imgW * scale;
		float dispH = imgH * scale;
		float offsetX = t_x + (t_w - dispW) * 0.5f;
		float offsetY = t_y + (t_h - dispH) * 0.5f;
		
		App::renderTexture(texID, offsetX, offsetY, dispW, dispH, App::theme.darker_background_color);
	}
	
	Color* borderC = App::theme.border;
	if (this == App::activeLeafNode) {
		borderC = App::theme.active_color;
	}
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, borderC, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, borderC);
	}
}

bool ImageView::on_mouse_button_event(int button, int action, int mods) {
	if (cursor_in_this && this != App::activeLeafNode && action == GLFW_PRESS) {
		App::setActiveLeafNode(this);
		return true;
	}
	return false;
}
