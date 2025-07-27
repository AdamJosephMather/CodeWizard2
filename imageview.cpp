#include "imageview.h"
#include "application.h"
#include "text_renderer.h"

#include <stb_image.h>

ImageView::ImageView(Widget* parent) : Widget(parent) {
	id = icu::UnicodeString::fromUTF8("ImageView");
}

void ImageView::position(int x, int y, int width, int height) {
	t_x = x;
	t_y = y;
	t_w = width;
	t_h = height;
}

void ImageView::openFile() {
	// Clean up any previous texture
	if (hasTexture) {
		glDeleteTextures(1, &texID);
		hasTexture = false;
	}

	if (!file || !file->ondisk) {
		return;
	}

	// Load image pixels (force RGBA)
	int channels;
	unsigned char* data = stbi_load(
		file->filepath.c_str(),
		&imgW, &imgH, &channels,
		STBI_rgb_alpha
	);

	if (!data) {
		auto text = icu::UnicodeString::fromUTF8("Failed to load: " + file->filename);
		TextRenderer::draw_text(
			t_x, t_y,
			text,
			App::theme.main_text_color
		);
		return;
	}

	// Generate GL texture
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);
	glTexImage2D(
		GL_TEXTURE_2D, 0, GL_RGBA,
		imgW, imgH, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, data
	);
	// Simple linear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(data);

	hasTexture = true;
}

void ImageView::render() {
	if (!is_visible) return;

	// background
	App::DrawRect(t_x, t_y, t_w, t_h, App::theme.main_background_color);

	// call parent in case of clipping, etc.
	Widget::render();

	if (hasTexture) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, texID);

		// Compute scale to maintain aspect ratio and not exceed available space
		float scaleX = (float)t_w / (float)imgW;
		float scaleY = (float)t_h / (float)imgH;
		float scale = min(1.0f, min(scaleX, scaleY));

		// Compute displayed width and height
		float dispW = imgW * scale;
		float dispH = imgH * scale;

		// Center the image in the available space (optional)
		float offsetX = t_x + (t_w - dispW) * 0.5f;
		float offsetY = t_y + (t_h - dispH) * 0.5f;
		
		App::DrawRect(offsetX, offsetY, dispW, dispH, App::theme.white);

		// Draw the textured quad
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 1.0f); glVertex2f(offsetX,         offsetY + dispH);
			glTexCoord2f(1.0f, 1.0f); glVertex2f(offsetX + dispW, offsetY + dispH);
			glTexCoord2f(1.0f, 0.0f); glVertex2f(offsetX + dispW, offsetY);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(offsetX,         offsetY);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
	}
}
