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

	if (!file) {
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

	Widget::render();

	if (hasTexture) {
		glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT | GL_COLOR_BUFFER_BIT);
		
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, texID);

		// --- STEP 1: DEFINE THE "BUFFER" BACKGROUND COLOR ---
		// This is the color you want to composite the image against.
		// It simulates the "background" of the separate buffer you described.
		Color* bg = App::theme.main_background_color;
		float blendColor[] = { bg->r, bg->g, bg->b, 1.0f };
		
		// Pass this color into the Texture Environment
		glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blendColor);

		// --- STEP 2: CONFIGURE THE MATH (THE "COMBINER") ---
		// We switch from standard modulation to COMBINE mode
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

		// Set the operation to INTERPOLATE (mix)
		// Formula: Arg0 * Arg2 + Arg1 * (1 - Arg2)
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);

		// Argument 0: The Texture Image (The foreground)
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

		// Argument 1: The Constant Color (The background/matte)
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

		// Argument 2: The Mixer (Use the Texture's Alpha channel)
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

		// --- STEP 3: RENDER ---
		// IMPORTANT: DISABLE BLENDING.
		// The texture unit has already done the mixing for us.
		// We now want to overwrite the RGB on the screen with our calculated result.
		glDisable(GL_BLEND); 

		// Ensure we are drawing "White" so we don't tint the result
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		// Calculate geometry
		float scaleX = (float)t_w / (float)imgW;
		float scaleY = (float)t_h / (float)imgH;
		float scale = std::min(1.0f, std::min(scaleX, scaleY));
		float dispW = imgW * scale;
		float dispH = imgH * scale;
		float offsetX = t_x + (t_w - dispW) * 0.5f;
		float offsetY = t_y + (t_h - dispH) * 0.5f;

		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 1.0f); glVertex2f(offsetX,         offsetY + dispH);
			glTexCoord2f(1.0f, 1.0f); glVertex2f(offsetX + dispW, offsetY + dispH);
			glTexCoord2f(1.0f, 0.0f); glVertex2f(offsetX + dispW, offsetY);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(offsetX,         offsetY);
		glEnd();

		// --- STEP 4: CLEANUP / RESTORE DEFAULTS ---
		// Very important to reset this, or other textures will look weird!
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		
		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		glPopAttrib();
	}
	
	if (rounded) {
		App::DrawInverseRoundedRect(t_x, t_y, t_w, t_h, App::text_padding, App::theme.main_background_color);
		App::DrawRoundBorder(t_x, t_y, t_w, t_h, App::theme.border, 5, App::text_padding);
	}else{
		App::DrawBorder(t_x, t_y, t_w, t_h, App::theme.border);
	}
}

bool ImageView::on_mouse_button_event(int button, int action, int mods) {
	if (App::mouseX >= t_x && App::mouseX <= t_x+t_w && App::mouseY >= t_y && App::mouseY <= t_y+t_h && this != App::activeLeafNode) {
		App::setActiveLeafNode(this);
		return true;
	}
	return false;
}